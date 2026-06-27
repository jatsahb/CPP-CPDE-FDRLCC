/**
 * fdrlcc-results-output.cpp
 * 
 * Results output and console display functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-results-output.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-metrics-collection.hpp"
#include "fdrlcc-console-colors.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include "src_cpp/helpers/fdrl-performance-logger.hpp"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/names.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>
#include <vector>
#include <map>
#include <algorithm>
#include <limits>
#include <cmath>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Schedule console output (recursive scheduler)
 */
void
ScheduleConsoleOutput()
{
  double simTime = Simulator::Now().GetSeconds();
  PrintEnhancedConsoleOutput(simTime);
  
  // Schedule next console output (every 5 seconds)
  Simulator::Schedule(Seconds(5.0), &ScheduleConsoleOutput);
}

/**
 * ENHANCEMENT: Print enhanced console output with full system status
 */
void
PrintEnhancedConsoleOutput(double simTime)
{
  std::ostringstream oss;
  
  std::string algoName = (g_selectedAlgorithm == CCAlgorithm::FDRLCC) ? "FDRLCC" :
                         (g_selectedAlgorithm == CCAlgorithm::AIMD) ? "AIMD" :
                         (g_selectedAlgorithm == CCAlgorithm::BIC) ? "BIC" : "CUBIC";
  
  oss << "\n" << ColorOutput::Info() << "╔══════════════════════════════════════════════════════════════════════════════╗" << ColorOutput::Reset() << "\n";
  oss << ColorOutput::Info() << "║" << ColorOutput::Reset() << " [t=" << std::fixed << std::setprecision(1) << simTime << "s] " << ColorOutput::Success() << algoName << ColorOutput::Reset() << " STATUS\n";
  oss << ColorOutput::Info() << "╠══════════════════════════════════════════════════════════════════════════════╣" << ColorOutput::Reset() << "\n";
  
  // Track previous values for rate calculation (static to persist across calls)
  static std::map<std::string, std::pair<double, std::pair<uint64_t, uint64_t>>> prevConsoleValues;
  
  // Collect all region data first for organized display
  struct RegionDisplayData {
    std::string region;
    double throughputMbps;
    double avgDelay;
    double packetLossRate;
    double interestRate;
    double dataRate;
    double drlReward;
    double drlRateFactor;
    double rewardAvg;
    double rewardVar;
    int convergenceInd;
    double divergenceInd;
    size_t bufferSize;
    size_t targetWarmupSize;
    std::string trainingStatus;
    double queueUtilization;
  };
  std::vector<RegionDisplayData> regionData;
  
  // REFACTORED: Use MetricEngine instead of g_regionMetrics
  for (const auto& region : g_topologyInfo.regions) {
    if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) continue;
    const MetricSnapshot& metrics = g_metricEngine->GetLatestSnapshot(region);
    // Get current snapshot for this region
    auto regionConsumers = g_consumers;
    std::vector<Ptr<FdrlConsumer>> consumersInRegion;
    for (auto& consumer : regionConsumers) {
      if (g_consumerRegions.find(consumer) != g_consumerRegions.end() &&
          g_consumerRegions[consumer] == region) {
        consumersInRegion.push_back(consumer);
      }
    }
    
    if (consumersInRegion.empty()) continue;
    
    // Calculate current metrics (cumulative totals)
    uint64_t totalInterests = 0;
    uint64_t totalData = 0;
    uint64_t totalTimeouts = 0;
    double totalDelay = 0.0;
    uint32_t delayCount = 0;
    double minDelay = std::numeric_limits<double>::max();
    double maxDelay = 0.0;
    
    for (auto& consumer : consumersInRegion) {
      if (consumer) {
        totalInterests += consumer->GetTotalInterestsSent();
        totalData += consumer->GetTotalDataReceived();
        totalTimeouts += consumer->GetTotalTimeouts();
        
        double delaySum = consumer->GetTotalDelaySum();
        uint64_t delayCnt = consumer->GetDelayCount();
        if (delayCnt > 0) {
          totalDelay += delaySum;
          delayCount += delayCnt;
          double avgDelayForConsumer = delaySum / static_cast<double>(delayCnt);
          minDelay = std::min(minDelay, avgDelayForConsumer);
          maxDelay = std::max(maxDelay, avgDelayForConsumer);
        }
      }
    }
    
    // Calculate average delay from actual measurements (no hardcoded minimums)
    double avgDelay = 0.0;
    if (delayCount > 0) {
      // Use actual calculated delay from measurements
      avgDelay = totalDelay / static_cast<double>(delayCount);
    } else if (metrics.avgDelayMs > 0.0) {
      // REFACTORED: Use MetricEngine snapshot (lastAvgDelay -> avgDelayMs)
      avgDelay = metrics.avgDelayMs;
    }
    // If delayCount is 0 and lastAvgDelay is 0, avgDelay remains 0 (no hardcoded fallback)
    
    // Calculate per-second rates using previous console print values
    double prevTime = prevConsoleValues[region].first;
    uint64_t prevInterests = prevConsoleValues[region].second.first;
    uint64_t prevData = prevConsoleValues[region].second.second;
    
    // Time delta since last console print (actual time difference, no hardcoded fallback)
    double dt = simTime - prevTime;
    if (dt < 0.001) dt = 5.0;  // Use console interval (5s) only for first call when prevTime is 0
    
    // Calculate rates (delta per second)
    double interestRate = (dt > 0) ? ((totalInterests - prevInterests) / dt) : 0.0;
    double dataRate = (dt > 0) ? ((totalData - prevData) / dt) : 0.0;
    
    // Calculate throughput (data rate in Mbps)
    // Throughput = (data packets/sec) * (bytes per packet) * (bits per byte) / (Mbps conversion)
    double throughputMbps = (dataRate * 1024.0 * 8.0) / 1e6;
    
    // Calculate packet loss rate (cumulative drops / cumulative interests)
    double packetLossRate = (totalInterests > 0) ? (static_cast<double>(totalTimeouts) / totalInterests) : 0.0;
    
    // Update previous values for next console print
    prevConsoleValues[region] = std::make_pair(simTime, std::make_pair(totalInterests, totalData));
    // successRate calculated but not used in console output (used in CSV)
    // double successRate = (totalInterests > 0) ? (static_cast<double>(totalData) / totalInterests) : 0.0;
    
    // Get DRL state (only if FDRLCC is active)
    double drlReward = 0.0;
    double drlRateFactor = 1.0;  // Default rate factor
    if (g_selectedAlgorithm == CCAlgorithm::FDRLCC && g_regionDRL.find(region) != g_regionDRL.end()) {
      auto& drl = g_regionDRL[region];  // Use non-const reference to access current state
      drlReward = drl.reward;
      
      // Get rate factor: use rateFactor which is explicitly updated by ApplyFDRLCCActions()
      // ApplyFDRLCCActions() sets: drl.rateFactor = drl.action[0] (line 2032)
      // So rateFactor always contains the current applied rate factor
      drlRateFactor = drl.rateFactor;
      
      // Ensure rate factor is in valid range [0.5, 2.0] (sanity check)
      // This prevents any invalid values (like 0, 5, etc.) from being logged
      // If rateFactor is 0 or invalid, default to 1.0
      if (drlRateFactor < 0.5 || drlRateFactor > 2.0) {
        // Invalid value detected - use default or fallback to action[0] if available
        if (!drl.action.empty() && drl.action[0] >= 0.5 && drl.action[0] <= 2.0) {
          drlRateFactor = drl.action[0];
        } else {
          drlRateFactor = 1.0;  // Safe default
        }
      }
      // Clamp to valid range
      drlRateFactor = std::max(0.5, std::min(2.0, drlRateFactor));
    }
    
    // Get router metrics (need to find a router node for this region)
    RouterMetrics routerMetrics;
    double queueUtilization = 0.0;
    // Try to find a router node for this region (use first router we can find)
    NodeContainer allNodes = NodeContainer::GetGlobal();
    for (uint32_t i = 0; i < allNodes.GetN(); i++) {
      Ptr<Node> node = allNodes.Get(i);
      std::string nodeName = Names::FindName(node);
      if (nodeName.find(region) != std::string::npos && 
          (nodeName.find("ER") != std::string::npos || 
           nodeName.find("IR") != std::string::npos || 
           nodeName.find("CR") != std::string::npos)) {
        routerMetrics = CollectRouterMetrics(node);
        queueUtilization = routerMetrics.pitUtilization * 100.0;  // Convert to percentage
        break;
      }
    }
    
    // Get DRL metrics if FDRLCC is active
    double rewardAvg = -1.0;
    double rewardVar = -1.0;
    int convergenceInd = -1;
    double divergenceInd = 0.0;
    size_t bufferSize = 0;
    size_t targetWarmupSize = 100;
    std::string trainingStatus = "N/A";
    
    if (g_selectedAlgorithm == CCAlgorithm::FDRLCC && g_regionDRL.find(region) != g_regionDRL.end()) {
      auto& drl = g_regionDRL[region];
      auto& perfLogger = PerformanceLogger::GetInstance();
      rewardAvg = perfLogger.GetRewardAverage(region);
      rewardVar = perfLogger.GetRewardVariance(region);
      convergenceInd = perfLogger.CalculateConvergenceIndicator(region);
      divergenceInd = perfLogger.GetCurrentDivergence();
      bufferSize = drl.replayBuffer.Size();
      targetWarmupSize = static_cast<size_t>(std::max(drl.minWarmupSize, drl.targetWarmupSize));
      
      // Determine training status
      if (bufferSize < targetWarmupSize) {
        trainingStatus = "WARMUP";
      } else if (drl.trainingStep > 0) {
        trainingStatus = "ACTIVE";
      } else {
        trainingStatus = "STALLED";
      }
    }
    
    // Store region data for organized display
    RegionDisplayData data;
    data.region = region;
    data.throughputMbps = throughputMbps;
    data.avgDelay = avgDelay;
    data.packetLossRate = packetLossRate;
    data.interestRate = interestRate;
    data.dataRate = dataRate;
    data.drlReward = drlReward;
    data.drlRateFactor = drlRateFactor;
    data.rewardAvg = rewardAvg;
    data.rewardVar = rewardVar;
    data.convergenceInd = convergenceInd;
    data.divergenceInd = divergenceInd;
    data.bufferSize = bufferSize;
    data.targetWarmupSize = targetWarmupSize;
    data.trainingStatus = trainingStatus;
    data.queueUtilization = queueUtilization;
    regionData.push_back(data);
  }
  
  // Display organized by categories
  // 1. NETWORK METRICS
  oss << "║ " << ColorOutput::Info() << "NETWORK METRICS:" << ColorOutput::Reset() << "\n";
  for (const auto& data : regionData) {
    oss << "║   " << data.region << ": "
        << "Throughput=" << std::setprecision(2) << data.throughputMbps << "Mbps | "
        << "Latency=" << std::setprecision(1) << data.avgDelay << "ms | "
        << "PLR=" << std::setprecision(1) << (data.packetLossRate * 100.0) << "% | "
        << "IntRate=" << std::setprecision(1) << data.interestRate << "/s | "
        << "DataRate=" << std::setprecision(1) << data.dataRate << "/s\n";
  }
  
  // 2. DRL METRICS (only for FDRLCC)
  if (g_selectedAlgorithm == CCAlgorithm::FDRLCC && !regionData.empty()) {
    oss << "║\n";
    oss << "║ " << ColorOutput::Info() << "DRL METRICS:" << ColorOutput::Reset() << "\n";
    for (const auto& data : regionData) {
      oss << "║   " << data.region << ": "
          << "Action=" << std::showpos << std::setprecision(2) << (data.drlRateFactor - 1.0) << std::noshowpos
          << " | Reward=" << std::setprecision(3) << data.drlReward;
      if (data.rewardAvg >= 0) {
        oss << " | AvgReward=" << std::setprecision(3) << data.rewardAvg;
      }
      if (data.rewardVar >= 0) {
        oss << " | Variance=" << std::setprecision(3) << data.rewardVar;
      }
      if (data.convergenceInd >= 0) {
        oss << " | Convergence=" << data.convergenceInd;
      }
      oss << "\n";
    }
  }
  
  // 3. FL METRICS (only for FDRLCC)
  if (g_selectedAlgorithm == CCAlgorithm::FDRLCC) {
    oss << "║\n";
    oss << "║ " << ColorOutput::Info() << "FL METRICS:" << ColorOutput::Reset() << "\n";
    oss << "║   Round=" << g_flRound 
        << " | Fairness=" << std::setprecision(3) << g_lastFLFairness
        << " | Divergence=" << std::setprecision(3) << g_lastFLDivergence
        << " | Noise=" << std::setprecision(3) << g_explorationNoise << "\n";
  }
  
  // 4. SYSTEM STATUS (only for FDRLCC)
  if (g_selectedAlgorithm == CCAlgorithm::FDRLCC && !regionData.empty()) {
    oss << "║\n";
    oss << "║ " << ColorOutput::Info() << "SYSTEM STATUS:" << ColorOutput::Reset() << "\n";
    
    // Buffer fill status
    oss << "║   Buffer Fill: ";
    for (size_t i = 0; i < regionData.size(); i++) {
      const auto& data = regionData[i];
      double fillPercent = (data.targetWarmupSize > 0) ? 
          (static_cast<double>(data.bufferSize) / static_cast<double>(data.targetWarmupSize)) * 100.0 : 0.0;
      oss << data.region << "=" << data.bufferSize << "/" << data.targetWarmupSize 
          << " (" << std::setprecision(0) << fillPercent << "%)";
      if (i < regionData.size() - 1) oss << " | ";
    }
    oss << "\n";
    
    // Training status
    oss << "║   Training: ";
    for (size_t i = 0; i < regionData.size(); i++) {
      const auto& data = regionData[i];
      oss << data.region << "=" << data.trainingStatus;
      if (i < regionData.size() - 1) oss << " | ";
    }
    oss << "\n";
    
    // Queue utilization
    oss << "║   Queue Utilization: ";
    for (size_t i = 0; i < regionData.size(); i++) {
      const auto& data = regionData[i];
      oss << data.region << "=" << std::setprecision(1) << data.queueUtilization << "%";
      if (i < regionData.size() - 1) oss << " | ";
    }
    oss << "\n";
  }
  
  oss << ColorOutput::Info() << "╚══════════════════════════════════════════════════════════════════════════════╝" << ColorOutput::Reset() << "\n";
  
  std::string output = oss.str();
  std::cout << output;
  
  // Also write to console log file
  if (g_consoleLog.is_open()) {
    g_consoleLog << output;
    g_consoleLog.flush();
  }
}
void
PrintResults()
{
  std::cout << std::endl;
  std::cout << ColorOutput::Success() << "╔══════════════════════════════════════════════════════════════════════════════╗" << ColorOutput::Reset() << std::endl;
  std::cout << ColorOutput::Success() << "║" << ColorOutput::Reset() << "                          " << ColorOutput::Success() << "FINAL RESULTS" << ColorOutput::Reset() << "                                        " << ColorOutput::Success() << "║" << ColorOutput::Reset() << std::endl;
  std::cout << ColorOutput::Success() << "╠══════════════════════════════════════════════════════════════════════════════╣" << ColorOutput::Reset() << std::endl;
  
  // Auto-detect regions from consumer tracking
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
      int consumersPerRegion = (g_consumers.size() > 3) ? 3 : 1;
      size_t idx = std::distance(g_consumers.begin(), 
                                  std::find(g_consumers.begin(), g_consumers.end(), consumer));
      int regionIdx = idx / consumersPerRegion;
      if (regionIdx < static_cast<int>(regions.size())) {
        regionConsumers[regions[regionIdx]].push_back(consumer);
      }
    }
  }
  
  for (const auto& region : regions) {
    if (regionConsumers.find(region) == regionConsumers.end()) continue;
    
    // REFACTORED: Use MetricEngine snapshot instead of g_regionMetrics
    if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) {
      continue;  // Skip if MetricEngine not available
    }
    // Note: For final output, we may need to calculate cumulative metrics differently
    // For now, use latest snapshot (accessed via MetricEngine when needed)
    auto& consumersInRegion = regionConsumers[region];
    
    // Aggregate statistics across all consumers in this region
    uint64_t interests = 0;
    uint64_t data = 0;
    uint64_t drops = 0;
    double totalDelaySum = 0.0;
    uint64_t totalDelayCount = 0;
    
    for (auto& consumer : consumersInRegion) {
      if (!consumer) continue;
      interests += consumer->GetTotalInterestsSent();
      data += consumer->GetTotalDataReceived();
      drops += consumer->GetTotalTimeouts();
      totalDelaySum += consumer->GetTotalDelaySum();
      totalDelayCount += consumer->GetDelayCount();
    }
    
    double avgDelay = (totalDelayCount > 0) ? 
        (totalDelaySum / static_cast<double>(totalDelayCount)) : 0.0;
    double dropRate = (interests > 0) ? (static_cast<double>(drops) / static_cast<double>(interests)) * 100.0 : 0.0;
    double successRate = (interests > 0) ? (static_cast<double>(data) / static_cast<double>(interests)) * 100.0 : 0.0;
    
    // Calculate final throughput (average over entire simulation)
    // Note: Simulator::Now() is 0 after Destroy(), so use stored simulation time
    double totalTime = (g_simulationTime > 0.1) ? g_simulationTime : std::max(1.0, Simulator::Now().GetSeconds());
    
    // Use data-based calculation (more reliable than totalBytes which may not be tracked correctly)
    double avgThroughputMbps = 0.0;
    if (data > 0 && totalTime > 0.1) {
      // Throughput = (total data packets * packet size in bytes * 8 bits) / (time in seconds * 1e6 for Mbps)
      avgThroughputMbps = (static_cast<double>(data) * 1024.0 * 8.0) / (totalTime * 1e6);
    }
    
    // REFACTORED: Jitter calculation removed (delaySamples not available in MetricSnapshot)
    // TODO: Add jitter tracking to MetricEngine if needed
    double jitter = 0.0;
    // Note: MetricSnapshot doesn't track delay samples for jitter calculation
    // Jitter calculation removed - can be added to MetricEngine if needed
    (void)jitter;  // Suppress unused warning
    
    std::cout << "║ " << region << ": "
              << "Throughput=" << std::setprecision(2) << avgThroughputMbps << "Mbps "
              << "Delay=" << std::setprecision(1) << avgDelay << "ms "
              << "Loss=" << std::setprecision(2) << dropRate << "% "
              << "Success=" << std::fixed << std::setprecision(1) << successRate << "% "
              << "Interests=" << interests << " Data=" << data << std::endl;
  }
  
  std::cout << ColorOutput::Success() << "╚══════════════════════════════════════════════════════════════════════════════╝" << ColorOutput::Reset() << std::endl;
  
  // Print FDRLCC summary (only for FDRLCC)
  if (g_selectedAlgorithm == CCAlgorithm::FDRLCC) {
    std::cout << std::endl;
    std::cout << ColorOutput::Success() << "╔══════════════════════════════════════════════════════════════════════════════╗" << ColorOutput::Reset() << std::endl;
    std::cout << ColorOutput::Success() << "║" << ColorOutput::Reset() << " FDRLCC Summary: FL Rounds=" << ColorOutput::Info() << g_flRound << ColorOutput::Reset() << " | Final Noise=" << std::setprecision(3) << g_explorationNoise << std::endl;
    for (const auto& [region, drl] : g_regionDRL) {
      // FIX: Use current reward (drl.reward) instead of cumulativeReward
      // cumulativeReward may not be updated correctly, use the latest reward value
      double displayReward = drl.reward;
      // If reward is 0, try to get from reward history or performance logger
      if (std::abs(displayReward) < 1e-6 && !drl.rewardHistory.empty()) {
        displayReward = drl.rewardHistory.back();
      }
      // If still 0, try performance logger
      if (std::abs(displayReward) < 1e-6) {
        auto& perfLogger = PerformanceLogger::GetInstance();
        double avgReward = perfLogger.GetRewardAverage(region);
        if (avgReward > -0.5) {  // Valid average reward
          displayReward = avgReward;
        }
      }
      std::cout << "║   " << region << ": Reward=" << std::fixed << std::setprecision(6) << displayReward 
                << " | RateFactor=" << std::setprecision(2) << drl.rateFactor << std::endl;
    }
    std::cout << ColorOutput::Success() << "╚══════════════════════════════════════════════════════════════════════════════╝" << ColorOutput::Reset() << std::endl;
  }
}


} // namespace fdrl
} // namespace ndn
} // namespace ns3

