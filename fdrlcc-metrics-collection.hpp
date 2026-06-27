/**
 * fdrlcc-metrics-collection.hpp
 * 
 * Metrics collection functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_METRICS_COLLECTION_HPP
#define FDRLCC_METRICS_COLLECTION_HPP

#include "fdrlcc-types.hpp"
#include "ns3/node.h"
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Collect network metrics (called every second)
 * Aggregates metrics per region and logs to CSV
 */
void CollectMetrics();

/**
 * Collect AI-specific metrics (called every 2 seconds for DRL/FDRL)
 */
void CollectAIMetrics();

/**
 * Collect CS/PIT/FIB metrics from a router node
 * 
 * @param node Router node
 * @return RouterMetrics structure with CS/PIT/FIB statistics
 */
RouterMetrics CollectRouterMetrics(Ptr<Node> node);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_METRICS_COLLECTION_HPP

