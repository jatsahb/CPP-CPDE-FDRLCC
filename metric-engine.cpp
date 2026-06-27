/**
 * metric-engine.cpp
 * 
 * Implementation of single authoritative metric engine
 */

#include "metric-engine.hpp"
#include "../apps/fdrl-consumer.hpp"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/cs.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/pit.hpp"
#include "ns3/log.h"
#include <algorithm>
#include <cmath>
#include <map>

NS_LOG_COMPONENT_DEFINE("FdrlccMetricEngine");

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_OBJECT_ENSURE_REGISTERED(MetricEngine);

TypeId
MetricEngine::GetTypeId()
{
  static TypeId tid = TypeId("ns3::ndn::fdrl::MetricEngine")
    .SetParent<Object>()
    .AddConstructor<MetricEngine>();
  return tid;
}

MetricEngine::MetricEngine()
  : m_collectionInterval(Seconds(1.0))
  , m_running(false)
{
}

MetricEngine::~MetricEngine()
{
  Stop();
}

void
MetricEngine::DoInitialize()
{
  Object::DoInitialize();
}

void
MetricEngine::DoDispose()
{
  Stop();
  m_regions.clear();
  Object::DoDispose();
}

void
MetricEngine::InitializeRegion(const std::string& region,
                               const NodeContainer& nodes,
                               const std::vector<Ptr<FdrlConsumer>>& consumers)
{
  RegionData data;
  data.nodes = nodes;
  data.consumers = consumers;
  data.prevTimestamp = Simulator::Now();
  m_regions[region] = data;
  
  NS_LOG_INFO("MetricEngine: Initialized region " << region 
                << " with " << nodes.GetN() << " nodes and " 
                << consumers.size() << " consumers");
}

void
MetricEngine::Start(Time interval)
{
  if (m_running) {
    return;
  }
  
  m_collectionInterval = interval;
  m_running = true;
  
  // Initialize all region timestamps
  for (auto& [region, data] : m_regions) {
    data.prevTimestamp = Simulator::Now();
  }
  
  // Start periodic collection
  CollectMetrics();
  
  NS_LOG_INFO("MetricEngine: Started with interval " << interval.GetSeconds() << "s");
}

void
MetricEngine::Stop()
{
  if (!m_running) {
    return;
  }
  
  m_running = false;
  if (m_collectionEvent.IsRunning()) {
    m_collectionEvent.Cancel();
  }
  
  NS_LOG_INFO("MetricEngine: Stopped");
}

const MetricSnapshot&
MetricEngine::GetLatestSnapshot(const std::string& region) const
{
  static const MetricSnapshot emptySnapshot;  // Return empty if region not found
  
  auto it = m_regions.find(region);
  if (it != m_regions.end()) {
    return it->second.latestSnapshot;
  }
  
  return emptySnapshot;
}

bool
MetricEngine::IsRegionInitialized(const std::string& region) const
{
  return m_regions.find(region) != m_regions.end();
}

void
MetricEngine::ScheduleNextCollection()
{
  if (!m_running) {
    return;
  }
  
  m_collectionEvent = Simulator::Schedule(m_collectionInterval, 
                                          &MetricEngine::CollectMetrics, 
                                          this);
}

void
MetricEngine::CollectMetrics()
{
  Time now = Simulator::Now();
  
  // Collect metrics for all regions
  for (auto& [region, data] : m_regions) {
    data.latestSnapshot = CollectRegionMetrics(region);
    NormalizeSnapshot(data.latestSnapshot);
  }
  
  // Schedule next collection
  ScheduleNextCollection();
}

