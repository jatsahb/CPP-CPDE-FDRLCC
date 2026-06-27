/**
 * fdrlcc-status-display.cpp
 * 
 * Structured console status display for FDRLCC
 * Shows comprehensive simulation status in formatted boxes
 */

#include "fdrlcc-status-display.hpp"
#include "fdrlcc-console-output.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-reward-calculation.hpp"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <deque>

NS_LOG_COMPONENT_DEFINE("FdrlccStatusDisplay");

namespace ns3 {
namespace ndn {
namespace fdrl {

// Static member initialization
bool StatusDisplay::m_initialized = false;
bool StatusDisplay::m_running = false;
Time StatusDisplay::m_displayInterval = Seconds(10.0);
EventId StatusDisplay::m_displayEvent;
double StatusDisplay::m_lastDisplayTime = 0.0;
std::map<std::string, std::deque<double>> StatusDisplay::m_queueHistory;
std::map<std::string, std::deque<double>> StatusDisplay::m_rttHistory;
std::map<std::string, std::deque<double>> StatusDisplay::m_rewardHistory;
std::map<std::string, double> StatusDisplay::m_lastActorLoss;
std::map<std::string, double> StatusDisplay::m_lastCriticLoss;

void
StatusDisplay::Initialize()
{
  m_initialized = true;
  m_running = false;
  m_lastDisplayTime = 0.0;
  m_queueHistory.clear();
  m_rttHistory.clear();
  m_rewardHistory.clear();
  m_lastActorLoss.clear();
  m_lastCriticLoss.clear();
}

void
StatusDisplay::Start(Time interval)
{
  if (!m_initialized) {
    Initialize();
  }

  m_displayInterval = interval;
  m_running = true;
  m_lastDisplayTime = 0.0;

  // Compact/quiet: progress line is updated on FL rounds only
  if (!ConsoleOutput::IsVerbose()) {
    return;
  }

  m_displayEvent = Simulator::Schedule(m_displayInterval, &StatusDisplay::PeriodicDisplay);
}

void
StatusDisplay::Stop()
{
  m_running = false;
  if (!m_displayEvent.IsExpired()) {
    Simulator::Cancel(m_displayEvent);
  }
}

void
StatusDisplay::PeriodicDisplay()
{
  if (!m_running) return;
  
  double simTime = Simulator::Now().GetSeconds();
  
  // Check if FL round is happening (every 5s, so might coincide with 10s display)
  bool isFLRound = (static_cast<uint32_t>(simTime) % 5 == 0 && simTime >= 5.0);
  
  // PrintStatus() will handle duplicate prevention internally
  PrintStatus(isFLRound);
  
  // Schedule next display
  m_displayEvent = Simulator::Schedule(m_displayInterval, &StatusDisplay::PeriodicDisplay);
}

// Helper function to get arrow symbol with color based on trend
static std::string GetTrendArrow(const std::string& trend, bool isGood = false)
{
  if (trend == "Up") {
    return isGood ? "\033[32m↑\033[0m" : "\033[31m↑\033[0m";  // Green ↑ for good up, Red ↑ for bad up
  } else if (trend == "Down") {
    return isGood ? "\033[32m↓\033[0m" : "\033[31m↓\033[0m";  // Green ↓ for good down, Red ↓ for bad down
  } else {
    return "\033[33m~\033[0m";  // Yellow ~ for stable
  }
}

// Helper function to get status indicator arrow
static std::string GetStatusArrow(double value, double threshold, bool higherIsBetter = false)
{
  if (higherIsBetter) {
    if (value >= threshold * 0.8) return "\033[32m→\033[0m";  // Green → for good
    if (value >= threshold * 0.5) return "\033[33m→\033[0m";  // Yellow → for medium
    return "\033[31m→\033[0m";  // Red → for poor
  } else {
    if (value <= threshold * 0.5) return "\033[32m→\033[0m";  // Green → for good (low is good)
    if (value <= threshold * 0.8) return "\033[33m→\033[0m";  // Yellow → for medium
    return "\033[31m→\033[0m";  // Red → for poor
  }
}

void
StatusDisplay::PrintStatus(bool isFLRound)
{
  if (!m_initialized) return;
  if (!ConsoleOutput::IsVerbose()) {
    return;
  }

  double simTime = Simulator::Now().GetSeconds();
  
  // Prevent duplicate prints: if we printed within last 0.5 seconds, skip
  // This handles cases where FL rounds and periodic display coincide
  if (simTime - m_lastDisplayTime < 0.5) {
    return;
  }
  
  // Update last display time
  m_lastDisplayTime = simTime;
  
  // Get system-wide stats
  size_t totalNodes = 0;
  size_t totalConsumers = 0;
  size_t totalProducers = 0;
  GetSystemStats(totalNodes, totalConsumers, totalProducers);
  
  // Aggregate metrics across all regions
  double avgQueue = 0.0;
  uint64_t totalPIT = 0;
  double avgRTT = 0.0;
  double totalThroughput = 0.0;
  double totalLoss = 0.0;
  double avgCacheHit = 0.0;
  double avgRateScale = 0.0;
  double avgReward = 0.0;
  double avgDivergence = 0.0;
  size_t activeRegions = 0;
  
  size_t totalReplayBuffer = 0;
  double avgActorLoss = 0.0;
  double avgCriticLoss = 0.0;
  size_t totalTrainingSteps = 0;
  
  for (const auto& [region, drl] : g_regionDRL) {
    if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) continue;
    
    const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
    
    avgQueue += snapshot.queueOccupancy;
    totalPIT += snapshot.pendingInterests;
    avgRTT += snapshot.avgDelayMs;
    totalThroughput += snapshot.throughputMbps;
    
    // Calculate loss rate
    double lossRate = 0.0;
    if (snapshot.totalInterestsSent > 0) {
      lossRate = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
    }
    totalLoss += lossRate;
    
    avgCacheHit += snapshot.cacheHitRatio;
    avgRateScale += drl.rateFactor;
    avgReward += drl.reward;
    totalTrainingSteps += drl.trainingStep;
    
    // Get divergence (from last FL round)
    avgDivergence += g_lastFLDivergence;
    
    // Replay buffer size
    totalReplayBuffer += drl.replayBuffer.Size();
    
    // Get training losses
    double actorLoss = 0.0;
    double criticLoss = 0.0;
    GetTrainingLosses(region, actorLoss, criticLoss);
    avgActorLoss += actorLoss;
    avgCriticLoss += criticLoss;
    
    // Update history for trends
    m_queueHistory[region].push_back(snapshot.queueOccupancy);
    m_rttHistory[region].push_back(snapshot.avgDelayMs);
    m_rewardHistory[region].push_back(drl.reward);
    
    // Keep only last 5 values for trend calculation
    if (m_queueHistory[region].size() > 5) {
      m_queueHistory[region].pop_front();
    }
    if (m_rttHistory[region].size() > 5) {
      m_rttHistory[region].pop_front();
    }
    if (m_rewardHistory[region].size() > 5) {
      m_rewardHistory[region].pop_front();
    }
    
    activeRegions++;
  }
  
