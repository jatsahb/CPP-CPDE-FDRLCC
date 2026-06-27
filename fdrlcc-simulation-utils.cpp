/**
 * fdrlcc-simulation-utils.cpp
 * 
 * Utility functions for FDRLCC simulation
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-simulation-utils.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Serialize state vector to comma-separated string (for CSV)
 */
std::string
SerializeStateVector(const std::vector<double>& state)
{
  std::ostringstream oss;
  for (size_t i = 0; i < state.size(); ++i) {
    if (i > 0) oss << ";";
    oss << std::fixed << std::setprecision(6) << state[i];
  }
  return oss.str();
}

/**
 * Calculate variance of a vector
 */
double
CalculateVariance(const std::vector<double>& values)
{
  if (values.empty()) return 0.0;
  double mean = 0.0;
  for (double v : values) mean += v;
  mean /= values.size();
  
  double variance = 0.0;
  for (double v : values) {
    double diff = v - mean;
    variance += diff * diff;
  }
  return variance / values.size();
}

/**
 * Calculate mean of a vector
 */
double
CalculateMean(const std::vector<double>& values)
{
  if (values.empty()) return 0.0;
  double sum = 0.0;
  for (double v : values) sum += v;
  return sum / values.size();
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

