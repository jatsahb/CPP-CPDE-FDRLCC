/**
 * fdrlcc-event-logger.hpp
 * 
 * Event-driven console logging system for FDRLCC
 * Narrates system behavior in real-time, printing only on meaningful events
 */

#ifndef FDRLCC_EVENT_LOGGER_HPP
#define FDRLCC_EVENT_LOGGER_HPP

#include "ns3/nstime.h"
#include <string>
#include <vector>
#include <map>
#include <deque>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Event-driven logger for FDRLCC system
 * Tracks system phases and prints meaningful events only
 */
class EventLogger
{
public:
  /**
   * Initialize event logger
   */
  static void Initialize();
  
  /**
   * Phase 1: Normal Operation
   * Log periodic normal operation metrics
   */
  static void LogNormalOperation(double time, const std::string& region,
                                 double interestRate, double avgRtt, double queueOccupancy);
  
  /**
   * Phase 2: Congestion Formation Detection
   * Detect and log when congestion is forming
   */
  static void CheckAndLogCongestion(double time, const std::string& region,
                                    double avgRtt, double queueOccupancy);
  
  /**
   * Phase 3: DRL Reaction
   * Log when DRL policy applies a new action
   */
  static void LogDRLAction(double time, const std::string& region,
                           double rateFactor, double reward);
  
  /**
   * Phase 4: Federated Learning Aggregation
   * Log FL aggregation events
   */
  static void LogFLAggregation(double time, size_t numClients,
                               const std::map<std::string, double>& weights,
                               double divergence, double globalReward);
  
  /**
   * Phase 5: Recovery and Stability
   * Detect and log when system reaches stable state
   */
  static void CheckAndLogStability(double time, const std::string& region,
                                   double avgRtt, double rttVariance, double queueOccupancy);
  
  /**
   * Validation checks - log warnings for anomalies
   */
  static void ValidateSystemState(double time);
  
  /**
   * Cleanup and final validation report
   */
  static void Cleanup();
  
private:
  // RTT history for percentile calculation
  static std::map<std::string, std::deque<double>> m_rttHistory;
  static std::map<std::string, double> m_lastQueueOccupancy;
  static std::map<std::string, double> m_lastRtt;
  static std::map<std::string, double> m_baselineRtt; // Baseline RTT for spike detection
  static std::map<std::string, bool> m_congestionDetected;
  static std::map<std::string, bool> m_stableStateReached;
  static std::map<std::string, double> m_lastActionTime;
  static std::map<std::string, double> m_lastActionValue;
  
  // Validation state
  static bool m_congestionOccurred;
  static bool m_drlActionsChanged;
  static std::map<std::string, double> m_initialReward;
  static std::map<std::string, double> m_bestReward;
  static bool m_flWeightsAdapted;
  
  // Constants
  static constexpr double CONGESTION_QUEUE_THRESHOLD = 0.80;
  static constexpr double CONGESTION_RTT_ABSOLUTE_THRESHOLD = 10.0; // ms - RTT spike detection
  static constexpr double CONGESTION_RTT_MULTIPLIER = 2.0; // RTT > 2x baseline indicates congestion
  static constexpr double STABLE_QUEUE_THRESHOLD = 0.40;
  static constexpr double STABLE_RTT_VARIANCE_THRESHOLD = 2.0; // ms²
  static constexpr size_t RTT_HISTORY_SIZE = 100;
  static constexpr double RTT_PERCENTILE_90 = 0.90;
  static constexpr size_t MIN_RTT_HISTORY_FOR_PERCENTILE = 10; // Minimum samples for percentile calculation
  
  // Helper functions
  static double CalculateRttPercentile(const std::string& region, double percentile);
  static void UpdateRttHistory(const std::string& region, double rtt);
  static size_t GetRttHistorySize(const std::string& region);
  static std::vector<double> GetRttHistory(const std::string& region);
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_EVENT_LOGGER_HPP
