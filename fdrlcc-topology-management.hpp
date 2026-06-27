/**
 * fdrlcc-topology-management.hpp
 * 
 * Topology management functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_TOPOLOGY_MANAGEMENT_HPP
#define FDRLCC_TOPOLOGY_MANAGEMENT_HPP

#include "fdrlcc-types.hpp"
#include "ns3/ndnSIM/helper/ndn-global-routing-helper.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include <vector>
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Parse topology from loaded nodes
 * Auto-detects regions, consumers, producers, and routers
 * 
 * @return TopologyInfo structure with parsed topology
 */
TopologyInfo ParseTopologyFromLoadedNodes();

/**
 * Install producers on producer nodes
 * 
 * @param routingHelper Global routing helper
 * @param topoInfo Topology information
 */
void InstallProducers(GlobalRoutingHelper& routingHelper, const TopologyInfo& topoInfo);

/**
 * Install consumers on consumer nodes
 * 
 * @param topoInfo Topology information
 * @param algorithm Congestion control algorithm
 * @param trafficMultiplier Traffic multiplier for interest rates
 * @return Vector of installed consumer pointers
 */
std::vector<Ptr<FdrlConsumer>> InstallConsumers(const TopologyInfo& topoInfo, 
                                                CCAlgorithm algorithm, 
                                                double trafficMultiplier = 1.0);

/**
 * Generate timestamped directory name for results
 * 
 * @param scenario Scenario number
 * @param simTime Simulation time
 * @param runNumber Run number
 * @param algorithm Algorithm name
 * @return Directory path string
 */
std::string GenerateTimestampedDirectory(uint32_t scenario, 
                                        double simTime, 
                                        uint32_t runNumber, 
                                        const std::string& algorithm);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_TOPOLOGY_MANAGEMENT_HPP

