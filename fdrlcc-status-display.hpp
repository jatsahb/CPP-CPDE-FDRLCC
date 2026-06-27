/**
 * fdrlcc-status-display.hpp
 * 
 * Structured console status display for FDRLCC
 * Shows comprehensive simulation status in formatted boxes
 */

#ifndef FDRLCC_STATUS_DISPLAY_HPP
#define FDRLCC_STATUS_DISPLAY_HPP

#include "ns3/nstime.h"
#include <string>
#include <map>
#include <vector>
#include <deque>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Status display for FDRLCC simulation
 * Prints formatted status boxes at regular intervals
 */
class StatusDisplay
{
public:
  /**
   * Initialize status display
   */
  static void Initialize();
  
  /**
   * Start periodic status display
   * @param interval Display interval (default: 10.0 seconds)
   */
  static void Start(Time interval = Seconds(10.0));
  
  /**
   * Stop status display
   */
  static void Stop();
  
  /**
   * Print status display immediately (for FL rounds)
   * @param isFLRound Whether this is an FL round
   */
  static void PrintStatus(bool isFLRound = false);
  
  /**
   * Cleanup
   */
  static void Cleanup();

private:
  /**
   * Periodic status display callback
   */
  static void PeriodicDisplay();
  
  /**
   * Calculate congestion trend (Up/Down/Stable)
   */
  static std::string CalculateCongestionTrend(const std::string& region);
  
  /**
   * Calculate reward trend (Up/Down/Stable)
   */
  static std::string CalculateRewardTrend(const std::string& region);
  
  /**
   * Decompose reward into components
   */
  static void DecomposeReward(const std::string& region, 
                              double& throughputReward,
                              double& delayPenalty,
                              double& lossPenalty,
                              double& congPenalty);
  
  /**
   * Get latest training losses
   */
  static void GetTrainingLosses(const std::string& region,
                                double& actorLoss,
                                double& criticLoss);
  
  /**
   * Get system-wide statistics
   */
  static void GetSystemStats(size_t& totalNodes,
                            size_t& totalConsumers,
                            size_t& totalProducers);
  
  // Static members
  static bool m_initialized;
  static bool m_running;
  static Time m_displayInterval;
  static EventId m_displayEvent;
  static double m_lastDisplayTime;
  
  // History for trend calculation
  static std::map<std::string, std::deque<double>> m_queueHistory;
  static std::map<std::string, std::deque<double>> m_rttHistory;
  static std::map<std::string, std::deque<double>> m_rewardHistory;
  static std::map<std::string, double> m_lastActorLoss;
  static std::map<std::string, double> m_lastCriticLoss;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_STATUS_DISPLAY_HPP

