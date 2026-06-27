/**
 * fdrlcc-metrics-collection.cpp
 * 
 * Metrics collection functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-metrics-collection.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-reward-calculation.hpp"
#include "fdrlcc-csv-management.hpp"
#include "fdrlcc-enhanced-metrics.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include "src_cpp/helpers/fdrl-results-logger.hpp"
#include "src_cpp/helpers/fdrl-performance-logger.hpp"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/names.h"
#include "ns3/net-device.h"
#include "ns3/queue.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/cs.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/pit.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/fib.hpp"
#include <iostream>
#include <iomanip>
#include <set>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <limits>
#include <fstream>

NS_LOG_COMPONENT_DEFINE("FdrlccMetricsCollection");

namespace ns3 {
namespace ndn {
namespace fdrl {

RouterMetrics
CollectRouterMetrics(Ptr<Node> node)
{
  RouterMetrics metrics;
  
  auto l3 = node->GetObject<ns3::ndn::L3Protocol>();
  if (!l3) {
    return metrics;  // Not an NDN node
  }
  
  std::shared_ptr<nfd::Forwarder> forwarder = l3->getForwarder();
  if (!forwarder) {
    return metrics;
  }
  
  // Get CS metrics
  nfd::cs::Cs& cs = forwarder->getCs();
  metrics.csSize = cs.size();
  metrics.csLimit = cs.getLimit();
  metrics.csUtilization = (metrics.csLimit > 0) ? 
      (static_cast<double>(metrics.csSize) / static_cast<double>(metrics.csLimit)) : 0.0;
  
  // Get CS hit/miss statistics from forwarder counters
  const auto& counters = forwarder->getCounters();
  metrics.csHits = static_cast<uint64_t>(counters.nCsHits);
  metrics.csMisses = static_cast<uint64_t>(counters.nCsMisses);
  uint64_t totalCsRequests = metrics.csHits + metrics.csMisses;
  metrics.csHitRatio = (totalCsRequests > 0) ? 
      (static_cast<double>(metrics.csHits) / static_cast<double>(totalCsRequests)) : 0.0;
  
  // Get PIT metrics
  nfd::Pit& pit = forwarder->getPit();
  metrics.pitSize = pit.size();
  const size_t maxPitSize = 1000;  // Reasonable assumption for small topologies
  metrics.pitUtilization = static_cast<double>(metrics.pitSize) / static_cast<double>(maxPitSize);
  metrics.pitUtilization = std::min(1.0, metrics.pitUtilization);  // Cap at 1.0
  
  // Get FIB metrics
  nfd::Fib& fib = forwarder->getFib();
  metrics.fibSize = fib.size();
  
  return metrics;
}
void
CollectMetrics()
{
  double simTime = Simulator::Now().GetSeconds();
  
  // Auto-detect regions from consumer tracking (use unique regions from g_consumerRegions)
  std::set<std::string> uniqueRegions;
  for (const auto& [consumer, region] : g_consumerRegions) {
    uniqueRegions.insert(region);
  }
  std::vector<std::string> regions(uniqueRegions.begin(), uniqueRegions.end());
  std::sort(regions.begin(), regions.end());
  
  // Group consumers by region using the tracked region mapping
  std::map<std::string, std::vector<Ptr<FdrlConsumer>>> regionConsumers;
  
  for (auto& consumer : g_consumers) {
    if (!consumer) continue;
    // Look up the region for this consumer
    if (g_consumerRegions.find(consumer) != g_consumerRegions.end()) {
      std::string region = g_consumerRegions[consumer];
      regionConsumers[region].push_back(consumer);
    } else {
      // Fallback: if not tracked, use old method (shouldn't happen)
      NS_LOG_WARN("Consumer region not tracked, using fallback grouping");
      int consumersPerRegion = (g_consumers.size() > 3) ? 3 : 1;
      size_t idx = std::distance(g_consumers.begin(), 
                                  std::find(g_consumers.begin(), g_consumers.end(), consumer));
      int regionIdx = idx / consumersPerRegion;
      if (regionIdx < static_cast<int>(regions.size())) {
        regionConsumers[regions[regionIdx]].push_back(consumer);
      }
    }
  }

  // Structure to store metrics for CNPI computation
  struct RegionMetricsForCNPI {
    double throughputMbps;
    double avgDelay;
    double packetLossRate;
    double jitter;
  };
  std::map<std::string, RegionMetricsForCNPI> regionMetricsForCNPI;

  // REFACTORED: CollectMetrics is deprecated - MetricEngine handles all metric collection
  // This function is kept for backward compatibility but should not be used
  // All metrics should come from MetricEngine::GetLatestSnapshot()
  
  // Collect metrics aggregated per region (sum across all consumers in region)
  for (const auto& region : regions) {
    if (regionConsumers.find(region) == regionConsumers.end()) continue;
    
    // REMOVED: g_regionMetrics - MetricEngine is the single source of truth
    // If this function is still called, metrics should be obtained from MetricEngine
    if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) continue;
    
    auto& consumersInRegion = regionConsumers[region];
    
    // Aggregate statistics across all consumers in this region
    uint64_t totalInterests = 0;
    uint64_t totalData = 0;
    uint64_t totalTimeouts = 0;
    double totalDelaySum = 0.0;
    uint64_t totalDelayCount = 0;
    double totalSuccessfulRttSum = 0.0;    // NEW: Sum of successful RTT only
    uint64_t totalSuccessfulRttCount = 0;  // NEW: Count of successful RTT only
    
    for (auto& consumer : consumersInRegion) {
      if (!consumer) continue;
      totalInterests += consumer->GetTotalInterestsSent();
      totalData += consumer->GetTotalDataReceived();
      totalTimeouts += consumer->GetTotalTimeouts();
      totalDelaySum += consumer->GetTotalDelaySum();
      totalDelayCount += consumer->GetDelayCount();
      totalSuccessfulRttSum += consumer->GetSuccessfulRttSum();    // NEW: Track successful RTT
      totalSuccessfulRttCount += consumer->GetSuccessfulRttCount(); // NEW: Count successful RTT
    }
    
    // Calculate average delay across all consumers in region (includes timeouts)
    double avgDelay = (totalDelayCount > 0) ? 
        (totalDelaySum / static_cast<double>(totalDelayCount)) : 0.0;
    
    // REFACTORED: CollectMetrics is deprecated - MetricEngine handles all metrics
    // Note: Average successful RTT is available via MetricEngine if needed
    // Removed metrics variable updates - MetricEngine is the single source of truth
    // Get snapshot from MetricEngine if needed
    const MetricSnapshot* snapshot = nullptr;
    if (g_metricEngine && g_metricEngine->IsRegionInitialized(region)) {
      snapshot = &g_metricEngine->GetLatestSnapshot(region);
    }
    
    // Calculate per-second rates (delta from previous measurement)
    // REFACTORED: Use MetricEngine snapshot for previous values if available
    double prevTime = (snapshot) ? snapshot->timestamp.GetSeconds() : simTime - 1.0;
    double timeDelta = simTime - prevTime;
    if (timeDelta < 0.1) timeDelta = 1.0;  // Default to 1 second if too small
    
    // REFACTORED: Get previous values from MetricEngine snapshot if available
    uint64_t prevInterests = (snapshot) ? snapshot->totalInterestsSent : 0;
    uint64_t prevData = (snapshot) ? snapshot->totalDataReceived : 0;
    uint64_t prevDrops = (snapshot) ? snapshot->totalPacketsDropped : 0;
    
    uint64_t interestsDelta = totalInterests - prevInterests;
    uint64_t dataDelta = totalData - prevData;
    uint64_t dropsDelta = totalTimeouts - prevDrops;
    
    // REFACTORED: Removed metrics.timeoutRate - MetricEngine handles this
    // Note: Timeout rate is available via MetricEngine if needed
    
    double interestRate = static_cast<double>(interestsDelta) / timeDelta;  // interests/sec
    double dataRate = static_cast<double>(dataDelta) / timeDelta;  // data packets/sec
    
    // Calculate packet loss rate (drops / interests sent in this interval)
    double packetLossRate = (interestsDelta > 0) ? 
        (static_cast<double>(dropsDelta) / static_cast<double>(interestsDelta)) : 0.0;
    
    // Calculate success rate (data received / interests sent) - per-interval
    double successRate = (interestsDelta > 0) ? 
        (static_cast<double>(dataDelta) / static_cast<double>(interestsDelta)) * 100.0 : 0.0;
    
    // STEP 2: Calculate Interest Satisfaction Rate (ISR) - cumulative ground truth
    // ISR = (Satisfied Interests / Total Interests) * 100
    double isr = (totalInterests > 0) ? 
        (static_cast<double>(totalData) / static_cast<double>(totalInterests)) * 100.0 : 0.0;
    
    // REFACTORED: Removed delay samples tracking - MetricEngine handles this
    // Calculate jitter (simplified - use variance of recent delays if available)
    double jitter = 0.0;
    // Jitter calculation removed - MetricEngine handles delay statistics
    
    // Calculate throughput in Mbps
    // Data packet size: 1024 bytes (from producer PayloadSize attribute)
    // Throughput = (data packets/sec) * (bytes per packet) * (bits per byte) / (Mbps conversion)
    // Note: This is the rate at which data is received, not link utilization
    double throughputMbps = (dataRate * 1024.0 * 8.0) / 1e6;
    
    // Cap throughput at reasonable maximum (e.g., 10x link capacity for burst scenarios)
    // But don't cap too low - allow for bursts and multiple consumers
    throughputMbps = std::min(throughputMbps, 1000.0);  // Cap at 1Gbps for sanity check
    
    // REFACTORED: Removed metrics.totalBytes - MetricEngine handles this
    
    // Collect CS/PIT/FIB metrics from routers in this region
    // Scan all nodes to find routers for this region (auto-detect all router types)
    uint64_t totalCsHits = 0;
    uint64_t totalCsMisses = 0;
    double totalCsUtilization = 0.0;
    double totalPitUtilization = 0.0;
    size_t totalFibSize = 0;
    size_t routerCount = 0;
    
    // NEW: Track queue occupancy across routers in this region
    double totalQueueOccupancy = 0.0;
    size_t queueSampleCount = 0;
    
    // Open CS/PIT/FIB CSV file (only if enabled)
    static std::ofstream csPitFibCsv;
    static bool csPitFibCsvOpened = false;
    if (g_saveCsPitFib && !csPitFibCsvOpened) {
      // ENHANCEMENT: Use global results directory (set in main)
      csPitFibCsv.open(g_resultsDir + "/cs-pit-fib.csv", std::ios::out | std::ios::trunc);
      if (csPitFibCsv.is_open()) {
        csPitFibCsv << "Time,Region,Router,CS_Size,CS_Limit,CS_Utilization,CS_Hits,CS_Misses,CS_HitRatio,"
                    << "PIT_Size,PIT_Utilization,FIB_Size" << std::endl;
      }
      csPitFibCsvOpened = true;
    }
    
    // Scan all nodes to find routers for this region
    NodeContainer allNodes = NodeContainer::GetGlobal();
    for (uint32_t i = 0; i < allNodes.GetN(); i++) {
      Ptr<Node> node = allNodes.Get(i);
      std::string nodeName = Names::FindName(node);
      
      // Check if this node belongs to this region
      if (nodeName.find(region + "-") == 0) {
        size_t dashPos = nodeName.find('-');
        if (dashPos != std::string::npos) {
          std::string identifier = nodeName.substr(dashPos + 1);
          // If not consumer (C) or producer (P), it's a router
          // This auto-detects all router types: ER, ERC, ERP, SR, CR, IR, etc.
          if (identifier.length() > 0 && identifier[0] != 'C' && identifier[0] != 'P') {
            RouterMetrics routerMetrics = CollectRouterMetrics(node);
            
            if (routerMetrics.csLimit > 0 || routerMetrics.pitSize > 0 || routerMetrics.fibSize > 0) {
              totalCsHits += routerMetrics.csHits;
              totalCsMisses += routerMetrics.csMisses;
              totalCsUtilization += routerMetrics.csUtilization;
              totalPitUtilization += routerMetrics.pitUtilization;
              totalFibSize += routerMetrics.fibSize;
              routerCount++;
              
              // OPTIMIZATION: Sample queue occupancy only from bottleneck routers (SHARED-CORE, regional core routers)
              // This reduces sampling overhead by 3-5x while still capturing congestion
              bool isBottleneckRouter = (nodeName.find("SHARED-CORE") != std::string::npos) ||
                                       (nodeName.find("-CR1") != std::string::npos) ||  // Regional core routers
                                       (nodeName.find("PRODUCER-AGG") != std::string::npos);
              
              if (isBottleneckRouter) {
                uint32_t numDevices = node->GetNDevices();
                for (uint32_t devIdx = 0; devIdx < numDevices; devIdx++) {
                  Ptr<NetDevice> dev = node->GetDevice(devIdx);
                  if (dev) {
                    // Cast to PointToPointNetDevice to access GetQueue()
                    Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
                    if (p2pDev) {
                      Ptr<Queue<Packet>> queue = p2pDev->GetQueue();
                      if (queue) {
                        uint32_t queueSize = queue->GetNPackets();
                        uint32_t queueMax = queue->GetMaxSize().GetValue();
                        if (queueMax > 0) {
                          double queueOcc = static_cast<double>(queueSize) / static_cast<double>(queueMax);
                          totalQueueOccupancy += queueOcc;
                          queueSampleCount++;
                        }
                      }
                    }
                  }
                }
              }
              
              // Write detailed router-level metrics to separate CSV (only if enabled)
              if (g_saveCsPitFib && csPitFibCsv.is_open()) {
                csPitFibCsv << std::fixed << std::setprecision(3)
                            << simTime << ","
                            << region << ","
                            << nodeName << ","
                            << routerMetrics.csSize << ","
                            << routerMetrics.csLimit << ","
                            << routerMetrics.csUtilization << ","
                            << routerMetrics.csHits << ","
                            << routerMetrics.csMisses << ","
                            << routerMetrics.csHitRatio << ","
                            << routerMetrics.pitSize << ","
                            << routerMetrics.pitUtilization << ","
                            << routerMetrics.fibSize << std::endl;
              }
            }
          }
        }
      }
    }
    
    // REFACTORED: Removed metrics updates - MetricEngine handles all metrics
    // CS/PIT/FIB metrics are collected by MetricEngine
    double csUtilization = (routerCount > 0) ? totalCsUtilization / static_cast<double>(routerCount) : 0.0;
    double pitUtilization = (routerCount > 0) ? totalPitUtilization / static_cast<double>(routerCount) : 0.0;
    size_t fibSize = totalFibSize;
    double queueOccupancy = (queueSampleCount > 0) ? totalQueueOccupancy / static_cast<double>(queueSampleCount) : 0.0;
    uint64_t totalCsRequests = totalCsHits + totalCsMisses;
    double csHitRatio = (totalCsRequests > 0) ? 
        (static_cast<double>(totalCsHits) / static_cast<double>(totalCsRequests)) : 0.0;
    
    // REFACTORED: Use MetricEngine snapshot if available, otherwise create local snapshot for logging
    MetricSnapshot localSnapshot;
    if (snapshot) {
      // Use existing snapshot from MetricEngine
      localSnapshot = *snapshot;
    } else {
      // Create local snapshot for logging (deprecated function)
      localSnapshot.totalInterestsSent = totalInterests;
      localSnapshot.totalDataReceived = totalData;
      localSnapshot.totalPacketsDropped = totalTimeouts;
      localSnapshot.avgDelayMs = avgDelay;
    }
    // REFACTORED: Complete local snapshot if not using MetricEngine snapshot
    if (!snapshot) {
      localSnapshot.throughputMbps = throughputMbps;
      localSnapshot.queueOccupancy = queueOccupancy;
      localSnapshot.cacheHitRatio = csHitRatio;
      localSnapshot.timestamp = Seconds(simTime);
    }
    
    // REFACTORED: Store metrics for CNPI computation
    double lossRate = (totalInterests > 0) ? static_cast<double>(totalTimeouts) / static_cast<double>(totalInterests) : 0.0;
    regionMetricsForCNPI[region] = {throughputMbps, avgDelay, lossRate, 0.0};  // jitter removed, use 0.0 placeholder
    
    // Calculate simple reward (kept for backward compatibility, but not used in CNPI)
    double reward = CalculateSimpleReward(localSnapshot);
    
    // REMOVED: ResultsLogger reward logging - replaced by StructuredLogger
    // StructuredLogger handles reward logging via LogDRLLearning() and LogLearning()
    
    // STEP 2: Calculate congestion observability (ground truth, not agent perception)
    // Congestion thresholds (same as used in controller for consistency)
    const double QUEUE_THRESHOLD = 0.8;
    const double DELAY_THRESHOLD = 100.0;  // ms
    const double LOSS_THRESHOLD = 0.15;    // 15%
    
    bool congestionFlag = (queueOccupancy > QUEUE_THRESHOLD) || 
                          (avgDelay > DELAY_THRESHOLD) || 
                          (packetLossRate > LOSS_THRESHOLD);
    
    // Congestion severity: weighted combination of queue, delay, and loss
    double queueSeverity = std::min(1.0, queueOccupancy / QUEUE_THRESHOLD);
    double delaySeverity = std::min(1.0, avgDelay / DELAY_THRESHOLD);
    double lossSeverity = std::min(1.0, packetLossRate / LOSS_THRESHOLD);
    double congestionSeverity = std::max({queueSeverity, delaySeverity, lossSeverity});
    
    // STEP 2: Write network ground truth to summary CSV (scientific measurement format)
    if (g_summaryCsv.is_open()) {
      // Calculate current epoch for dynamic content
      extern uint32_t g_dynamicContentEpoch;
      uint32_t currentEpoch = 0;
      if (g_dynamicContentEpoch > 0) {
        currentEpoch = static_cast<uint32_t>(std::floor(simTime / g_dynamicContentEpoch));
      }
      
      g_summaryCsv << std::fixed << std::setprecision(6)
                   << simTime << ","                                    // time
                   << g_ablationConfig.ablationLabel << ","             // ablation_config
                   << region << ","                                     // region
                   << currentEpoch << ","                               // current_epoch
                   << throughputMbps << ","                             // throughput_mbps
                   << avgDelay << ","                                   // avg_delay_ms
                   << packetLossRate << ","                             // loss_rate (0-1)
                   << queueOccupancy << ","                             // queue_utilization (0-1)
                   << csHitRatio << ","                                 // cache_hit_ratio (0-1)
                   << (congestionFlag ? 1 : 0) << ","                   // congestion_flag (0 or 1)
                   << congestionSeverity << ","                         // congestion_severity (0-1)
                   << isr << ","                                        // interest_satisfaction_rate (0-100)
                   << totalInterests << ","                             // interests (cumulative)
                   << totalData << ","                                  // data (cumulative)
                   << totalTimeouts << ","                              // drops (cumulative)
                   << interestRate << ","                                // interest_rate (per second)
                   << dataRate << ","                                   // data_rate (per second)
                   << jitter << ","                                     // jitter
                   << csHitRatio << ","                                 // cs_hit_ratio (duplicate for compatibility)
                   << csUtilization << ","                              // cs_utilization
                   << pitUtilization << ","                             // pit_utilization
                   << fibSize << std::endl;                             // fib_size
    }
    
    // Note: comprehensive_analysis.csv removed (redundant with summary.csv + training CSVs)
    // All DRL/FL metrics are available in dedicated CSV files:
    // - summary.csv: Network metrics per region
    // - training_metrics.csv: Training metrics  
    // - fl_metrics.csv: Federated learning metrics
    // - policy_actions.csv: Action traces
    // - experience_dataset.csv: Full transition data
    
    // Note: CNPI will be computed and displayed after all regions are processed
    
    // REFACTORED: Use local variables instead of metrics
    QueueMetrics queueMetrics;
    queueMetrics.current = static_cast<uint64_t>(pitUtilization * 1000); // PIT size as queue proxy
    queueMetrics.max = 1000; // Max PIT size assumption
    queueMetrics.peak = static_cast<uint64_t>(pitUtilization * 1000); // Use current as peak for now
    queueMetrics.utilization = pitUtilization;
    
    // Calculate average queue size (simplified - use current PIT utilization)
    static std::map<std::string, std::vector<uint64_t>> queueSamples;
    queueSamples[region].push_back(queueMetrics.current);
    if (queueSamples[region].size() > 100) {
      queueSamples[region].erase(queueSamples[region].begin());
    }
    if (!queueSamples[region].empty()) {
      uint64_t sum = 0;
      for (uint64_t s : queueSamples[region]) {
        sum += s;
      }
      queueMetrics.average = static_cast<double>(sum) / static_cast<double>(queueSamples[region].size());
    }
    
    // STEP 1: REMOVED WriteQueueMetrics() - data already in logs/congestion/congestion_*.csv (StructuredLogger)
    // Queue metrics are logged via StructuredLogger::LogCongestionMetrics()
    
    // REFACTORED: Removed metrics.prev* updates - MetricEngine handles this
    
    // Reset delay stats for all consumers in this region
    for (auto& consumer : consumersInRegion) {
      if (consumer) {
        consumer->ResetDelayStats();
      }
    }
  }
  
  // Compute CNPI for all regions using dynamic normalization
  // CNPI aggregates throughput, latency, packet loss, and jitter into a single normalized performance index for cross-region comparison.
  std::map<std::string, double> regionCNPI;
  if (!regionMetricsForCNPI.empty()) {
    // Find maximum values across all regions for normalization
    double T_max = 0.0;
    double L_max = 0.0;
    double J_max = 0.0;
    
    for (const auto& [region, metrics] : regionMetricsForCNPI) {
      // REFACTORED: Use regionMetricsForCNPI instead of metrics
      if (regionMetricsForCNPI[region].throughputMbps > T_max) T_max = regionMetricsForCNPI[region].throughputMbps;
      if (regionMetricsForCNPI[region].avgDelay > L_max) L_max = regionMetricsForCNPI[region].avgDelay;
      if (regionMetricsForCNPI[region].jitter > J_max) J_max = regionMetricsForCNPI[region].jitter;
    }
    
    // Ensure non-zero denominators (use minimum thresholds if all values are zero)
    if (T_max < 0.001) T_max = 1.0;
    if (L_max < 0.001) L_max = 1.0;
    if (J_max < 0.001) J_max = 1.0;
    
    // Compute CNPI for each region
    for (const auto& [region, metrics] : regionMetricsForCNPI) {
      // Normalize metrics
      double T_norm = metrics.throughputMbps / T_max;
      double L_norm = 1.0 - (metrics.avgDelay / L_max);
      double P_norm = 1.0 - metrics.packetLossRate;
      double J_norm = 1.0 - (metrics.jitter / J_max);
      
      // Ensure normalized values are in [0, 1] range
      T_norm = std::max(0.0, std::min(1.0, T_norm));
      L_norm = std::max(0.0, std::min(1.0, L_norm));
      P_norm = std::max(0.0, std::min(1.0, P_norm));
      J_norm = std::max(0.0, std::min(1.0, J_norm));
      
      // Compute CNPI
      double cnpi = 0.40 * T_norm + 0.25 * L_norm + 0.25 * P_norm + 0.10 * J_norm;
      regionCNPI[region] = cnpi;
      
    }
  }
  
  // REFACTORED: Removed g_regionMetrics - CNPI should be stored in MetricEngine if needed
  // Update CNPI and log common metrics with CNPI (deferred logging)
  auto& perfLogger = PerformanceLogger::GetInstance();
  for (const auto& [region, cnpi] : regionCNPI) {
    // REMOVED: g_regionMetrics update - MetricEngine is the single source of truth
    
    // Log common metrics with computed CNPI (deferred logging after CNPI computation)
    if (regionConsumers.find(region) != regionConsumers.end()) {
      auto& consumersInRegion = regionConsumers[region];
      
      // Recalculate key metrics for logging
      uint64_t totalInterests = 0, totalData = 0, totalTimeouts = 0;
      double totalDelaySum = 0.0;
      uint64_t totalDelayCount = 0;
      
      for (auto& consumer : consumersInRegion) {
        if (!consumer) continue;
        totalInterests += consumer->GetTotalInterestsSent();
        totalData += consumer->GetTotalDataReceived();
        totalTimeouts += consumer->GetTotalTimeouts();
        totalDelaySum += consumer->GetTotalDelaySum();
        totalDelayCount += consumer->GetDelayCount();
      }
      
      double avgDelay = (totalDelayCount > 0) ? 
          (totalDelaySum / static_cast<double>(totalDelayCount)) : 0.0;
      
      // REFACTORED: Get previous values from MetricEngine snapshot
      uint64_t prevInterests_local = 0;
      uint64_t prevData_local = 0;
      double prevTime_local = simTime - 1.0;  // Default to 1 second ago
      
      if (g_metricEngine && g_metricEngine->IsRegionInitialized(region)) {
        const MetricSnapshot& snap = g_metricEngine->GetLatestSnapshot(region);
        prevInterests_local = snap.totalInterestsSent;
        prevData_local = snap.totalDataReceived;
        prevTime_local = snap.timestamp.GetSeconds();
      }
      
      double timeDelta = simTime - prevTime_local;
      if (timeDelta < 0.1) timeDelta = 1.0;
      uint64_t interestsDelta = totalInterests - prevInterests_local;
      uint64_t dataDelta = totalData - prevData_local;
      double interestRate = static_cast<double>(interestsDelta) / timeDelta;
      double dataRate = static_cast<double>(dataDelta) / timeDelta;
      double throughputMbps = (dataRate * 1024.0 * 8.0) / 1e6;
      throughputMbps = std::min(throughputMbps, 1000.0);
      
      // REFACTORED: Use local variables and MetricEngine snapshot
      double jitter = 0.0;  // Simplified - MetricEngine handles jitter calculation
      
      // REFACTORED: Get CS/PIT/FIB metrics from MetricEngine if available
      // Since CS/PIT/FIB metrics are not in MetricSnapshot, we need to recalculate them
      // or use default values. For CNPI logging, we'll use simplified values.
      double csHitRatio_local = 0.0;
      double csUtilization_local = 0.0;
      double pitUtilization_local = 0.0;
      size_t fibSize_local = 0;
      
      // REFACTORED: Get prevDrops from MetricEngine snapshot or use 0
      uint64_t prevDrops_local = 0;
      if (g_metricEngine && g_metricEngine->IsRegionInitialized(region)) {
        const MetricSnapshot& snap = g_metricEngine->GetLatestSnapshot(region);
        csHitRatio_local = snap.cacheHitRatio;
        prevDrops_local = snap.totalPacketsDropped;
        // Note: CS utilization, PIT utilization, FIB size not in MetricSnapshot
        // For CNPI logging, use simplified values (0.0) or recalculate from routers if needed
        csUtilization_local = 0.0;  // Not available in snapshot
        pitUtilization_local = snap.queueOccupancy;  // Use queue occupancy as proxy
        fibSize_local = 0;  // Not available in snapshot
      }
      
      double packetLossRate = (interestsDelta > 0) ? 
          (static_cast<double>(totalTimeouts - prevDrops_local) / static_cast<double>(interestsDelta)) : 0.0;
      
      PerformanceLogger::CommonMetrics commonMetrics;
      commonMetrics.timestamp = simTime;
      commonMetrics.nodeId = region;
      commonMetrics.throughputMbps = throughputMbps;
      commonMetrics.e2eLatencyMs = avgDelay;
      commonMetrics.jitterMs = jitter;
      commonMetrics.packetLossRatio = packetLossRate;
      commonMetrics.interestGenRate = interestRate;
      commonMetrics.dataSatisfactionRate = dataRate;
      commonMetrics.csHitRatio = csHitRatio_local;
      commonMetrics.csUtilization = csUtilization_local;
      commonMetrics.pitOccupancy = pitUtilization_local;
      commonMetrics.fibSize = static_cast<size_t>(fibSize_local);
      commonMetrics.cnpi = cnpi;  // Now with computed CNPI
      perfLogger.LogCommonMetrics(commonMetrics);
    }
  }
  
  // REFACTORED: MetricEngine handles periodic collection internally
  // Removed recursive scheduling - CollectMetrics() is deprecated
  // Simulator::Schedule(Seconds(1.0), &CollectMetrics);  // REMOVED
}
/**
 * Collect AI-specific metrics (called every 2 seconds for DRL/FDRL)
 */
