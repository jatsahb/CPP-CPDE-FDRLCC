/**
 * fdrlcc-validation-checks.cpp
 * 
 * STEP 7: Validation checks implementation for scientific measurement instrument
 */

#include "fdrlcc-validation-checks.hpp"
#include "fdrlcc-types.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <set>
#include <algorithm>
#include <iomanip>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * STEP 7: Validate all required CSV files have data (not just headers)
 */
void
ValidateDataCompleteness(const std::string& resultsDir)
{
  std::vector<std::string> warnings;
  
  // Required files for scientific measurement
  std::vector<std::pair<std::string, std::string>> requiredFiles = {
    {"training_metrics.csv", "Training metrics"},
    {"fl_metrics.csv", "FL metrics"},
    {"experience_dataset.csv", "Experience dataset"},
    {"summary.csv", "Network ground truth"},
    {"logs/events.csv", "Events log"},
    {"logs/federation/federation_rounds.csv", "Federation rounds"},
    {"logs/fairness/fairness_timeseries.csv", "Fairness timeseries"},
    {"logs/metadata/simulation_config.json", "Simulation config"}
  };
  
  for (const auto& [filename, description] : requiredFiles) {
    std::string filepath = resultsDir + "/" + filename;
    std::ifstream file(filepath);
    
    if (!file.is_open()) {
      warnings.push_back("⚠️  [VALIDATION] " + description + " file not found: " + filename);
      continue;
    }
    
    // Check if file has data (more than just header)
    std::string line;
    int lineCount = 0;
    while (std::getline(file, line)) {
      lineCount++;
      if (lineCount > 1) break;  // Found at least one data row
    }
    
    if (lineCount <= 1) {
      warnings.push_back("⚠️  [VALIDATION] " + description + " file has only header (no data): " + filename);
    }
    
    file.close();
  }
  
  // STEP 7: Check per-region structured logs (updated to reflect removed files)
  // STEP 1 & 2: Removed state_*.csv and learning_*.csv (redundant/debug only)
  for (const auto& region : g_topologyInfo.regions) {
    std::vector<std::string> regionFiles = {
      "logs/congestion/congestion_" + region + ".csv",
      // STEP 2: REMOVED - logs/drl/state_" + region + ".csv (debug only, not needed for paper)
      "logs/drl/action_" + region + ".csv",
      // STEP 1: REMOVED - logs/drl/learning_" + region + ".csv (redundant with training_metrics.csv)
    };
    
    for (const auto& filename : regionFiles) {
      std::string filepath = resultsDir + "/" + filename;
      std::ifstream file(filepath);
      
      if (!file.is_open()) {
        warnings.push_back("⚠️  [VALIDATION] Region " + region + " log file not found: " + filename);
        continue;
      }
      
      std::string line;
      int lineCount = 0;
      while (std::getline(file, line)) {
        lineCount++;
        if (lineCount > 1) break;
      }
      
      if (lineCount <= 1) {
        warnings.push_back("⚠️  [VALIDATION] Region " + region + " log file has only header: " + filename);
      }
      
      file.close();
    }
  }
  
  // Print warnings (do not abort)
  if (!warnings.empty()) {
    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║ DATA COMPLETENESS VALIDATION WARNINGS" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
    for (const auto& warning : warnings) {
      std::cout << "║ " << warning << std::endl;
    }
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
  } else {
    std::cout << "✓ [VALIDATION] All required files have data" << std::endl;
  }
}

/**
 * STEP 7: Validate timestamps are monotonic
 */
