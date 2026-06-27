/**
 * fdrlcc-policy-blending.cpp
 * 
 * Policy blending framework implementation
 */

#include "fdrlcc-policy-blending.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-ablation-control.hpp"
#include "fdrlcc-action-management.hpp"
#include "fdrlcc-metrics-collection.hpp"
#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/simulator.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Global policy blending configuration (defined here, declared extern in header)
PolicyBlendingConfig g_policyBlendingConfig;

void
ParsePolicyBlendingArgs(ns3::CommandLine& cmd)
{
  cmd.AddValue("blendingRatio", 
               "Fixed policy blending ratio (0=classical, 1=DRL)", 
               g_policyBlendingConfig.blendingRatio);
  cmd.AddValue("enableGradualBlending", 
               "Enable gradual transition from classical to DRL", 
               g_policyBlendingConfig.enableGradualBlending);
  cmd.AddValue("blendingStartTime", 
               "Time to start blending (seconds)", 
               g_policyBlendingConfig.blendingStartTime);
  cmd.AddValue("blendingDuration", 
               "Duration of blending transition (seconds)", 
               g_policyBlendingConfig.blendingDuration);
}

double
CalculatePolicyBlendingRatio(double currentTime)
{
  // If policy blending is disabled, return fixed ratio or pure DRL
  if (!IsPolicyBlendingEnabled()) {
    if (g_ablationFlags.classicalOnly) {
      return 0.0;  // Pure classical
    }
    if (g_ablationFlags.drlOnly) {
      return 1.0;  // Pure DRL
    }
    // If blending disabled but neither flag set, use fixed ratio
    return g_policyBlendingConfig.blendingRatio;
  }
  
  // If gradual blending is disabled, use fixed ratio
  if (!g_policyBlendingConfig.enableGradualBlending) {
    return std::clamp(g_policyBlendingConfig.blendingRatio, 0.0, 1.0);
  }
  
  // Gradual transition: 0.0 → 1.0 over blendingDuration
  if (currentTime < g_policyBlendingConfig.blendingStartTime) {
    return 0.0;  // Pure classical before start time
  }
  
  double elapsed = currentTime - g_policyBlendingConfig.blendingStartTime;
  if (elapsed >= g_policyBlendingConfig.blendingDuration) {
    return 1.0;  // Pure DRL after transition complete
  }
  
  // Linear transition: 0.0 → 1.0 over blendingDuration
  double ratio = elapsed / g_policyBlendingConfig.blendingDuration;
  return std::clamp(ratio, 0.0, 1.0);
}

double
GetClassicalCCAction(const std::string& region)
{
  // Get classical CC rate factor based on current algorithm
  // This is a simplified version - actual implementation would use AIMD/CUBIC/BIC logic
  // For now, return a default rate factor (1.0 = no change)
  // TODO: Integrate with actual classical CC algorithms
  return 1.0;
}

void
ApplyBlendedActions(const std::string& region, double currentTime)
{
  // Calculate blending ratio
  double lambda = CalculatePolicyBlendingRatio(currentTime);
  
  // Get classical CC action
  double classicalAction = GetClassicalCCAction(region);
  
  // Get DRL action (from existing action management)
  // Note: This assumes ApplyFDRLCCActions sets rate factors in g_regionDRL
  double drlAction = 1.0;  // Default
  if (g_regionDRL.find(region) != g_regionDRL.end()) {
    drlAction = g_regionDRL[region].rateFactor;
  }
  
  // Blend actions: blended = λ * DRL + (1-λ) * classical
  double blendedAction = lambda * drlAction + (1.0 - lambda) * classicalAction;
  
  // Apply blended action to consumers in this region
  // This will be integrated into ApplyFDRLCCActions
  // For now, we'll modify the rate factor in g_regionDRL
  if (g_regionDRL.find(region) != g_regionDRL.end()) {
    g_regionDRL[region].rateFactor = blendedAction;
  }
}

void
PrintPolicyBlendingConfiguration()
{
  std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║ Policy Blending Configuration                                                ║" << std::endl;
  std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
  std::cout << "║ Gradual Blending: " << std::setw(10) << (g_policyBlendingConfig.enableGradualBlending ? "ENABLED" : "DISABLED") 
            << " | Start Time: " << std::setw(8) << std::fixed << std::setprecision(1) 
            << g_policyBlendingConfig.blendingStartTime << "s ║" << std::endl;
  std::cout << "║ Blending Duration: " << std::setw(6) << std::fixed << std::setprecision(1) 
            << g_policyBlendingConfig.blendingDuration << "s | Fixed Ratio: " << std::setw(8) 
            << std::setprecision(2) << g_policyBlendingConfig.blendingRatio << " ║" << std::endl;
  std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

