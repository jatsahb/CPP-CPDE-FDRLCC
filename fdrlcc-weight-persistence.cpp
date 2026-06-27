/**
 * fdrlcc-weight-persistence.cpp
 * 
 * Weight persistence functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-weight-persistence.hpp"
#include "fdrlcc-types.hpp"
#include "ns3/log.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdlib>

NS_LOG_COMPONENT_DEFINE("FdrlccWeightPersistence");

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Save model weights to file (both local and global)
 * Format:
 *   GLOBAL_WEIGHTS: w1 w2 w3 ...
 *   REGION_A: w1 w2 w3 ...
 *   REGION_B: w1 w2 w3 ...
 *   ...
 */
void
SaveModelWeights(const std::string& filepath)
{
  std::ofstream outFile(filepath, std::ios::out | std::ios::trunc);
  if (!outFile.is_open()) {
    NS_LOG_WARN("Could not open weights file for writing: " << filepath);
    return;
  }
  
  // Write header
  outFile << "# FDRLCC Model Weights\n";
  outFile << "# Format: GLOBAL_WEIGHTS or REGION_X followed by space-separated weights\n";
  outFile << "# Generated: " << std::time(nullptr) << "\n\n";
  
  // Save global weights
  outFile << "GLOBAL_WEIGHTS:";
  for (const auto& w : g_globalWeights) {
    outFile << " " << std::fixed << std::setprecision(8) << w;
  }
  outFile << "\n\n";
  
  // Save local actor network weights for each region
  for (const auto& [region, drl] : g_regionDRL) {
    outFile << "REGION_" << region << ":";
    std::vector<double> actorWeights = drl.actor.Serialize();
    for (const auto& w : actorWeights) {
      outFile << " " << std::fixed << std::setprecision(8) << w;
    }
    outFile << "\n";
  }
  
  outFile.close();
  NS_LOG_INFO("Model weights saved to: " << filepath);
}

/**
 * Load model weights from file
 * Returns true if weights were successfully loaded, false otherwise
 */
bool
LoadModelWeights(const std::string& filepath)
{
  std::ifstream inFile(filepath);
  if (!inFile.is_open()) {
    return false;  // File doesn't exist or can't be opened
  }
  
  std::string line;
  bool globalLoaded = false;
  std::map<std::string, std::vector<double>> loadedWeights;
  
  while (std::getline(inFile, line)) {
    // Skip comments and empty lines
    if (line.empty() || line[0] == '#') {
      continue;
    }
    
    // Parse line: "GLOBAL_WEIGHTS: w1 w2 w3 ..." or "REGION_X: w1 w2 w3 ..."
    size_t colonPos = line.find(':');
    if (colonPos == std::string::npos) {
      continue;
    }
    
    std::string prefix = line.substr(0, colonPos);
    std::string weightsStr = line.substr(colonPos + 1);
    
    // Parse weights
    std::vector<double> weights;
    std::istringstream iss(weightsStr);
    double weight;
    while (iss >> weight) {
      weights.push_back(weight);
    }
    
    if (weights.empty()) {
      continue;
    }
    
    if (prefix == "GLOBAL_WEIGHTS") {
      g_globalWeights = weights;
      globalLoaded = true;
      NS_LOG_INFO("Loaded global weights: " << weights.size() << " weights");
    } else if (prefix.find("REGION_") == 0) {
      std::string region = prefix.substr(7);  // Remove "REGION_" prefix
      loadedWeights[region] = weights;
      NS_LOG_INFO("Loaded weights for region " << region << ": " << weights.size() << " weights");
    }
  }
  
  inFile.close();
  
  // Store loaded weights for later application (after InitializeFDRLCC)
  g_loadedLocalWeights = loadedWeights;
  g_weightsLoaded = (globalLoaded || !loadedWeights.empty());
  
  return g_weightsLoaded;
}

/**
 * Save weights after FL round (for checkpointing)
 */
void
SaveWeightsAfterFLRound(const std::string& resultsDir, uint32_t round)
{
  // Create checkpoints directory
  std::string checkpointDir = resultsDir + "/checkpoints";
  std::string cmdStr = "mkdir -p " + checkpointDir;
  system(cmdStr.c_str());
  
  // STEP 5: Only save intermediate checkpoints if flag is enabled
  // Default: Only save weights_latest.txt (saves ~99% disk space)
  if (g_fdrlccConfig.keepIntermediateCheckpoints) {
    // Save checkpoint for this round
    std::string checkpointFile = checkpointDir + "/weights_round_" + std::to_string(round) + ".txt";
    SaveModelWeights(checkpointFile);
  }
  
  // Always save as latest (required for reproducibility)
  std::string latestFile = checkpointDir + "/weights_latest.txt";
  SaveModelWeights(latestFile);
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

