/**
 * fdrl-math-utils.hpp
 * 
 * Mathematical utility functions for FDRLCC
 * Including Huber loss (smooth L1) for critic training
 */

#ifndef FDRL_MATH_UTILS_HPP
#define FDRL_MATH_UTILS_HPP

#include <cmath>
#include <algorithm>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Huber loss (smooth L1 loss)
 * Quadratic near zero, linear for large errors (more robust than MSE)
 * 
 * @param error The error value (e.g., TD error)
 * @param delta Threshold where loss transitions from quadratic to linear (default 1.0)
 * @return Huber loss value
 */
inline double
HuberLoss(double error, double delta = 1.0)
{
  double abs_error = std::abs(error);
  if (abs_error <= delta) {
    // Quadratic region: 0.5 * error^2
    return 0.5 * error * error;
  } else {
    // Linear region: delta * (abs_error - 0.5 * delta)
    return delta * (abs_error - 0.5 * delta);
  }
}

/**
 * Huber loss derivative w.r.t. error
 * Used for backpropagation
 * 
 * @param error The error value
 * @param delta Threshold where loss transitions from quadratic to linear (default 1.0)
 * @return Gradient of Huber loss w.r.t. error
 */
inline double
HuberLossGradient(double error, double delta = 1.0)
{
  double abs_error = std::abs(error);
  if (abs_error <= delta) {
    // Quadratic region: derivative is error
    return error;
  } else {
    // Linear region: derivative is delta * sign(error)
    return (error > 0.0) ? delta : -delta;
  }
}

/**
 * Calculate mean of a vector
 */
inline double
CalculateMean(const std::vector<double>& values)
{
  if (values.empty()) return 0.0;
  double sum = 0.0;
  for (double v : values) sum += v;
  return sum / values.size();
}

/**
 * Calculate variance of a vector
 */
inline double
CalculateVariance(const std::vector<double>& values)
{
  if (values.empty()) return 0.0;
  double mean = CalculateMean(values);
  double variance = 0.0;
  for (double v : values) {
    double diff = v - mean;
    variance += diff * diff;
  }
  return variance / values.size();
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRL_MATH_UTILS_HPP

