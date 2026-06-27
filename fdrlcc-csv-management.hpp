/**
 * fdrlcc-csv-management.hpp
 * 
 * CSV file management functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_CSV_MANAGEMENT_HPP
#define FDRLCC_CSV_MANAGEMENT_HPP

#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Periodic CSV flush (buffered I/O)
 * Flushes all open CSV files periodically
 */
void FlushCsvFiles();

/**
 * Initialize training CSV files
 * Opens all training CSV files and writes headers
 * 
 * @param resultsDir Results directory path
 */
void InitializeTrainingCsvFiles(const std::string& resultsDir);

/**
 * Close training CSV files
 * Closes all open CSV files
 */
void CloseTrainingCsvFiles();

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CSV_MANAGEMENT_HPP

