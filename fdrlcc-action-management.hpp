/**
 * fdrlcc-action-management.hpp
 * 
 * Action selection and management for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_ACTION_MANAGEMENT_HPP
#define FDRLCC_ACTION_MANAGEMENT_HPP

#include "fdrlcc-types.hpp"
#include "fdrlcc-research-instrumentation.hpp"  // Research instrumentation
#include <vector>
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Select action using DDPG Actor network with exploration noise
 * 
 * @param region Region ID
 * @param state Current state vector (11-dim)
 * @return Action vector (1-dim: rate_factor in [0.5, 2.0])
 */
std::vector<double> SelectAction(const std::string& region, const std::vector<double>& state);

/**
 * Apply FDRLCC actions to consumers (called every second)
 * Updates consumer interest rates based on DRL agent actions
 * FIX: Now collects transitions and triggers training at every control step
 */
void ApplyFDRLCCActions();

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_ACTION_MANAGEMENT_HPP

