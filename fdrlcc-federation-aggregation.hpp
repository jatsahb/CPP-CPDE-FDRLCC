/**
 * fdrlcc-federation-aggregation.hpp
 * 
 * Federated Learning aggregation functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_FEDERATION_AGGREGATION_HPP
#define FDRLCC_FEDERATION_AGGREGATION_HPP

#include "fdrlcc-types.hpp"
#include "src_cpp/metrics/metric-engine.hpp"  // REFACTORED: Use MetricEngine instead of MetricStore
#include <vector>
#include <string>
#include <map>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Perform Federated Learning aggregation (called every g_flInterval seconds)
 */
void PerformFederatedAggregation();

/**
 * Standard Federated Averaging
 * 
 * @param localWeights Map of region -> local model weights
 * @return Global aggregated weights
 */
std::vector<double> FedAvgAggregate(const std::map<std::string, std::vector<double>>& localWeights);

/**
 * Calculate divergence between local models (L2 distance from global)
 * 
 * @param localWeights Map of region -> local model weights
 * @return Average divergence
 */
double CalculateDivergence(const std::map<std::string, std::vector<double>>& localWeights);

/**
 * Calculate adaptive mixing ratio based on divergence and performance
 * 
 * @param region Region ID
 * @param divergence Current divergence
 * @param regionReward Region's reward
 * @param avgReward Average reward across regions
 * @return Adaptive mixing ratio in [0.5, 0.9]
 */
double CalculateAdaptiveMixingRatio(const std::string& region, 
                                   double divergence, 
                                   double regionReward,
                                   double avgReward);

/**
 * Performance-Weighted Federated Averaging
 * 
 * @param localWeights Map of region -> local model weights
 * @param regionRewards Map of region -> reward values
 * @return Global aggregated weights
 */
std::vector<double> PerformanceWeightedFedAvg(const std::map<std::string, std::vector<double>>& localWeights,
                                             const std::map<std::string, double>& regionRewards);

/**
 * Calculate Jain's Fairness Index for throughput across regions
 * 
 * @param regionThroughputs Map of region -> throughput (Mbps)
 * @return Fairness index in [1/R, 1]
 */
double CalculateJainsFairnessIndex(const std::map<std::string, double>& regionThroughputs);

/**
 * Calculate stability index based on reward variance
 * 
 * @param rewardHistory Vector of recent rewards
 * @param windowSize Window size for calculation
 * @return Stability index in [0, 1]
 */
double CalculateStabilityIndex(const std::vector<double>& rewardHistory, size_t windowSize = 10);

/**
 * Enhanced Multi-Objective Reward Function
 * 
 * @param region Region ID
 * @param snapshot Metric snapshot
 * @param fairnessIndex Fairness index
 * @param stabilityIndex Stability index
 * @return Enhanced reward value
 */
double CalculateEnhancedReward(const std::string& region, 
                              const MetricSnapshot& snapshot,
                              double fairnessIndex,
                              double stabilityIndex);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_FEDERATION_AGGREGATION_HPP
