/**
 * fdrlcc-results-output.hpp
 * 
 * Results output and console display functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_RESULTS_OUTPUT_HPP
#define FDRLCC_RESULTS_OUTPUT_HPP

#include "fdrlcc-types.hpp"
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Print final results summary
 */
void PrintResults();

/**
 * Print enhanced console output with full system status
 * 
 * @param simTime Current simulation time
 */
void PrintEnhancedConsoleOutput(double simTime);

/**
 * Schedule console output (recursive scheduler)
 * Called every 5 seconds
 */
void ScheduleConsoleOutput();

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_RESULTS_OUTPUT_HPP