  if (activeRegions == 0) return;
  
  // Average across regions
  avgQueue /= activeRegions;
  avgRTT /= activeRegions;
  totalLoss /= activeRegions;
  avgCacheHit /= activeRegions;
  avgRateScale /= activeRegions;
  avgReward /= activeRegions;
  avgDivergence /= activeRegions;
  avgActorLoss /= activeRegions;
  avgCriticLoss /= activeRegions;
  
  // Calculate trends
  std::string congestionTrend = "Stable";
  std::string rewardTrend = "Stable";
  std::string throughputTrend = "Stable";
  if (!g_regionDRL.empty()) {
    std::string firstRegion = g_regionDRL.begin()->first;
    congestionTrend = CalculateCongestionTrend(firstRegion);
    rewardTrend = CalculateRewardTrend(firstRegion);
    
    // Calculate throughput trend
    if (m_rttHistory[firstRegion].size() >= 3) {
      auto it = m_rttHistory[firstRegion].end();
      double recent = *(it - 1);
      double older = *(it - 3);
      if (recent < older - 5.0) throughputTrend = "Up";  // Lower RTT = better throughput
      else if (recent > older + 5.0) throughputTrend = "Down";
    }
  }
  
  // Decompose reward
  double throughputReward = 0.0;
  double delayPenalty = 0.0;
  double lossPenalty = 0.0;
  double congPenalty = 0.0;
  if (!g_regionDRL.empty()) {
    std::string firstRegion = g_regionDRL.begin()->first;
    DecomposeReward(firstRegion, throughputReward, delayPenalty, lossPenalty, congPenalty);
  }
  
  // Determine status indicators
  bool congestionAlert = (avgQueue > 0.4 || avgRTT > 50.0 || totalLoss > 0.005);
  bool drlActive = (totalTrainingSteps > 0 && totalReplayBuffer > 50);
  bool flActive = (g_flRound > 0);
  
  // ============================================================================
  // PRINT OPTIMIZED STATUS DISPLAY WITH SECTIONS
  // ============================================================================
  
