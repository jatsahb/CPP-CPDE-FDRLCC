/**
 * fdrlcc-simulation-utils.hpp
 * 
 * Utility functions for FDRLCC simulation
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_SIMULATION_UTILS_HPP
#define FDRLCC_SIMULATION_UTILS_HPP

#include <vector>
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Serialize state vector to comma-separated string (for CSV)
 * 
 * @param state State vector to serialize
 * @return Comma-separated string representation
 */
std::string SerializeStateVector(const std::vector<double>& state);

/**
 * Calculate variance of a vector
 * 
 * @param values Vector of values
 * @return Variance
 */
double CalculateVariance(const std::vector<double>& values);

/**
 * Calculate mean of a vector
 * 
 * @param values Vector of values
 * @return Mean
 */
double CalculateMean(const std::vector<double>& values);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_SIMULATION_UTILS_HPP

