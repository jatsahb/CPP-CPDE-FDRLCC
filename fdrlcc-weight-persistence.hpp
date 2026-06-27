/**
 * fdrlcc-weight-persistence.hpp
 * 
 * Weight persistence functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_WEIGHT_PERSISTENCE_HPP
#define FDRLCC_WEIGHT_PERSISTENCE_HPP

#include "fdrlcc-types.hpp"
#include <string>
#include <cstdint>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Save model weights to file (both local and global)
 * Format:
 *   GLOBAL_WEIGHTS: w1 w2 w3 ...
 *   REGION_A: w1 w2 w3 ...
 *   REGION_B: w1 w2 w3 ...
 *   ...
 * 
 * @param filepath Path to save weights file
 */
void SaveModelWeights(const std::string& filepath);

/**
 * Load model weights from file
 * Returns true if weights were successfully loaded, false otherwise
 * 
 * @param filepath Path to load weights file
 * @return true if weights loaded successfully
 */
bool LoadModelWeights(const std::string& filepath);

/**
 * Save weights after FL round (for checkpointing)
 * 
 * @param resultsDir Results directory path
 * @param round FL round number
 */
void SaveWeightsAfterFLRound(const std::string& resultsDir, uint32_t round);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_WEIGHT_PERSISTENCE_HPP

