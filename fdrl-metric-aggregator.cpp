/**
 * FDRLCC Metric Aggregator Implementation - FIXED VERSION v2
 * 
 * Key fixes for realistic metrics:
 * 1. Queue: Direct sampling from actual device queues, peak tracking
 * 2. Delay: Track both RTT and cache-hit delays separately
 * 3. Continuous sampling: Sample queues every 50ms for better accuracy
 * 4. Pending interests tracking: Shows network load
 */

#include "fdrl-metric-aggregator.hpp"
#include "../simulation/fdrlcc-structured-logger.hpp"  // For congestion logging
#include "../simulation/fdrlcc-types.hpp"  // For g_structuredLogger, g_enableStructuredLogs

#include "ns3/log.h"
#include "ns3/config.h"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/net-device.h"
#include "ns3/channel.h"
#include "ns3/queue.h"
#include "ns3/drop-tail-queue.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>

NS_LOG_COMPONENT_DEFINE("ndn.FdrlMetricAggregator");

namespace ns3 {
namespace ndn {
namespace fdrl {

MetricAggregator::MetricAggregator(const AggregatorConfig& config)
    : m_config(config)
    , m_interestsSent(0)
    , m_dataReceived(0)
    , m_packetsDropped(0)
    , m_bytesReceived(0)
    , m_packetsInQueue(0)
    , m_delaySum(0.0)
    , m_delayCount(0)
    , m_queueCurrent(0)
    , m_queueMax(config.maxQueueSize)
    , m_queuePeak(0)
    , m_pendingInterests(0)
    , m_cacheHits(0)
    , m_networkResponses(0)
    , m_samplingActive(false)
    , m_congestionState(CongestionState::NOT_CONGESTED)
    , m_congestionEnterTime(-1.0)
    , m_reactionLatency(-1.0)
    , m_rateReducedAfterCongestion(false)
{
    m_lastAction.rateFactor = 1.0;
    m_lastAction.queueFactor = 1.0;
    NS_LOG_INFO("MetricAggregator created with maxQueueSize=" << config.maxQueueSize);
}

MetricAggregator::~MetricAggregator()
{
    Shutdown();
}

bool MetricAggregator::Initialize(const std::string& resultsDir)
{
    m_resultsDir = resultsDir;
    // REFACTORED: Removed CSV/debug file initialization - MetricAggregator only collects raw metrics
    // Output/logging moved to MetricSinkConsole/MetricSinkCsv
    return true;
}

void MetricAggregator::ConnectTraces(const NodeContainer& bottleneckNodes)
{
    NS_LOG_INFO("Connecting trace sources on " << bottleneckNodes.GetN() << " nodes");
    
    for (uint32_t i = 0; i < bottleneckNodes.GetN(); ++i) {
        Ptr<Node> node = bottleneckNodes.Get(i);
        
        for (uint32_t d = 0; d < node->GetNDevices(); ++d) {
            Ptr<NetDevice> dev = node->GetDevice(d);
            Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
            
            if (p2pDev) {
                m_monitoredDevices.push_back(p2pDev);
                NS_LOG_INFO("Monitoring P2P device on node " << node->GetId() << " device " << d);
            }
        }
    }
    
    // Connect queue drop traces via Config system
    bool connected = Config::ConnectWithoutContextFailSafe(
        "/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/TxQueue/Drop",
        MakeCallback(&MetricAggregator::RecordQueueDrop, this)
    );
    NS_LOG_INFO("Queue drop trace connected: " << (connected ? "yes" : "no"));
    
    // Start periodic queue sampling (every 50ms for better accuracy)
    StartPeriodicSampling();
    
    NS_LOG_INFO("Trace connections completed. Monitoring " << m_monitoredDevices.size() << " devices");
}

void MetricAggregator::StartPeriodicSampling()
{
    if (!m_samplingActive) {
        m_samplingActive = true;
        PeriodicQueueSample();
    }
}

void MetricAggregator::PeriodicQueueSample()
{
    if (!m_samplingActive) return;
    
    // Sample current queue lengths
    uint64_t totalQueue = 0;
    uint64_t totalMax = 0;
    
    for (auto& dev : m_monitoredDevices) {
        if (dev) {
            Ptr<Queue<Packet>> queue = dev->GetQueue();
            if (queue) {
                totalQueue += queue->GetNPackets();
                totalMax += queue->GetMaxSize().GetValue();
            }
        }
    }
    
    // Update current and peak
    m_queueCurrent.store(totalQueue, std::memory_order_relaxed);
    if (totalMax > 0) {
        m_queueMax.store(totalMax, std::memory_order_relaxed);
    }
    
    // Track peak queue length this second
    uint64_t currentPeak = m_queuePeak.load(std::memory_order_relaxed);
    if (totalQueue > currentPeak) {
        m_queuePeak.store(totalQueue, std::memory_order_relaxed);
    }
    
    // Schedule next sample in 50ms
    Simulator::Schedule(MilliSeconds(50), &MetricAggregator::PeriodicQueueSample, this);
}

void MetricAggregator::DisconnectTraces()
{
    m_samplingActive = false;
    m_monitoredDevices.clear();
}

void MetricAggregator::RecordInterestSent(uint32_t bytes)
{
    m_interestsSent.fetch_add(1, std::memory_order_relaxed);
    m_pendingInterests.fetch_add(1, std::memory_order_relaxed);
    NS_LOG_DEBUG("Interest sent, total=" << m_interestsSent.load() 
                 << ", pending=" << m_pendingInterests.load());
}

void MetricAggregator::RecordDataReceived(uint32_t bytes, double delayMs)
{
    m_dataReceived.fetch_add(1, std::memory_order_relaxed);
    m_bytesReceived.fetch_add(bytes, std::memory_order_relaxed);
    
    // Decrease pending count (data received means interest satisfied)
    uint64_t pending = m_pendingInterests.load(std::memory_order_relaxed);
    if (pending > 0) {
        m_pendingInterests.fetch_sub(1, std::memory_order_relaxed);
    }
    
    // Track delay - distinguish cache hits from network responses
    if (delayMs < 10.0) {
        // Likely a cache hit (very low delay)
        m_cacheHits.fetch_add(1, std::memory_order_relaxed);
    } else {
        // Network response with real RTT
        m_networkResponses.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Always record delay if positive
    if (delayMs > 0.0) {
        std::lock_guard<std::mutex> lock(m_delayMutex);
        m_delaySum += delayMs;
        ++m_delayCount;
    }
    
    NS_LOG_DEBUG("Data received: " << bytes << " bytes, delay=" << delayMs << "ms");
}

void MetricAggregator::RecordQueueDrop(Ptr<const Packet> packet)
{
    m_packetsDropped.fetch_add(1, std::memory_order_relaxed);
    NS_LOG_DEBUG("Queue drop! Total drops=" << m_packetsDropped.load());
}

void MetricAggregator::RecordEnqueue(Ptr<const Packet> packet)
{
    // No longer used - we sample directly from queues
}

void MetricAggregator::RecordDequeue(Ptr<const Packet> packet)
{
    // No longer used - we sample directly from queues
}

void MetricAggregator::OnActionApplied(const AggregatorAction& action)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastAction = action;
    NS_LOG_DEBUG("Action applied: rate=" << action.rateFactor);
    
    // Check congestion state after action is applied
    double queueOccupancy = GetQueueOccupancy();
    double avgDelay = (m_delayCount > 0) ? (m_delaySum / m_delayCount) : 0.0;
    double lossRate = 0.0;
    uint64_t totalSent = m_interestsSent.load();
    uint64_t totalDropped = m_packetsDropped.load();
    if (totalSent > 0) {
        lossRate = static_cast<double>(totalDropped) / static_cast<double>(totalSent);
    }
    
    CheckCongestionState(queueOccupancy, avgDelay, lossRate, action);
}

void MetricAggregator::SampleQueueLengths()
{
    // Queue is now sampled periodically (every 50ms)
    // This function just ensures we have latest data
    uint64_t totalQueue = 0;
    uint64_t totalMax = 0;
    
    for (auto& dev : m_monitoredDevices) {
        if (dev) {
            Ptr<Queue<Packet>> queue = dev->GetQueue();
            if (queue) {
                totalQueue += queue->GetNPackets();
                totalMax += queue->GetMaxSize().GetValue();
            }
        }
    }
    
    if (totalMax == 0) {
        totalMax = static_cast<uint64_t>(m_config.maxQueueSize);
    }
    
    m_queueCurrent.store(totalQueue, std::memory_order_relaxed);
    m_queueMax.store(totalMax, std::memory_order_relaxed);
    
    NS_LOG_DEBUG("Queue sampled: " << totalQueue << "/" << totalMax 
                 << ", peak=" << m_queuePeak.load());
}

// REFACTORED: FlushPerSecond and PrintHeader removed - output moved to MetricSinkConsole/MetricSinkCsv

void MetricAggregator::Shutdown()
{
    m_samplingActive = false;
    DisconnectTraces();
    // REFACTORED: No file closing needed - CSV/debug files removed
}

double MetricAggregator::GetQueueOccupancy() const
{
    uint64_t cur = m_queueCurrent.load(std::memory_order_relaxed);
    uint64_t max = m_queueMax.load(std::memory_order_relaxed);
    return (max > 0) ? static_cast<double>(cur) / static_cast<double>(max) : 0.0;
}

// REFACTORED: GetCongestion, GetThroughputMbps, ComputeThroughputSign, PrintConsoleLine, WriteCsvRow removed
// Metric computation moved to MetricEngine, output moved to MetricSinkConsole/MetricSinkCsv

void MetricAggregator::ResetCounters()
{
    m_interestsSent.store(0, std::memory_order_relaxed);
    m_dataReceived.store(0, std::memory_order_relaxed);
    m_packetsDropped.store(0, std::memory_order_relaxed);
    m_bytesReceived.store(0, std::memory_order_relaxed);
    m_queuePeak.store(0, std::memory_order_relaxed);  // Reset peak for new second
    m_cacheHits.store(0, std::memory_order_relaxed);
    m_networkResponses.store(0, std::memory_order_relaxed);
    // Don't reset pendingInterests - that's live state
    
    {
        std::lock_guard<std::mutex> lock(m_delayMutex);
        m_delaySum = 0.0;
        m_delayCount = 0;
    }
}

void MetricAggregator::CheckCongestionState(double queueOccupancy, double delayMs, double lossRate,
                                            const AggregatorAction& action)
{
    // Congestion detection using existing thresholds (same as in controller)
    bool isCongested = (queueOccupancy > QUEUE_THRESHOLD_HIGH || lossRate > LOSS_THRESHOLD_HIGH) ||  // High congestion
                       (queueOccupancy > QUEUE_THRESHOLD_MODERATE || lossRate > LOSS_THRESHOLD_MODERATE) ||  // Moderate congestion
                       (delayMs > DELAY_THRESHOLD || queueOccupancy > QUEUE_THRESHOLD_HIGH);  // High latency/queue
    
    double currentTime = Simulator::Now().GetSeconds();
    std::string region = "default";  // TODO: Get actual region if available
    
    // State machine transitions
    if (isCongested && m_congestionState == CongestionState::NOT_CONGESTED) {
        // CONGESTION_ENTER
        m_congestionState = CongestionState::CONGESTED;
        m_congestionEnterTime = currentTime;
        m_rateReducedAfterCongestion = false;
        m_reactionLatency = -1.0;  // Reset
        
        // Log CONGESTION_ENTER event
        if (g_enableStructuredLogs && g_structuredLogger) {
            std::ostringstream details;
            details << "Congestion detected | queue=" << std::fixed << std::setprecision(3) << queueOccupancy
                    << " delay=" << delayMs << "ms"
                    << " loss=" << std::setprecision(4) << lossRate;
            g_structuredLogger->LogEvent(currentTime, "CONGESTION_ENTER", details.str());
            
            // Log congestion metrics at detection time
            g_structuredLogger->LogCongestion(currentTime, region,
                                             queueOccupancy,  // queue
                                             delayMs,         // delay
                                             lossRate,        // loss
                                             std::vector<double>());  // state (optional)
        }
        
        NS_LOG_INFO("[CONGESTION_ENTER] t=" << currentTime << "s queue=" << queueOccupancy 
                    << " delay=" << delayMs << "ms loss=" << lossRate);
    }
    else if (!isCongested && m_congestionState == CongestionState::CONGESTED) {
        // CONGESTION_EXIT
        m_congestionState = CongestionState::NOT_CONGESTED;
        
        // Log CONGESTION_EXIT event
        if (g_enableStructuredLogs && g_structuredLogger) {
            double congestionDuration = currentTime - m_congestionEnterTime;
            std::ostringstream details;
            details << "Congestion cleared | duration=" << std::fixed << std::setprecision(2) 
                    << congestionDuration << "s"
                    << " reaction_latency=";
            if (m_reactionLatency >= 0.0) {
                details << std::setprecision(3) << m_reactionLatency << "s";
            } else {
                details << "N/A";
            }
            g_structuredLogger->LogEvent(currentTime, "CONGESTION_EXIT", details.str());
        }
        
        NS_LOG_INFO("[CONGESTION_EXIT] t=" << currentTime << "s duration=" 
                    << (currentTime - m_congestionEnterTime) << "s");
        
        // Reset tracking
        m_congestionEnterTime = -1.0;
        m_rateReducedAfterCongestion = false;
        m_reactionLatency = -1.0;
    }
    
    // Track reaction latency: time from congestion enter to first rate reduction
    if (m_congestionState == CongestionState::CONGESTED && 
        m_congestionEnterTime >= 0.0 && 
        !m_rateReducedAfterCongestion) {
        
        // Check if current action is a rate reduction (rateFactor < 1.0)
        if (action.rateFactor < 1.0) {
            m_rateReducedAfterCongestion = true;
            m_reactionLatency = currentTime - m_congestionEnterTime;
            
            // Log reaction latency
            if (g_enableStructuredLogs && g_structuredLogger) {
                std::ostringstream details;
                details << "Reaction latency: " << std::fixed << std::setprecision(3) 
                        << m_reactionLatency << "s"
                        << " | rate_reduced_to=" << action.rateFactor;
                g_structuredLogger->LogEvent(currentTime, "CONGESTION_REACTION", details.str());
            }
            
            NS_LOG_INFO("[CONGESTION_REACTION] t=" << currentTime << "s latency=" 
                        << m_reactionLatency << "s rate=" << action.rateFactor);
        }
    }
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3
