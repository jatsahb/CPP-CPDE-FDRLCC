/**
 * fdrlcc-ablation-utils.cpp
 * 
 * Utility functions for ablation framework
 * Auto-generates ablation labels and descriptions
 */

#include "fdrlcc-types.hpp"
#include <sstream>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Generate ablation label and description from config
 * Called after CLI flags are parsed
 */
void
GenerateAblationLabel(AblationConfig& config)
{
  std::vector<std::string> activeAblations;
  
  if (config.disableDRL) {
    activeAblations.push_back("no_drl");
  }
  if (config.disableFL) {
    activeAblations.push_back("no_fl");
  }
  if (config.disableCongestionState) {
    activeAblations.push_back("no_congestion_state");
  }
  if (config.disableCongestionReward) {
    activeAblations.push_back("no_congestion_reward");
  }
  if (config.disableTargetNetworks) {
    activeAblations.push_back("no_target_networks");
  }
  
  if (activeAblations.empty()) {
    config.ablationLabel = "baseline";
    config.ablationDescription = "Full FDRLCC (no ablations)";
  } else {
    // Join with "+" for multiple ablations
    std::ostringstream oss;
    for (size_t i = 0; i < activeAblations.size(); ++i) {
      if (i > 0) oss << "+";
      oss << activeAblations[i];
    }
    config.ablationLabel = oss.str();
    
    // Generate human-readable description
    std::ostringstream desc;
    desc << "Ablations: ";
    for (size_t i = 0; i < activeAblations.size(); ++i) {
      if (i > 0) desc << ", ";
      if (activeAblations[i] == "no_drl") desc << "DRL disabled";
      else if (activeAblations[i] == "no_fl") desc << "FL disabled";
      else if (activeAblations[i] == "no_congestion_state") desc << "Congestion state removed";
      else if (activeAblations[i] == "no_congestion_reward") desc << "Congestion reward removed";
      else if (activeAblations[i] == "no_target_networks") desc << "Target networks disabled";
    }
    config.ablationDescription = desc.str();
  }
}

/**
 * Write ablation configuration to JSON metadata file
 */
void
WriteAblationMetadata(const AblationConfig& config, const std::string& resultsDir)
{
  std::string metadataDir = resultsDir + "/logs/metadata";
  
  // Create metadata directory if it doesn't exist
  std::string mkdirCmd = "mkdir -p " + metadataDir;
  system(mkdirCmd.c_str());
  
  std::string metadataPath = metadataDir + "/ablation_config.json";
  std::ofstream metadataFile(metadataPath);
  
  if (!metadataFile.is_open()) {
    return;  // Silently fail if can't write metadata
  }
  
  // Get current timestamp
  std::time_t now = std::time(nullptr);
  std::tm* timeinfo = std::localtime(&now);
  char timestamp[100];
  std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", timeinfo);
  
  // Write JSON
  metadataFile << "{\n";
  metadataFile << "  \"ablation_config\": {\n";
  metadataFile << "    \"disable_drl\": " << (config.disableDRL ? "true" : "false") << ",\n";
  metadataFile << "    \"disable_fl\": " << (config.disableFL ? "true" : "false") << ",\n";
  metadataFile << "    \"disable_congestion_state\": " << (config.disableCongestionState ? "true" : "false") << ",\n";
  metadataFile << "    \"disable_congestion_reward\": " << (config.disableCongestionReward ? "true" : "false") << ",\n";
  metadataFile << "    \"disable_target_networks\": " << (config.disableTargetNetworks ? "true" : "false") << "\n";
  metadataFile << "  },\n";
  metadataFile << "  \"ablation_label\": \"" << config.ablationLabel << "\",\n";
  metadataFile << "  \"ablation_description\": \"" << config.ablationDescription << "\",\n";
  metadataFile << "  \"timestamp\": \"" << timestamp << "\"\n";
  metadataFile << "}\n";
  
  metadataFile.close();
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3