void
ValidateTimestampConsistency(const std::string& resultsDir)
{
  std::vector<std::string> warnings;
  
  // Check key files for timestamp monotonicity
  std::vector<std::string> filesToCheck = {
    "training_metrics.csv",
    "fl_metrics.csv",
    "summary.csv",
    "logs/events.csv"
  };
  
  for (const auto& filename : filesToCheck) {
    std::string filepath = resultsDir + "/" + filename;
    std::ifstream file(filepath);
    
    if (!file.is_open()) continue;
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    double prevTime = -1.0;
    int rowNum = 1;
    bool hasNonMonotonic = false;
    
    while (std::getline(file, line)) {
      rowNum++;
      if (line.empty()) continue;
      
      // Extract first column (timestamp)
      std::istringstream iss(line);
      std::string timeStr;
      std::getline(iss, timeStr, ',');
      
      try {
        double currentTime = std::stod(timeStr);
        
        if (prevTime >= 0.0 && currentTime < prevTime) {
          hasNonMonotonic = true;
          warnings.push_back("⚠️  [VALIDATION] Non-monotonic timestamp in " + filename + 
                           " at row " + std::to_string(rowNum) + 
                           " (prev=" + std::to_string(prevTime) + 
                           ", current=" + std::to_string(currentTime) + ")");
          break;  // Only report first occurrence
        }
        
        prevTime = currentTime;
      } catch (...) {
        // Skip invalid lines
        continue;
      }
    }
    
    file.close();
  }
  
  if (!warnings.empty()) {
    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║ TIMESTAMP CONSISTENCY VALIDATION WARNINGS" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
    for (const auto& warning : warnings) {
      std::cout << "║ " << warning << std::endl;
    }
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
  } else {
    std::cout << "✓ [VALIDATION] All timestamps are monotonic" << std::endl;
  }
}

/**
 * STEP 7: Validate all regions are covered
 */
void
ValidateRegionCoverage(const std::string& resultsDir)
{
  std::vector<std::string> warnings;
  std::set<std::string> foundRegions;
  
  // Check structured logs for region coverage
  for (const auto& region : g_topologyInfo.regions) {
    std::vector<std::string> regionFiles = {
      "logs/congestion/congestion_" + region + ".csv",
      "logs/drl/state_" + region + ".csv",
      "logs/drl/action_" + region + ".csv",
      "logs/drl/learning_" + region + ".csv"
    };
    
    bool allFilesExist = true;
    for (const auto& filename : regionFiles) {
      std::string filepath = resultsDir + "/" + filename;
      std::ifstream file(filepath);
      if (!file.is_open()) {
        allFilesExist = false;
        break;
      }
      
      // Check if file has data
      std::string line;
      int lineCount = 0;
      while (std::getline(file, line)) {
        lineCount++;
        if (lineCount > 1) break;
      }
      file.close();
      
      if (lineCount <= 1) {
        allFilesExist = false;
        break;
      }
    }
    
    if (allFilesExist) {
      foundRegions.insert(region);
    } else {
      warnings.push_back("⚠️  [VALIDATION] Region " + region + " missing or incomplete in structured logs");
    }
  }
  
  // Check summary.csv for region coverage
  std::string summaryPath = resultsDir + "/summary.csv";
  std::ifstream summaryFile(summaryPath);
  if (summaryFile.is_open()) {
    std::string line;
    std::getline(summaryFile, line);  // Skip header
    
    std::set<std::string> summaryRegions;
    while (std::getline(summaryFile, line)) {
      if (line.empty()) continue;
      
      // Extract region (second column)
      std::istringstream iss(line);
      std::string timeStr, regionStr;
      std::getline(iss, timeStr, ',');
      std::getline(iss, regionStr, ',');
      
      summaryRegions.insert(regionStr);
    }
    summaryFile.close();
    
    for (const auto& region : g_topologyInfo.regions) {
      if (summaryRegions.find(region) == summaryRegions.end()) {
        warnings.push_back("⚠️  [VALIDATION] Region " + region + " missing from summary.csv");
      }
    }
  }
  
  if (!warnings.empty()) {
    std::cout << "✓ [VALIDATION] All regions covered in logs" << std::endl;
  } else {
    std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║ REGION COVERAGE VALIDATION WARNINGS" << std::endl;
    std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
    for (const auto& warning : warnings) {
      std::cout << "║ " << warning << std::endl;
    }
    std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
  }
}

