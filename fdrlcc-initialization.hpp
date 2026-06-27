/**
 * fdrlcc-initialization.hpp
 * 
 * FDRLCC initialization functions
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_INITIALIZATION_HPP
#define FDRLCC_INITIALIZATION_HPP

#include "fdrlcc-types.hpp"
#include <vector>
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Initialize FDRLCC for all regions with DDPG networks
 * 
 * @param regions Vector of region IDs to initialize
 */
void InitializeFDRLCC(const std::vector<std::string>& regions);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_INITIALIZATION_HPP

