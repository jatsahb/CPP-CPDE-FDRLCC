/**
 * fdrlcc-state-extraction.hpp
 * 
 * State extraction functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_STATE_EXTRACTION_HPP
#define FDRLCC_STATE_EXTRACTION_HPP

#include "fdrlcc-types.hpp"
#include <vector>
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Extract 5-dimensional state vector for a region (REFACTORED)
 * State: [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio]
 * 
 * CRITICAL: Uses MetricSnapshot from MetricEngine - single source of truth
 * All normalization happens ONLY here
 * 
 * @param region Region ID
 * @return 5-dimensional normalized state vector [0, 1]^5
 */
std::vector<double> ExtractState(const std::string& region);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_STATE_EXTRACTION_HPP