  std::cout << "\n";
  std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
  
  // Header
  std::string header = "t=" + std::to_string(static_cast<int>(simTime)) + "s | FDRLCC SIMULATION STATUS";
  if (isFLRound) {
    header += " [FL ROUND " + std::to_string(g_flRound) + "]";
  }
  std::cout << "║ " << std::left << std::setw(78) << header << "║" << std::endl;
  std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
  
  // ============================================================================
  // SECTION 1: NETWORK METRICS
  // ============================================================================
  std::cout << "║ \033[1;36m📊 NETWORK METRICS\033[0m" << std::string(60, ' ') << "║" << std::endl;
  std::cout << "╠──────────────────────────────────────────────────────────────────────────────╣" << std::endl;
  
  // Network topology
  std::cout << "║   Topology: " << totalNodes << " nodes | " << totalConsumers << " consumers | " 
            << totalProducers << " producers" << std::string(78 - 45 - std::to_string(totalNodes).length() - std::to_string(totalConsumers).length() - std::to_string(totalProducers).length(), ' ') << "║" << std::endl;
  
  // Queue and PIT
  std::string queueArrow = GetStatusArrow(avgQueue, 0.4, false);  // Lower is better
  std::cout << "║   Queue: " << std::fixed << std::setprecision(2) << (avgQueue * 100.0) << "% " << queueArrow
            << " | PIT: " << totalPIT << " entries" << std::string(78 - 35, ' ') << "║" << std::endl;
  
  // RTT with trend
  std::string rttArrow = GetTrendArrow(congestionTrend, false);  // Lower RTT is better
  std::cout << "║   RTT: " << std::setprecision(1) << avgRTT << " ms " << rttArrow
            << " | Throughput: " << std::setprecision(1) << totalThroughput << " Mbps " << GetTrendArrow(throughputTrend, true)
            << std::string(78 - 50, ' ') << "║" << std::endl;
  
  // Loss and Cache
  std::string lossArrow = GetStatusArrow(totalLoss, 0.01, false);  // Lower is better
  std::cout << "║   Loss: " << std::setprecision(2) << (totalLoss * 100.0) << "% " << lossArrow
            << " | Cache Hit: " << std::setprecision(2) << (avgCacheHit * 100.0) << "%" << std::string(78 - 40, ' ') << "║" << std::endl;
  
  // Congestion alert
  if (congestionAlert) {
    std::cout << "║   \033[1;31m⚠ ALERT: Congestion detected\033[0m" << std::string(78 - 35, ' ') << "║" << std::endl;
  }
  
  std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
  
  // ============================================================================
  // SECTION 2: DRL WORKING
  // ============================================================================
  std::cout << "║ \033[1;33m🤖 DRL WORKING\033[0m" << std::string(65, ' ') << "║" << std::endl;
  std::cout << "╠──────────────────────────────────────────────────────────────────────────────╣" << std::endl;
  
  // DRL Status
  std::string drlStatusArrow = drlActive ? "\033[32m→\033[0m" : "\033[31m→\033[0m";
  std::string drlStatus = drlActive ? "ACTIVE" : "INACTIVE";
  std::cout << "║   Status: " << drlStatus << " " << drlStatusArrow
            << " | Training Steps: " << totalTrainingSteps << std::string(78 - 45, ' ') << "║" << std::endl;
  
  // Rate scaling factor
  std::string rateArrow = GetStatusArrow(avgRateScale, 1.5, true);  // Higher is better (up to reasonable limit)
  std::cout << "║   Rate Factor: " << std::fixed << std::setprecision(2) << avgRateScale << "x " << rateArrow
            << " | Replay Buffer: " << totalReplayBuffer << " transitions" << std::string(78 - 50, ' ') << "║" << std::endl;
  
  // Reward with trend
  std::string rewardArrow = GetTrendArrow(rewardTrend, true);  // Higher reward is better
  std::cout << "║   Reward: " << std::setprecision(3) << avgReward << " " << rewardArrow
            << " | Components: Thpt=" << std::showpos << std::setprecision(2) << throughputReward
            << " Delay=" << delayPenalty << " Loss=" << lossPenalty << std::noshowpos << std::string(78 - 70, ' ') << "║" << std::endl;
  
  // Training losses
  std::string actorArrow = (avgActorLoss < 0.5) ? "\033[32m→\033[0m" : "\033[33m→\033[0m";
  std::string criticArrow = (avgCriticLoss < 1.0) ? "\033[32m→\033[0m" : "\033[33m→\033[0m";
  std::cout << "║   Losses: Actor=" << std::setprecision(3) << avgActorLoss << " " << actorArrow
            << " Critic=" << avgCriticLoss << " " << criticArrow << std::string(78 - 45, ' ') << "║" << std::endl;
  