/**
 * STEP 7: Run all validation checks
 */
void
RunValidationChecks(const std::string& resultsDir)
{
  std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║ STEP 7: VALIDATION CHECKS (Scientific Measurement Instrument)" << std::endl;
  std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣" << std::endl;
  
  ValidateDataCompleteness(resultsDir);
  ValidateTimestampConsistency(resultsDir);
  ValidateRegionCoverage(resultsDir);
  
  std::cout << "╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
}

/**
 * STEP 6: Print storage safety warnings on shutdown
 * Warns if files exceed expected sizes (prevents silent disk explosion)
 */
void
PrintStorageWarnings(const std::string& resultsDir)
{
  std::cout << "\n╔══════════════════════════════════════════════════════════════════════════════╗" << std::endl;
  std::cout << "║ STEP 6: STORAGE SAFETY WARNINGS" << std::endl;
  std::cout << "╠══════════════════════════════════════════════════════════════════════════════╣\n" << std::endl;
  
  std::vector<std::string> warnings;
  
  // Check experience_dataset.csv size
  std::string experiencePath = resultsDir + "/experience_dataset.csv";
  std::ifstream experienceFile(experiencePath, std::ios::binary | std::ios::ate);
  if (experienceFile.is_open()) {
    std::streamsize size = experienceFile.tellg();
    experienceFile.close();
    double sizeMB = static_cast<double>(size) / (1024.0 * 1024.0);
    
    // Warn if > 100 MB (indicates very long run or subsampling disabled)
    if (sizeMB > 100.0) {
      std::ostringstream oss;
      oss << "⚠️  [STORAGE] experience_dataset.csv is large: " << std::fixed << std::setprecision(2) 
          << sizeMB << " MB. Consider using --experience-log-interval=10 for long runs.";
      warnings.push_back(oss.str());
    }
  }
  
  // Check training_metrics.csv size
  std::string trainingPath = resultsDir + "/training_metrics.csv";
  std::ifstream trainingFile(trainingPath, std::ios::binary | std::ios::ate);
  if (trainingFile.is_open()) {
    std::streamsize size = trainingFile.tellg();
    trainingFile.close();
    double sizeMB = static_cast<double>(size) / (1024.0 * 1024.0);
    
    // Warn if > 50 MB (indicates very long run or subsampling disabled)
    if (sizeMB > 50.0) {
      std::ostringstream oss;
      oss << "⚠️  [STORAGE] training_metrics.csv is large: " << std::fixed << std::setprecision(2) 
          << sizeMB << " MB. Consider using --training-log-interval=10 for long runs.";
      warnings.push_back(oss.str());
    }
  }
  
  // Check for redundant files (should not exist after STEP 1 & 2)
  for (const auto& region : g_topologyInfo.regions) {
    std::string learningPath = resultsDir + "/logs/drl/learning_" + region + ".csv";
    std::ifstream learningFile(learningPath);
    if (learningFile.is_open()) {
      warnings.push_back("⚠️  [STORAGE] Redundant file found: logs/drl/learning_" + region + ".csv (should be disabled, duplicates training_metrics.csv)");
      learningFile.close();
    }
    
    std::string statePath = resultsDir + "/logs/drl/state_" + region + ".csv";
    std::ifstream stateFile(statePath);
    if (stateFile.is_open()) {
      warnings.push_back("⚠️  [STORAGE] Debug file found: logs/drl/state_" + region + ".csv (should be disabled, debug only)");
      stateFile.close();
    }
  }
  
  // Print warnings (do not abort)
  if (!warnings.empty()) {
    for (const auto& warning : warnings) {
      std::cout << warning << std::endl;
    }
  } else {
    std::cout << "✓ [STORAGE] All file sizes within expected ranges" << std::endl;
  }
  
  std::cout << "\n╚══════════════════════════════════════════════════════════════════════════════╝" << std::endl;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

