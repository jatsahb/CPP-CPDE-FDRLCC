/**
 * fdrlcc-reward-calculation.cpp
 * 
 * Reward calculation functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-reward-calculation.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-ablation-control.hpp"
#include "fdrlcc-federation-aggregation.hpp"
#include "ns3/log.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <map>

NS_LOG_COMPONENT_DEFINE("FdrlccRewardCalculation");

namespace ns3 {
namespace ndn {
namespace fdrl {

namespace {

double
ComputeInterestSatisfactionRatio(const MetricSnapshot& snapshot)
{
  if (snapshot.totalInterestsSent == 0) {
    return 0.0;
  }
  double isr = static_cast<double>(snapshot.totalDataReceived)
             / static_cast<double>(snapshot.totalInterestsSent);
  return std::min(1.0, std::max(0.0, isr));
}

std::map<std::string, double>
CollectRegionInterestCounts()
{
  std::map<std::string, double> counts;
  if (!g_metricEngine) {
    return counts;
  }
  for (const auto& region : g_topologyInfo.regions) {
    if (g_metricEngine->IsRegionInitialized(region)) {
      counts[region] = static_cast<double>(
          g_metricEngine->GetLatestSnapshot(region).totalInterestsSent);
    }
  }
  return counts;
}

double
ComputeFairnessRewardComponent()
{
  const auto interestCounts = CollectRegionInterestCounts();
  if (interestCounts.size() < 2) {
    return 0.0;
  }
  return CalculateJainsFairnessIndex(interestCounts);
}

double
ComputeInterestGrowthPenalty(const std::string& region,
                             const MetricSnapshot& snapshot,
                             RegionDRLState& drl)
{
  if (drl.prevTotalInterestsSent == 0) {
    return 0.0;
  }
  const double delta = static_cast<double>(snapshot.totalInterestsSent)
                     - static_cast<double>(drl.prevTotalInterestsSent);
  const double prev = static_cast<double>(drl.prevTotalInterestsSent);
  const double normDelta = delta / std::max(1.0, prev);
  if (normDelta <= 0.05) {
    return 0.0;
  }
  const double weight = -0.12;
  return weight * std::min(1.0, normDelta);
}

double
ComputeInterestSharePenalty(const std::string& region, const MetricSnapshot& snapshot)
{
  const auto interestCounts = CollectRegionInterestCounts();
  if (interestCounts.size() < 2) {
    return 0.0;
  }
  double total = 0.0;
  for (const auto& [r, c] : interestCounts) {
    total += c;
  }
  if (total < 1.0) {
    return 0.0;
  }
  const double fairShare = total / static_cast<double>(interestCounts.size());
  const double myShare = static_cast<double>(snapshot.totalInterestsSent);
  if (fairShare < 1.0 || myShare <= fairShare * 1.25) {
    return 0.0;
  }
  const double excess = (myShare / fairShare) - 1.25;
  return -0.15 * std::min(1.0, excess);
}

} // namespace

/**
 * Calculate DRL reward for a region
 * Reward = w_thr * throughput + w_isr * ISR + congestion terms + fairness/stability
 */
