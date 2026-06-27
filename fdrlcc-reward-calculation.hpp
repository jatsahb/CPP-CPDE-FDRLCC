/**
 * fdrlcc-reward-calculation.hpp
 * 
 * Reward calculation functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_REWARD_CALCULATION_HPP
#define FDRLCC_REWARD_CALCULATION_HPP

#include "src_cpp/metrics/metric-engine.hpp"  // REFACTORED: Use MetricEngine instead of MetricStore
#include "fdrlcc-types.hpp"
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Calculate DRL reward for a region
 * Reward = w_thr * throughput_reward + w_del * delay_penalty + w_drop * drop_penalty
 * 
 * Includes Potential-Based Reward Shaping (PBRS) for stabilization
 * Reference: Ng et al. (1999) - Policy invariance theorem guarantees no bias
 * 
 * @param region Region ID
 * @param snapshot Metric snapshot
 * @return Calculated reward value
 */
double CalculateDRLReward(const std::string& region, const MetricSnapshot& snapshot);

/**
 * Calculate simple reward for non-FDRLCC algorithms
 * NOTE: This is NOT an AI/RL reward - just a simple performance score
 * 
 * @param snapshot Metric snapshot
 * @return Simple reward value
 */
double CalculateSimpleReward(const MetricSnapshot& snapshot);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_REWARD_CALCULATION_HPP