  std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
  
  // ============================================================================
  // SECTION 3: FL WORKING
  // ============================================================================
  std::cout << "║ \033[1;35m🌐 FL WORKING\033[0m" << std::string(66, ' ') << "║" << std::endl;
  std::cout << "╠──────────────────────────────────────────────────────────────────────────────╣" << std::endl;
  
  // FL Status
  std::string flStatusArrow = flActive ? "\033[32m→\033[0m" : "\033[33m→\033[0m";
  std::string flStatus = flActive ? "ACTIVE" : "INITIALIZING";
  std::cout << "║   Status: " << flStatus << " " << flStatusArrow
            << " | Round: " << g_flRound << " | Active Regions: " << activeRegions << std::string(78 - 50, ' ') << "║" << std::endl;
  
  // Divergence
  std::string divArrow = (avgDivergence < 0.1) ? "\033[32m→\033[0m" : (avgDivergence < 1.0) ? "\033[33m→\033[0m" : "\033[31m→\033[0m";
  std::cout << "║   Divergence: " << std::setprecision(4) << avgDivergence << " " << divArrow
            << " | Fairness: " << std::setprecision(3) << g_lastFLFairness << std::string(78 - 40, ' ') << "║" << std::endl;
  
  // FL aggregation info (if FL round)
  if (isFLRound) {
    std::cout << "║   \033[1;32m✓ Aggregation: Actor weights aggregated and redistributed\033[0m" << std::string(78 - 60, ' ') << "║" << std::endl;
  }
  
  // Exploration noise
  std::string noiseArrow = (g_explorationNoise > 0.1) ? "\033[33m→\033[0m" : "\033[32m→\033[0m";
  std::cout << "║   Exploration Noise: " << std::setprecision(3) << g_explorationNoise << " " << noiseArrow << std::string(78 - 35, ' ') << "║" << std::endl;
  
  std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
  
  m_lastDisplayTime = simTime;
}

void
StatusDisplay::GetSystemStats(size_t& totalNodes, size_t& totalConsumers, size_t& totalProducers)
{
  // Count consumers and producers
  totalConsumers = g_consumers.size();
  
  // Count producers from topology
  totalProducers = 0;
  for (const auto& [region, nodeNames] : g_topologyInfo.producers) {
    totalProducers += nodeNames.size();
  }
  
  // Count total nodes from topology
  totalNodes = 0;
  for (const auto& [region, nodeNames] : g_topologyInfo.consumers) {
    totalNodes += nodeNames.size();
  }
  for (const auto& [region, nodeNames] : g_topologyInfo.producers) {
    totalNodes += nodeNames.size();
  }
  for (const auto& [region, nodeNames] : g_topologyInfo.routers) {
    totalNodes += nodeNames.size();
  }
  
  // If topology not available, estimate from consumers
  if (totalNodes == 0) {
    totalNodes = totalConsumers + totalProducers + 10;  // Estimate: consumers + producers + routers
  }
}

std::string
StatusDisplay::CalculateCongestionTrend(const std::string& region)
{
  if (m_queueHistory[region].size() < 3 || m_rttHistory[region].size() < 3) {
    return "Stable";
  }
  
  // Calculate trend from last 3 values
  double queueSlope = 0.0;
  double rttSlope = 0.0;
  
  if (m_queueHistory[region].size() >= 3) {
    auto it = m_queueHistory[region].end();
    double recent = *(it - 1);
    double older = *(it - 3);
    queueSlope = recent - older;
  }
  
  if (m_rttHistory[region].size() >= 3) {
    auto it = m_rttHistory[region].end();
    double recent = *(it - 1);
    double older = *(it - 3);
    rttSlope = recent - older;
  }
  
  // Combine queue and RTT trends
  double combinedSlope = (queueSlope * 0.5) + (rttSlope / 100.0);  // Normalize RTT slope
  
  if (combinedSlope > 0.05) {
    return "Up";
  } else if (combinedSlope < -0.05) {
    return "Down";
  } else {
    return "Stable";
  }
}

std::string
StatusDisplay::CalculateRewardTrend(const std::string& region)
{
  if (m_rewardHistory[region].size() < 3) {
    return "Stable";
  }
  
  // Calculate trend from last 3 values
  auto it = m_rewardHistory[region].end();
  double recent = *(it - 1);
  double older = *(it - 3);
  double slope = recent - older;
  
  if (slope > 0.01) {
    return "Up";
  } else if (slope < -0.01) {
    return "Down";
  } else {
    return "Stable";
  }
}

void
StatusDisplay::DecomposeReward(const std::string& region,
                               double& throughputReward,
                               double& delayPenalty,
                               double& lossPenalty,
                               double& congPenalty)
{
  if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) {
    throughputReward = 0.0;
    delayPenalty = 0.0;
    lossPenalty = 0.0;
    congPenalty = 0.0;
    return;
  }
  
