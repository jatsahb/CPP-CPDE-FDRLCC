/**
 * fdrlcc-training-logic.hpp
 * 
 * DRL training logic for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_TRAINING_LOGIC_HPP
#define FDRLCC_TRAINING_LOGIC_HPP

#include "fdrlcc-types.hpp"
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Train DRL agent independently of FL rounds
 * Called every N control steps when buffer is ready
 * 
 * @param region Region ID
 */
void TrainDRLAgent(const std::string& region);

/**
 * Check training status and print clear status messages
 * Continuously evaluates training signals
 * 
 * @param region Region ID
 * @param simTime Current simulation time
 */
void CheckTrainingStatus(const std::string& region, double simTime);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_TRAINING_LOGIC_HPP