MetricSnapshot
MetricEngine::CollectRegionMetrics(const std::string& region)
{
  MetricSnapshot snapshot;
  snapshot.timestamp = Simulator::Now();
  
  auto& data = m_regions[region];
  Time now = Simulator::Now();
  double deltaTime = std::max(0.1, (now - data.prevTimestamp).ToDouble(Time::S));
  
  // ============================================================================
  // Collect raw metrics from consumers (authoritative source)
  // ============================================================================
  uint64_t totalInterests = 0;
  uint64_t totalData = 0;
  uint64_t totalBytes = 0;  // Actual bytes received
  uint64_t totalDrops = 0;
  uint64_t totalCongestionMarks = 0;  // PHASE 1: Track congestion marks
  double totalDelaySum = 0.0;
  uint32_t delayCount = 0;
  
  for (const auto& consumer : data.consumers) {
    if (!consumer) continue;
    
    totalInterests += consumer->GetTotalInterestsSent();
    totalData += consumer->GetTotalDataReceived();
    totalDrops += consumer->GetTotalTimeouts();
    totalCongestionMarks += consumer->GetTotalCongestionMarks();  // PHASE 1: Aggregate congestion marks
    
    // Get actual bytes (if available) or estimate from data packets
    // TODO: Add GetTotalBytesReceived() to FdrlConsumer if needed
    // For now, estimate: assume average packet size (1024 bytes)
    totalBytes += consumer->GetTotalDataReceived() * 1024;
    
    // Get delay statistics
    totalDelaySum += consumer->GetTotalDelaySum();
    delayCount += consumer->GetDelayCount();
  }
  
  // Calculate rates
  uint64_t interestsDelta = totalInterests - data.prevInterestsSent;
  uint64_t dataDelta = totalData - data.prevDataReceived;
  uint64_t bytesDelta = totalBytes - data.prevBytesReceived;
  uint64_t dropsDelta = totalDrops - data.prevPacketsDropped;
  
  // Throughput: actual bytes * 8 bits / time / 1e6 = Mbps
  snapshot.throughputMbps = (deltaTime > 0.0) ? 
    (static_cast<double>(bytesDelta) * 8.0) / (deltaTime * 1e6) : 0.0;
  
  // Average delay
  snapshot.avgDelayMs = (delayCount > 0) ? 
    (totalDelaySum / static_cast<double>(delayCount)) : 0.0;
  
  // PHASE 1: Calculate RTT gradient (rate of change in RTT)
  if (data.hasPrevRtt && deltaTime > 0.1) {
    // Calculate gradient: (currentRTT - previousRTT) / timeDelta
    double rttDelta = snapshot.avgDelayMs - data.prevRtt;
    snapshot.rttGradient = rttDelta / deltaTime;  // ms/s
  } else {
    // First measurement or insufficient time delta
    snapshot.rttGradient = 0.0;
  }
  
  // Update previous RTT for next calculation
  data.prevRtt = snapshot.avgDelayMs;
  data.prevRttTime = now;
  data.hasPrevRtt = true;
  
  // Queue occupancy (from routers in region)
  double queueSum = 0.0;
  uint32_t routerCount = 0;
  
  for (uint32_t i = 0; i < data.nodes.GetN(); ++i) {
    Ptr<Node> node = data.nodes.Get(i);
    auto l3 = node->GetObject<ns3::ndn::L3Protocol>();
    if (!l3) continue;
    
    std::shared_ptr<nfd::Forwarder> forwarder = l3->getForwarder();
    if (!forwarder) continue;
    
    // Get PIT size as queue occupancy indicator
    // FIX: Use smaller maxPitSize for better sensitivity to small PIT size variations
    // In NDN, PIT sizes are typically 0-5 per router during normal operation
    // Using maxPitSize=5 provides better granularity: 0→0%, 1→20%, 2→40%, 3→60%, 4→80%, 5+→100%
    nfd::Pit& pit = forwarder->getPit();
    size_t pitSize = pit.size();
    const size_t maxPitSize = 5;  // Smaller max for better sensitivity (was 20, too large for typical PIT sizes)
    double queueUtil = static_cast<double>(pitSize) / static_cast<double>(maxPitSize);
    queueUtil = std::min(1.0, queueUtil);
    
    queueSum += queueUtil;
    routerCount++;
  }
  
  snapshot.queueOccupancy = (routerCount > 0) ? (queueSum / static_cast<double>(routerCount)) : 0.0;
  
  // Pending interests (PIT size)
  uint64_t pendingSum = 0;
  for (uint32_t i = 0; i < data.nodes.GetN(); ++i) {
    Ptr<Node> node = data.nodes.Get(i);
    auto l3 = node->GetObject<ns3::ndn::L3Protocol>();
    if (!l3) continue;
    
    std::shared_ptr<nfd::Forwarder> forwarder = l3->getForwarder();
    if (!forwarder) continue;
    
    nfd::Pit& pit = forwarder->getPit();
    pendingSum += pit.size();
  }
  snapshot.pendingInterests = pendingSum;
  
  // Cache hit ratio (from routers)
  uint64_t cacheHits = 0;
  uint64_t cacheMisses = 0;
  
  for (uint32_t i = 0; i < data.nodes.GetN(); ++i) {
    Ptr<Node> node = data.nodes.Get(i);
    auto l3 = node->GetObject<ns3::ndn::L3Protocol>();
    if (!l3) continue;
    
    std::shared_ptr<nfd::Forwarder> forwarder = l3->getForwarder();
    if (!forwarder) continue;
    
    const auto& counters = forwarder->getCounters();
    cacheHits += static_cast<uint64_t>(counters.nCsHits);
    cacheMisses += static_cast<uint64_t>(counters.nCsMisses);
  }
  
  uint64_t totalCacheRequests = cacheHits + cacheMisses;
  snapshot.cacheHitRatio = (totalCacheRequests > 0) ? 
    (static_cast<double>(cacheHits) / static_cast<double>(totalCacheRequests)) : 0.0;
  
  // Store raw counts
  snapshot.totalInterestsSent = totalInterests;
  snapshot.totalDataReceived = totalData;
  snapshot.totalBytesReceived = totalBytes;
  snapshot.totalPacketsDropped = totalDrops;
  snapshot.totalCongestionMarks = totalCongestionMarks;  // PHASE 1: Store congestion mark count
  snapshot.cacheHits = cacheHits;
  snapshot.cacheMisses = cacheMisses;
  
  // Update previous values for next calculation
  data.prevInterestsSent = totalInterests;
  data.prevDataReceived = totalData;
  data.prevBytesReceived = totalBytes;
  data.prevPacketsDropped = totalDrops;
  data.prevTimestamp = now;
  
  return snapshot;
}

