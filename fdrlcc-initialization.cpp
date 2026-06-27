/**
 * fdrlcc-initialization.cpp
 * 
 * FDRLCC initialization functions
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-initialization.hpp"
#include "fdrlcc-types.hpp"
#include "src_cpp/controller/fdrl-ddpg-networks.hpp"
#include "src_cpp/controller/fdrl-replay-buffer.hpp"
#include "ns3/log.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE("FdrlccInitialization");

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Initialize FDRLCC for all regions with DDPG networks
 */
void
InitializeFDRLCC(const std::vector<std::string>& regions)
{
  // Initialize DRL state for each region
  // PHASE 1: Extended from 5D to 6D state space (added RTT gradient)
  // State: [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio, rttGradientNorm]
  // ABLATION 3: If disableCongestionState is true, state is 3D (removes congestion features)
  const size_t stateDim = GetStateDim();  // Get state dimension from ablation config (3 or 6)
  
  for (const auto& region : regions) {
    RegionDRLState drl;
    drl.regionId = region;
    drl.state.resize(stateDim, 0.0);
    drl.action.resize(1, 1.0);  // [rate_factor] only
    drl.reward = 0.0;
    drl.cumulativeReward = 0.0;
    drl.rateFactor = 1.0;
    
    // Initialize DDPG networks with configurable state dimension
    drl.actor = ActorNetwork(stateDim);
    drl.actor_target = ActorNetwork(stateDim);
    drl.critic = CriticNetwork(stateDim);
    drl.critic_target = CriticNetwork(stateDim);
    
    // Initialize network weights
    drl.actor.Initialize();
    drl.actor_target.Initialize();
    drl.critic.Initialize();
    drl.critic_target.Initialize();
    
    // Copy initial weights to target networks (hard copy for first initialization)
    drl.actor_target = drl.actor;
    drl.critic_target = drl.critic;
    drl.targetNetworksInitialized = true;
    
    // Initialize experience replay buffer (50K capacity)
    drl.replayBuffer = ReplayBuffer(50000);
    
    // Check if weights were loaded for this region (for checkpoint loading)
    // REGION C FIX: Skip pre-trained weights for Region C (IoT/low bandwidth)
    // Pre-trained weights are optimized for high/medium bandwidth (A/B) and don't work well for C
    if (g_weightsLoaded && g_loadedLocalWeights.find(region) != g_loadedLocalWeights.end()) {
      if (region == "C" || region == "N3") {
        // Skip Region C - it has different network conditions (low bandwidth/IoT)
        // Let it learn from scratch for its specific conditions
        NS_LOG_INFO("Skipping pre-trained weights for region " << region 
                    << " (IoT/low bandwidth - will use random initialization for better adaptation)");
      } else {
        // Try to deserialize actor network from loaded weights (for A, B, etc.)
      size_t actorParamCount = drl.actor.GetParameterCount();
      if (g_loadedLocalWeights[region].size() == actorParamCount) {
        drl.actor.Deserialize(g_loadedLocalWeights[region]);
        drl.actor_target.Deserialize(g_loadedLocalWeights[region]);  // Copy to target
        NS_LOG_INFO("Using loaded DDPG actor weights for region " << region);
      } else {
        NS_LOG_WARN("Weight size mismatch for region " << region 
                    << ": expected " << actorParamCount 
                    << ", got " << g_loadedLocalWeights[region].size() << ", using random init");
        }
      }
    }
    
    // Set base frequency based on region type
    if (region == "A" || region == "N1") drl.baseFrequency = 50.0;       // DataCenter
    else if (region == "B" || region == "N2") drl.baseFrequency = 20.0;  // Enterprise
    else if (region == "C" || region == "N3") drl.baseFrequency = 10.0;  // IoT
    else if (region == "D" || region == "N4") drl.baseFrequency = 30.0;  // 5G/LTE
    else if (region == "E" || region == "N5") drl.baseFrequency = 5.0;   // Satellite
    else drl.baseFrequency = 10.0;  // Default for N6, N7, etc.
    
    g_regionDRL[region] = drl;
  }
  
  // Initialize global weights (for FL aggregation - will store actor network parameters)
  size_t actorParamCount = g_regionDRL.begin()->second.actor.GetParameterCount();
  if (g_globalWeights.empty()) {
    g_globalWeights.resize(actorParamCount, 0.0);
  } else if (g_globalWeights.size() != actorParamCount) {
    NS_LOG_WARN("Loaded global weights size mismatch, reinitializing");
    g_globalWeights.resize(actorParamCount, 0.0);
  }
  
  NS_LOG_INFO("FDRLCC initialized with DDPG for " << regions.size() << " regions");
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