  const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
  
  // Calculate components (same logic as CalculateDRLReward but return components)
  const double w_thr = 0.8;
  const double w_del = -0.4;
  const double w_drop = -0.3;
  
  // Throughput reward
  throughputReward = 0.0;
  if (snapshot.throughputMbps > 0.001) {
    const double maxThroughput = 10.0;
    throughputReward = std::log(1.0 + snapshot.throughputMbps) / std::log(1.0 + maxThroughput);
    throughputReward = std::min(1.0, throughputReward);
  }
  throughputReward *= w_thr;
  
  // Delay penalty
  double delayMs = snapshot.avgDelayMs;
  if (delayMs < 0.1) {
    delayMs = (snapshot.throughputMbps > 0.01) ? 2.0 : 1.0;
  }
  const double maxDelay = 500.0;
  delayPenalty = std::log(1.0 + delayMs) / std::log(1.0 + maxDelay);
  delayPenalty = std::min(1.0, delayPenalty);
  delayPenalty *= w_del;
  
  // Loss penalty
  lossPenalty = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    double lossRate = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
    if (lossRate > 0.0) {
      const double maxDropRate = 0.1;
      lossPenalty = std::log(1.0 + lossRate) / std::log(1.0 + maxDropRate);
      lossPenalty = std::min(1.0, lossPenalty);
    }
  }
  lossPenalty *= w_drop;
  
  // Congestion penalty
  congPenalty = 0.0;
  double congestion_level = snapshot.queueOccupancy;
  if (snapshot.avgDelayMs > 0.0 && snapshot.throughputMbps > 0.0) {
    double norm_delay = std::min(1.0, snapshot.avgDelayMs / 200.0);
    double norm_loss = 0.0;
    if (snapshot.totalInterestsSent > 0) {
      double lossRate = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
      norm_loss = std::min(1.0, lossRate * 10.0);
    }
    congestion_level = 0.5 * snapshot.queueOccupancy + 0.3 * norm_delay + 0.2 * norm_loss;
  }
  
  if (congestion_level > 0.7) {
    congPenalty = -1.0;
  } else if (congestion_level > 0.4) {
    congPenalty = -0.5 * congestion_level;
  } else if (congestion_level > 0.2) {
    congPenalty = -0.2 * congestion_level;
  }
}

void
StatusDisplay::GetTrainingLosses(const std::string& region,
                                double& actorLoss,
                                double& criticLoss)
{
  actorLoss = 0.0;
  criticLoss = 0.0;
  
  // Get from global tracking variables (updated in training logic)
  // These are defined in fdrlcc_unified.cpp
  if (g_regionActorLoss.find(region) != g_regionActorLoss.end()) {
    actorLoss = g_regionActorLoss[region];
    m_lastActorLoss[region] = actorLoss;
  } else if (m_lastActorLoss.find(region) != m_lastActorLoss.end()) {
    actorLoss = m_lastActorLoss[region];
  }
  
  if (g_regionCriticLoss.find(region) != g_regionCriticLoss.end()) {
    criticLoss = g_regionCriticLoss[region];
    m_lastCriticLoss[region] = criticLoss;
  } else if (m_lastCriticLoss.find(region) != m_lastCriticLoss.end()) {
    criticLoss = m_lastCriticLoss[region];
  }
  
  // Fallback: estimate from total loss history if no direct values
  if (actorLoss == 0.0 && criticLoss == 0.0) {
    if (g_trainingStatus.lossHistory.find(region) != g_trainingStatus.lossHistory.end()) {
      const auto& lossHistory = g_trainingStatus.lossHistory[region];
      if (!lossHistory.empty()) {
        double totalLoss = lossHistory.back();
        criticLoss = totalLoss * 0.7;
        actorLoss = totalLoss * 0.3;
        m_lastActorLoss[region] = actorLoss;
        m_lastCriticLoss[region] = criticLoss;
      }
    }
  }
}

void
StatusDisplay::Cleanup()
{
  Stop();
  m_initialized = false;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

