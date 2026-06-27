/**
 * fdrlcc-policy-blending.hpp
 * 
 * Policy blending framework for FDRLCC
 * Implements gradual transition from classical CC to DRL policy
 */

#ifndef FDRLCC_POLICY_BLENDING_HPP
#define FDRLCC_POLICY_BLENDING_HPP

#include <string>

// Forward declaration
namespace ns3 {
  class CommandLine;
}

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Policy blending configuration
 */
struct PolicyBlendingConfig {
  double blendingRatio = 0.0;          // Fixed blending ratio (0.0 = classical, 1.0 = DRL)
  bool enableGradualBlending = true;    // Enable gradual transition
  double blendingStartTime = 10.0;     // Time to start blending (seconds)
  double blendingDuration = 50.0;      // Duration of blending transition (seconds)
};

/**
 * Global policy blending configuration (defined in fdrlcc_unified.cpp)
 */
extern PolicyBlendingConfig g_policyBlendingConfig;

/**
 * Parse policy blending arguments from command line
 * 
 * @param cmd CommandLine object to add arguments to
 */
void ParsePolicyBlendingArgs(ns3::CommandLine& cmd);

/**
 * Calculate current policy blending ratio based on simulation time
 * Returns value in [0.0, 1.0] where:
 *   - 0.0 = pure classical CC
 *   - 1.0 = pure DRL
 * 
 * @param currentTime Current simulation time in seconds
 * @return Blending ratio (0.0 to 1.0)
 */
double CalculatePolicyBlendingRatio(double currentTime);

/**
 * Get classical CC action for a region
 * 
 * @param region Region ID
 * @return Classical CC rate factor
 */
double GetClassicalCCAction(const std::string& region);

/**
 * Apply blended actions (classical + DRL) to consumers in a region
 * 
 * @param region Region ID
 * @param currentTime Current simulation time
 */
void ApplyBlendedActions(const std::string& region, double currentTime);

/**
 * Print policy blending configuration
 */
void PrintPolicyBlendingConfiguration();

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_POLICY_BLENDING_HPP