double
CalculateDRLReward(const std::string& region, const MetricSnapshot& snapshot)
{
  auto& drl = g_regionDRL[region];

  const double w_thr = 0.8;
  const double w_isr = 0.5;
  const double w_del = -0.4;
  const double w_drop = -0.3;
  const double w_marks = -0.2;
  const double w_fair = 0.15;
  const double w_stab = 0.05;

  double throughputReward = 0.0;
  if (snapshot.throughputMbps > 0.001) {
    const double maxThroughput = 10.0;
    throughputReward = std::log(1.0 + snapshot.throughputMbps) / std::log(1.0 + maxThroughput);
    throughputReward = std::min(1.0, throughputReward);
  }

  const double isrReward = ComputeInterestSatisfactionRatio(snapshot);

  double delayMs = snapshot.avgDelayMs;
  if (delayMs < 0.1) {
    delayMs = (snapshot.throughputMbps > 0.01) ? 2.0 : 1.0;
  }
  const double maxDelay = 500.0;
  double delayPenalty = std::log(1.0 + delayMs) / std::log(1.0 + maxDelay);
  delayPenalty = std::min(1.0, delayPenalty);

  double dropPenalty = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    double lossRate = static_cast<double>(snapshot.totalPacketsDropped)
                    / static_cast<double>(snapshot.totalInterestsSent);
    dropPenalty = lossRate;
  }
  if (dropPenalty > 0.0) {
    const double maxDropRate = 0.1;
    dropPenalty = std::log(1.0 + dropPenalty) / std::log(1.0 + maxDropRate);
    dropPenalty = std::min(1.0, dropPenalty);
  }

  double congestion_level = snapshot.queueOccupancy;
  if (snapshot.avgDelayMs > 0.0 && snapshot.throughputMbps > 0.0) {
    double norm_delay = std::min(1.0, snapshot.avgDelayMs / 200.0);
    double norm_loss = 0.0;
    if (snapshot.totalInterestsSent > 0) {
      double lossRate = static_cast<double>(snapshot.totalPacketsDropped)
                      / static_cast<double>(snapshot.totalInterestsSent);
      norm_loss = std::min(1.0, lossRate * 10.0);
    }
    congestion_level = 0.5 * snapshot.queueOccupancy + 0.3 * norm_delay + 0.2 * norm_loss;
  }

  double congestion_penalty = 0.0;
  if (congestion_level > 0.7) {
    congestion_penalty = -1.0;
  } else if (congestion_level > 0.4) {
    congestion_penalty = -0.5 * congestion_level;
  } else if (congestion_level > 0.2) {
    congestion_penalty = -0.2 * congestion_level;
  }

  double markPenalty = snapshot.congestionMarkRate;

  static std::set<std::string> loggedRegions;
  if (loggedRegions.find(region) == loggedRegions.end()) {
    NS_LOG_INFO("Reward for region " << region
                << " ISR=" << isrReward
                << " marks=" << snapshot.totalCongestionMarks
                << "/" << snapshot.totalDataReceived);
    loggedRegions.insert(region);
  }

  double raw_reward = w_thr * throughputReward + w_isr * isrReward;

  if (!g_ablationConfig.disableCongestionReward) {
    raw_reward += w_del * delayPenalty + w_drop * dropPenalty + w_marks * markPenalty;
    raw_reward += congestion_penalty;
  }

  const double fairnessIndex = ComputeFairnessRewardComponent();
  raw_reward += w_fair * fairnessIndex;

  if (!drl.rewardHistory.empty()) {
    double stabilityIndex = CalculateStabilityIndex(drl.rewardHistory);
    raw_reward += w_stab * stabilityIndex;
  }

  raw_reward += ComputeInterestGrowthPenalty(region, snapshot, drl);
  raw_reward += ComputeInterestSharePenalty(region, snapshot);
  drl.prevTotalInterestsSent = snapshot.totalInterestsSent;

  if (dropPenalty < 0.005 && throughputReward > 0.2 && delayPenalty < 0.1 && isrReward > 0.95) {
    raw_reward += 0.05;
  }

  bool has_prev_state = !drl.prevState.empty() && drl.prevState.size() >= 5;
  bool has_current_state = !drl.state.empty() && drl.state.size() >= 5;

  if (has_prev_state && has_current_state && IsRewardShapingEnabled()) {
    const double lambda = 0.1;
    const double gamma = 0.95;
    double phi_s = -lambda * drl.prevState[3];
    double phi_sp = -lambda * drl.state[3];
    return raw_reward + gamma * phi_sp - phi_s;
  }

  return raw_reward;
}

double
CalculateSimpleReward(const MetricSnapshot& snapshot)
{
  double throughputReward = snapshot.throughputMbps * 10.0;
  double latencyPenalty = snapshot.avgDelayMs / 20.0;
  double lossPenalty = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    double lossRate = static_cast<double>(snapshot.totalPacketsDropped)
                    / static_cast<double>(snapshot.totalInterestsSent);
    lossPenalty = lossRate * 20.0;
  }

  return throughputReward - latencyPenalty - lossPenalty;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3
