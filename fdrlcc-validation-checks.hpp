/**
 * fdrlcc-validation-checks.hpp
 * 
 * STEP 7: Validation checks for scientific measurement instrument
 * Validates data completeness and consistency on simulation shutdown
 */

#ifndef FDRLCC_VALIDATION_CHECKS_HPP
#define FDRLCC_VALIDATION_CHECKS_HPP

#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * STEP 7: Validate all required CSV files have data (not just headers)
 * Warns if any required file is missing or empty
 * 
 * @param resultsDir Results directory path
 */
void ValidateDataCompleteness(const std::string& resultsDir);

/**
 * STEP 7: Validate timestamps are monotonic in CSV files
 * Warns if timestamps are non-monotonic (indicates clock issues)
 * 
 * @param resultsDir Results directory path
 */
void ValidateTimestampConsistency(const std::string& resultsDir);

/**
 * STEP 7: Validate all regions are covered in logs
 * Warns if any region is missing from structured logs
 * 
 * @param resultsDir Results directory path
 */
void ValidateRegionCoverage(const std::string& resultsDir);

/**
 * STEP 7: Run all validation checks (called on shutdown)
 * 
 * @param resultsDir Results directory path
 */
void RunValidationChecks(const std::string& resultsDir);

/**
 * STEP 6: Print storage safety warnings on shutdown
 * Warns if files exceed expected sizes (prevents silent disk explosion)
 * 
 * @param resultsDir Results directory path
 */
void PrintStorageWarnings(const std::string& resultsDir);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_VALIDATION_CHECKS_HPP

