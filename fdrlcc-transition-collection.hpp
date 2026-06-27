/**
 * fdrlcc-transition-collection.hpp
 * 
 * Transition collection for experience replay in FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_TRANSITION_COLLECTION_HPP
#define FDRLCC_TRANSITION_COLLECTION_HPP

#include "fdrlcc-types.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp" // For FdrlConsumer complete type
#include "ns3/ptr.h" // For Ptr template
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Collect transition and push to replay buffer (called at every control step)
 * This ensures transitions are collected every 1s, not just every 5s (FL interval)
 * 
 * @param region Region ID
 */
void CollectTransition(const std::string& region);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_TRANSITION_COLLECTION_HPP

