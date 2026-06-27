/**
 * fdrlcc-state-extraction.cpp
 * 
 * State extraction functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-state-extraction.hpp"
#include "fdrlcc-types.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/pit.hpp"
#include "ns3/log.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <set>

NS_LOG_COMPONENT_DEFINE("FdrlccStateExtraction");

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Extract state vector for a region
 * ABLATION 3: If disableCongestionState is true, returns 3D state (removes congestion features)
 * Normal: 6D state [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio, rttGradientNorm]
 * Ablated: 3D state [pendingInterestsNorm, throughputNorm, cacheHitRatio]
 * 
 * CRITICAL: Uses MetricSnapshot from MetricEngine - single source of truth
 * All normalization happens ONLY here
 */
std::vector<double>
ExtractState(const std::string& region)
{
  // REFACTORED: Use MetricEngine snapshot (single source of truth)
  // Get snapshot from MetricEngine - all metrics come from this single source
  if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) {
    // Fallback: return zero state if MetricEngine not initialized
    NS_LOG_WARN("MetricEngine not initialized for region " << region << " - returning zero state");
    // Return appropriate size based on ablation
    if (g_ablationConfig.disableCongestionState) {
      return std::vector<double>(3, 0.0);
    }
    return std::vector<double>(6, 0.0);
  }
  
  const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
  
  // ABLATION 3: Remove congestion-related features if disabled
  // Congestion features: queueOccupancy (index 0), avgDelayNorm (index 3), rttGradientNorm (index 5)
  if (g_ablationConfig.disableCongestionState) {
    // Reduced state: [pendingInterestsNorm, throughputNorm, cacheHitRatio]
    std::vector<double> state(3, 0.0);
    state[0] = std::clamp(snapshot.pendingInterestsNorm, 0.0, 1.0);
    state[1] = std::clamp(snapshot.throughputNorm, 0.0, 1.0);
    state[2] = std::clamp(snapshot.cacheHitRatio, 0.0, 1.0);
    return state;
  }
  
  // Normal 6D state: [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio, rttGradientNorm]
  std::vector<double> state(6, 0.0);
  
  // State[0] = queueOccupancy (already normalized 0-1) - CONGESTION FEATURE
  state[0] = std::clamp(snapshot.queueOccupancy, 0.0, 1.0);
  
  // State[1] = pendingInterestsNorm (normalized 0-1)
  // REFACTORED: Use pendingInterestsNorm from MetricEngine snapshot (already normalized)
  state[1] = std::clamp(snapshot.pendingInterestsNorm, 0.0, 1.0);
  
  // State[2] = throughputNorm (normalized 0-1)
  // REFACTORED: Use throughputNorm from MetricEngine snapshot (already normalized)
  state[2] = std::clamp(snapshot.throughputNorm, 0.0, 1.0);
  
  // State[3] = avgDelayNorm (normalized 0-1) - CONGESTION FEATURE
  // REFACTORED: Use avgDelayNorm from MetricEngine snapshot (already normalized)
  state[3] = std::clamp(snapshot.avgDelayNorm, 0.0, 1.0);
  
  // State[4] = cacheHitRatio (already normalized 0-1)
  state[4] = std::clamp(snapshot.cacheHitRatio, 0.0, 1.0);
  
  // State[5] = rttGradientNorm (PHASE 1: Added for improved congestion detection) - CONGESTION FEATURE
  // Normalized RTT gradient [0, 1] - indicates rate of change in delay
  state[5] = std::clamp(snapshot.rttGradientNorm, 0.0, 1.0);
  
  // PHASE 1 VERIFICATION: Log state vector size (debug only, first call per region)
  static std::set<std::string> loggedRegions;
  if (loggedRegions.find(region) == loggedRegions.end()) {
    NS_LOG_INFO("PHASE 1: State vector extracted for region " << region 
                << " - Size: " << state.size() << "D [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio, rttGradientNorm]");
    loggedRegions.insert(region);
  }
  
  return state;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