void
MetricEngine::NormalizeSnapshot(MetricSnapshot& snapshot) const
{
  // Normalize pending interests: [0, MAX_PENDING_INTERESTS] -> [0, 1]
  snapshot.pendingInterestsNorm = std::clamp(
    static_cast<double>(snapshot.pendingInterests) / MAX_PENDING_INTERESTS, 
    0.0, 1.0);
  
  // Normalize throughput: [0, MAX_THROUGHPUT_MBPS] -> [0, 1]
  snapshot.throughputNorm = std::clamp(
    snapshot.throughputMbps / MAX_THROUGHPUT_MBPS, 
    0.0, 1.0);
  
  // Normalize delay: [0, MAX_DELAY_MS] -> [0, 1]
  snapshot.avgDelayNorm = std::clamp(
    snapshot.avgDelayMs / MAX_DELAY_MS, 
    0.0, 1.0);
  
  // PHASE 1: Normalize RTT gradient
  // Assume max gradient = 100 ms/s (reasonable for network dynamics)
  // First normalize to [-1, 1] range, then shift to [0, 1]
  constexpr double MAX_RTT_GRADIENT_MS_PER_S = 100.0;
  double gradientNorm = std::clamp(
    snapshot.rttGradient / MAX_RTT_GRADIENT_MS_PER_S,
    -1.0, 1.0);
  // Shift from [-1, 1] to [0, 1]
  snapshot.rttGradientNorm = (gradientNorm + 1.0) / 2.0;
  
  // PHASE 1: Calculate congestion mark rate
  // Rate = marked packets / total data packets received
  if (snapshot.totalDataReceived > 0) {
    snapshot.congestionMarkRate = static_cast<double>(snapshot.totalCongestionMarks) / 
                                  static_cast<double>(snapshot.totalDataReceived);
    snapshot.congestionMarkRate = std::clamp(snapshot.congestionMarkRate, 0.0, 1.0);
  } else {
    snapshot.congestionMarkRate = 0.0;
  }
  
  // queueOccupancy and cacheHitRatio are already normalized 0-1
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

