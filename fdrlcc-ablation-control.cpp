/**
 * fdrlcc-ablation-control.cpp
 * 
 * Ablation control framework implementation
 */

#include "fdrlcc-ablation-control.hpp"
#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include <iostream>
#include <iomanip>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Global ablation flags (defined here, declared extern in header)
AblationFlags g_ablationFlags;

void
ParseAblationFlags(ns3::CommandLine& cmd)
{
  cmd.AddValue("noFL", 
               "Disable federated learning (use local training only)", 
               g_ablationFlags.noFL);
  cmd.AddValue("noAdaptiveAggregation", 
               "Use uniform FedAvg instead of performance-weighted aggregation", 
               g_ablationFlags.noAdaptiveAggregation);
  cmd.AddValue("noPolicyBlending", 
               "Disable adaptive mixing (use pure DRL)", 
               g_ablationFlags.noPolicyBlending);
  cmd.AddValue("noRewardShaping", 
               "Disable PBRS (Potential-Based Reward Shaping)", 
               g_ablationFlags.noRewardShaping);
  cmd.AddValue("classicalOnly", 
               "Use only classical CC (AIMD/CUBIC/BIC)", 
               g_ablationFlags.classicalOnly);
  cmd.AddValue("drlOnly", 
               "Use only DRL (no classical fallback)", 
               g_ablationFlags.drlOnly);
}

bool
IsFLEnabled()
{
  return !g_ablationFlags.noFL && !g_ablationFlags.classicalOnly;
}

bool
IsAdaptiveAggregationEnabled()
{
  return !g_ablationFlags.noAdaptiveAggregation && IsFLEnabled();
}

bool
IsAdaptiveMixingEnabled()
{
  return IsFLEnabled()
      && !g_ablationFlags.noAdaptiveAggregation
      && !g_ablationFlags.noPolicyBlending;
}

bool
IsPolicyBlendingEnabled()
{
  return !g_ablationFlags.noPolicyBlending && !g_ablationFlags.classicalOnly && !g_ablationFlags.drlOnly;
}

bool
IsRewardShapingEnabled()
{
  return !g_ablationFlags.noRewardShaping && !g_ablationFlags.classicalOnly;
}

bool
ShouldUseClassicalCC()
{
  return g_ablationFlags.classicalOnly || (!g_ablationFlags.drlOnly && IsPolicyBlendingEnabled());
}

bool
ShouldUseDRL()
{
  return !g_ablationFlags.classicalOnly && (g_ablationFlags.drlOnly || !g_ablationFlags.noPolicyBlending);
}

void
PrintAblationConfiguration()
{
  std::cout << "╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║ Ablation Configuration                                                       ║" << std::endl;
  std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
  std::cout << "║ FL Enabled: " << std::setw(10) << (IsFLEnabled() ? "YES" : "NO") 
            << " | Adaptive Aggregation: " << std::setw(10) << (IsAdaptiveAggregationEnabled() ? "YES" : "NO") << " ║" << std::endl;
  std::cout << "║ Adaptive Mixing: " << std::setw(7) << (IsAdaptiveMixingEnabled() ? "YES" : "NO")
            << " | Reward Shaping: " << std::setw(10) << (IsRewardShapingEnabled() ? "YES" : "NO") << " ║" << std::endl;
  std::cout << "║ Policy Blending: " << std::setw(8) << (IsPolicyBlendingEnabled() ? "YES" : "NO") 
            << " | DRL: " << std::setw(18) << (ShouldUseDRL() ? "YES" : "NO") << " ║" << std::endl;
  std::cout << "║ Classical CC: " << std::setw(10) << (ShouldUseClassicalCC() ? "YES" : "NO") << " ║" << std::endl;
  std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

