/**
 * action-executor.hpp
 * 
 * Action executor for FDRLCC - fixes action-actuator gap
 * 
 * Rules:
 * - Validate action bounds BEFORE applying
 * - Apply action ONCE
 * - If action is invalid, REJECT and DO NOT TRAIN
 * - Remove all silent action correction
 */

#ifndef FDRLCC_CONTROL_ACTION_EXECUTOR_HPP
#define FDRLCC_CONTROL_ACTION_EXECUTOR_HPP

#include "ns3/ptr.h"
#include <string>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

class FdrlConsumer;

/**
 * Action execution result
 */
enum class ActionResult {
  SUCCESS,      // Action applied successfully
  INVALID_BOUNDS, // Action out of valid range
  INVALID_VALUE,  // Action is NaN or Inf
  CONSUMER_NOT_FOUND  // Consumer not found
};

/**
 * Action execution report
 */
struct ActionExecutionReport {
  bool success = false;
  std::string errorMessage = "";
  double requestedRateFactor = 1.0;
  double appliedRateFactor = 1.0;
  double baseFrequency = 0.0;
  double effectiveFrequencyBefore = 0.0;
  double effectiveFrequencyAfter = 0.0;
  
  bool IsValid() const {
    return success;
  }
};

/**
 * Action executor - validates and applies actions to consumers
 */
class ActionExecutor {
public:
  /**
   * Validate action value
   * @param rateFactor Rate factor to validate
   * @return true if valid, false otherwise
   */
  static bool ValidateAction(double rateFactor);
  
  /**
   * Apply action to all consumers in a region
   * @param region Region name
   * @param rateFactor Rate factor to apply
   * @return Vector of execution reports (one per consumer)
   */
  static std::vector<ActionExecutionReport> ApplyActionToRegion(
    const std::string& region, 
    double rateFactor
  );
  
  /**
   * Apply action to a single consumer
   * @param consumer Consumer to apply action to
   * @param rateFactor Rate factor to apply
   * @return Execution report
   */
  static ActionExecutionReport ApplyAction(
    Ptr<FdrlConsumer> consumer, 
    double rateFactor
  );
  
  // Unified rate-factor bounds (paper, actor, SelectAction, executor)
  static constexpr double MIN_ACTION = 0.5;
  static constexpr double MAX_ACTION = 2.0;
  /** (MAX_ACTION - MIN_ACTION) / 2 — must match ActorNetwork tanh scaling */
  static constexpr double ACTOR_OUTPUT_SCALE = 0.75;
  /** Max per-control-step change in rate factor (soft cap) */
  static constexpr double MAX_RATE_FACTOR_STEP = 0.08;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CONTROL_ACTION_EXECUTOR_HPP