void
CollectAIMetrics()
{
  double simTime = Simulator::Now().GetSeconds();
  
  if (g_selectedAlgorithm != CCAlgorithm::FDRLCC) {
    // Not an AI algorithm - schedule anyway but return early
    Simulator::Schedule(Seconds(2.0), &CollectAIMetrics);
    return;
  }
  
  auto& perfLogger = PerformanceLogger::GetInstance();
  
  // Collect AI metrics for each region
  for (auto& [region, drl] : g_regionDRL) {
    // Update reward history (for rolling window calculations)
    perfLogger.UpdateRewardHistory(region, drl.reward);
    
    // Build AI metrics
    PerformanceLogger::AIMetrics aiMetrics;
    aiMetrics.timestamp = simTime;
    aiMetrics.nodeId = region;
    aiMetrics.rlAction = drl.rateFactor;  // Current rate factor
    aiMetrics.reward = drl.reward;
    aiMetrics.rewardAvg = perfLogger.GetRewardAverage(region);  // From rolling window
    aiMetrics.rewardVariance = perfLogger.GetRewardVariance(region);  // From rolling window
    aiMetrics.lossValue = -1.0;  // Training loss not directly available
    aiMetrics.policyEntropy = -1.0;  // Not implemented
    aiMetrics.convergenceIndicator = perfLogger.CalculateConvergenceIndicator(region);
    aiMetrics.divergenceIndicator = 0;  // Set based on FL divergence (propagated)
    aiMetrics.trainingStep = drl.trainingStep;
    
    perfLogger.LogAIMetrics(aiMetrics);
  }
  
  // Schedule next AI metrics collection
  Simulator::Schedule(Seconds(2.0), &CollectAIMetrics);
}


} // namespace fdrl
} // namespace ndn
} // namespace ns3

