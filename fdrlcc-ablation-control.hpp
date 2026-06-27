/**
 * fdrlcc-ablation-control.hpp
 * 
 * Ablation control framework for FDRLCC
 * Allows disabling individual components to study their contributions
 */

#ifndef FDRLCC_ABLATION_CONTROL_HPP
#define FDRLCC_ABLATION_CONTROL_HPP

// Forward declaration
namespace ns3 {
  class CommandLine;
}

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Ablation control flags
 * These control which components are active in the FDRLCC algorithm
 */
struct AblationFlags {
  bool noFL = false;                    // Disable federated learning (use local training only)
  bool noAdaptiveAggregation = false;   // Use uniform FedAvg instead of performance-weighted
  bool noPolicyBlending = false;        // Disable AFM in FL (fixed uniform mixing when FL on)
  bool noRewardShaping = false;         // Disable PBRS (Potential-Based Reward Shaping)
  bool classicalOnly = false;          // Use only classical CC (AIMD/CUBIC/BIC)
  bool drlOnly = false;                // Use only DRL (no classical fallback)
};

/**
 * Global ablation flags (defined in fdrlcc_unified.cpp)
 */
extern AblationFlags g_ablationFlags;

/**
 * Parse ablation flags from command line
 * Should be called from main() before simulation starts
 * 
 * @param cmd CommandLine object to add arguments to
 */
void ParseAblationFlags(ns3::CommandLine& cmd);

/**
 * Check if federated learning is enabled
 */
bool IsFLEnabled();

/**
 * Check if adaptive aggregation is enabled
 */
bool IsAdaptiveAggregationEnabled();

/**
 * Check if adaptive federated mixing (AFM) is enabled during FL rounds
 */
bool IsAdaptiveMixingEnabled();

/**
 * Check if policy blending is enabled
 */
bool IsPolicyBlendingEnabled();

/**
 * Check if reward shaping is enabled
 */
bool IsRewardShapingEnabled();

/**
 * Check if classical CC should be used
 */
bool ShouldUseClassicalCC();

/**
 * Check if DRL should be used
 */
bool ShouldUseDRL();

/**
 * Print ablation configuration to console
 */
void PrintAblationConfiguration();

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_ABLATION_CONTROL_HPP

