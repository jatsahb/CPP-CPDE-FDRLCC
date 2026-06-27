/**
 * fdrlcc-ablation-utils.hpp
 * 
 * Utility functions for ablation framework
 */

#ifndef FDRLCC_ABLATION_UTILS_HPP
#define FDRLCC_ABLATION_UTILS_HPP

#include "fdrlcc-types.hpp"

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Generate ablation label and description from config
 * Called after CLI flags are parsed
 */
void GenerateAblationLabel(AblationConfig& config);

/**
 * Write ablation configuration to JSON metadata file
 * Creates logs/metadata/ablation_config.json
 */
void WriteAblationMetadata(const AblationConfig& config, const std::string& resultsDir);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_ABLATION_UTILS_HPP
