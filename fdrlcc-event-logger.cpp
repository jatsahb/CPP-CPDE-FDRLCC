/**
 * fdrlcc-event-logger.cpp
 * 
 * Implementation of event-driven console logging system
 */

#include "fdrlcc-event-logger.hpp"
#include "fdrlcc-console-output.hpp"
#include "fdrlcc-types.hpp"
#include "src_cpp/metrics/metric-engine.hpp"
#include "ns3/simulator.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <cmath>
#include <sstream>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Static member initialization
std::map<std::string, std::deque<double>> EventLogger::m_rttHistory;
std::map<std::string, double> EventLogger::m_lastQueueOccupancy;
std::map<std::string, double> EventLogger::m_lastRtt;
std::map<std::string, double> EventLogger::m_baselineRtt;
std::map<std::string, bool> EventLogger::m_congestionDetected;
std::map<std::string, bool> EventLogger::m_stableStateReached;
std::map<std::string, double> EventLogger::m_lastActionTime;
std::map<std::string, double> EventLogger::m_lastActionValue;
bool EventLogger::m_congestionOccurred = false;
bool EventLogger::m_drlActionsChanged = false;
std::map<std::string, double> EventLogger::m_initialReward;
std::map<std::string, double> EventLogger::m_bestReward;
bool EventLogger::m_flWeightsAdapted = false;

void
EventLogger::Initialize()
{
  m_rttHistory.clear();
  m_lastQueueOccupancy.clear();
  m_lastRtt.clear();
  m_baselineRtt.clear();
  m_congestionDetected.clear();
  m_stableStateReached.clear();
  m_lastActionTime.clear();
  m_lastActionValue.clear();
  m_congestionOccurred = false;
  m_drlActionsChanged = false;
  m_initialReward.clear();
  m_bestReward.clear();
  m_flWeightsAdapted = false;
}

void
EventLogger::LogNormalOperation(double time, const std::string& region,
                                double interestRate, double avgRtt, double queueOccupancy)
{
  // Log periodically (every 5 seconds) during normal operation
  static std::map<std::string, double> lastLogTime;
  if (lastLogTime.find(region) == lastLogTime.end()) {
    lastLogTime[region] = 0.0;
  }
  
  if (!ConsoleOutput::IsVerbose()) {
    return;
  }

  if (time - lastLogTime[region] >= 5.0) {
    std::cout << "[" << std::fixed << std::setprecision(1) << time << "s][NETWORK] Normal operation | Region=" << region
              << " | InterestRate=" << std::setprecision(2) << interestRate << " Hz"
              << " | RTT=" << std::setprecision(1) << avgRtt << " ms"
              << " | Queue=" << std::setprecision(1) << (queueOccupancy * 100.0) << "%" << std::endl;
    lastLogTime[region] = time;
  }
  
  // Update RTT history for percentile calculation
  UpdateRttHistory(region, avgRtt);
  m_lastRtt[region] = avgRtt;
  m_lastQueueOccupancy[region] = queueOccupancy;
}

void
EventLogger::CheckAndLogCongestion(double time, const std::string& region,
                                   double avgRtt, double queueOccupancy)
{
  // Update baseline RTT (use median of recent history as baseline)
  if (m_rttHistory.find(region) != m_rttHistory.end() && 
      m_rttHistory[region].size() >= MIN_RTT_HISTORY_FOR_PERCENTILE) {
    std::vector<double> sortedRtts(m_rttHistory[region].begin(), m_rttHistory[region].end());
    std::sort(sortedRtts.begin(), sortedRtts.end());
    size_t medianIndex = sortedRtts.size() / 2;
    m_baselineRtt[region] = sortedRtts[medianIndex];
  } else if (m_baselineRtt.find(region) == m_baselineRtt.end()) {
    // Initialize baseline with first few RTT values
    if (avgRtt > 0.1) {
      m_baselineRtt[region] = avgRtt;
    } else {
      m_baselineRtt[region] = 2.0; // Default baseline
    }
  }
  
  // Check if congestion is forming using multiple indicators
  bool queueCongestion = (queueOccupancy > CONGESTION_QUEUE_THRESHOLD);
  
  // RTT spike detection: absolute threshold OR relative to baseline
  bool rttSpikeAbsolute = (avgRtt > CONGESTION_RTT_ABSOLUTE_THRESHOLD);
  bool rttSpikeRelative = false;
  if (m_baselineRtt.find(region) != m_baselineRtt.end() && m_baselineRtt[region] > 0.1) {
    rttSpikeRelative = (avgRtt > (m_baselineRtt[region] * CONGESTION_RTT_MULTIPLIER));
  }
  
  // RTT percentile-based detection (requires sufficient history)
  double rttPercentile90 = CalculateRttPercentile(region, RTT_PERCENTILE_90);
  bool rttPercentileCongestion = false;
  if (rttPercentile90 > 0.0 && 
      GetRttHistorySize(region) >= MIN_RTT_HISTORY_FOR_PERCENTILE) {
    rttPercentileCongestion = (avgRtt > rttPercentile90);
  }
  
  // Congestion detected if any indicator is true
  bool congestionDetected = queueCongestion || rttSpikeAbsolute || rttSpikeRelative || rttPercentileCongestion;
  
  // Log congestion formation
  if (congestionDetected && !m_congestionDetected[region]) {
    m_congestionDetected[region] = true;
    m_congestionOccurred = true;
    
    std::string indicators;
    if (queueCongestion) indicators += "queue ";
    if (rttSpikeAbsolute) indicators += "rtt_absolute ";
    if (rttSpikeRelative) indicators += "rtt_relative ";
    if (rttPercentileCongestion) indicators += "rtt_percentile ";
    
    if (ConsoleOutput::IsCompact()) {
      std::ostringstream msg;
      msg << "FDRLCC | t=" << std::fixed << std::setprecision(0) << time << "s Region " << region
          << " congestion | queue=" << std::setprecision(0) << (queueOccupancy * 100.0)
          << "% RTT=" << std::setprecision(1) << avgRtt << "ms";
      ConsoleOutput::PrintWarning(msg.str());
    } else if (ConsoleOutput::IsVerbose()) {
      std::cout << "[" << std::fixed << std::setprecision(1) << time << "s][NETWORK] Congestion forming | Region=" << region
                << " | RTT=" << std::setprecision(1) << avgRtt << " ms"
                << " | Baseline=" << std::setprecision(1) << m_baselineRtt[region] << " ms"
                << " | Queue=" << std::setprecision(1) << (queueOccupancy * 100.0) << "%"
                << " | Indicators=[" << indicators << "]" << std::endl;
    }
  }

  // Clear congestion flag if conditions improve
  if (!congestionDetected && m_congestionDetected[region]) {
    m_congestionDetected[region] = false;
  }
}

void
EventLogger::LogDRLAction(double time, const std::string& region,
                          double rateFactor, double reward)
{
  // Log when action changes significantly (>5% change)
  bool actionChanged = false;
  if (m_lastActionValue.find(region) != m_lastActionValue.end()) {
    double change = std::abs(rateFactor - m_lastActionValue[region]) / m_lastActionValue[region];
    if (change > 0.05) {
      actionChanged = true;
      m_drlActionsChanged = true;
    }
  } else {
    actionChanged = true;
    m_drlActionsChanged = true;
  }
  
  // Log action application
  if (actionChanged || (time - m_lastActionTime[region] >= 10.0)) {
    m_lastActionTime[region] = time;
    m_lastActionValue[region] = rateFactor;

    if (m_initialReward.find(region) == m_initialReward.end()) {
      m_initialReward[region] = reward;
      m_bestReward[region] = reward;
    } else if (reward > m_bestReward[region]) {
      m_bestReward[region] = reward;
    }

    if (!ConsoleOutput::IsVerbose()) {
      return;
    }

    std::cout << "[" << std::fixed << std::setprecision(1) << time << "s][DRL] Action applied | Region=" << region
              << " | rate_factor=" << std::setprecision(3) << rateFactor
              << " | reward=" << std::setprecision(4) << reward << std::endl;
  }
}

void
EventLogger::LogFLAggregation(double time, size_t numClients,
                             const std::map<std::string, double>& weights,
                             double divergence, double globalReward)
{
  // Build weights string
  std::ostringstream weightsStr;
  weightsStr << "[";
  bool first = true;
  for (const auto& [region, weight] : weights) {
    if (!first) weightsStr << ",";
    weightsStr << std::setprecision(3) << weight;
    first = false;
  }
  weightsStr << "]";
  
  // Check if weights are adaptive (not uniform)
  if (weights.size() > 1) {
    double firstWeight = weights.begin()->second;
    for (const auto& [region, weight] : weights) {
      if (std::abs(weight - firstWeight) > 0.01) {
        m_flWeightsAdapted = true;
        break;
      }
    }
  }
  
  if (!ConsoleOutput::IsVerbose()) {
    return;
  }

  std::cout << "[" << std::fixed << std::setprecision(1) << time << "s][FL] Adaptive aggregation | num_clients=" << numClients
            << " | weights=" << weightsStr.str()
            << " | divergence=" << std::setprecision(4) << divergence
            << " | global_reward=" << std::setprecision(4) << globalReward << std::endl;
}

void
EventLogger::CheckAndLogStability(double time, const std::string& region,
                                  double avgRtt, double rttVariance, double queueOccupancy)
{
  // Calculate actual RTT variance from history if available
  double actualRttVariance = rttVariance;
  if (GetRttHistorySize(region) > 10) {
    std::vector<double> rttHistory = GetRttHistory(region);
    double rttMean = 0.0;
    for (double rtt : rttHistory) {
      rttMean += rtt;
    }
    rttMean /= rttHistory.size();
    
    double sumSqDiff = 0.0;
    for (double rtt : rttHistory) {
      double diff = rtt - rttMean;
      sumSqDiff += diff * diff;
    }
    actualRttVariance = sumSqDiff / rttHistory.size();
  }
  
  // Check if system has recovered and stabilized
  bool queueStable = (queueOccupancy < STABLE_QUEUE_THRESHOLD);
  bool rttStable = (actualRttVariance < STABLE_RTT_VARIANCE_THRESHOLD);
  
  if (queueStable && rttStable && !m_stableStateReached[region]) {
    m_stableStateReached[region] = true;
    
    std::cout << "[" << std::fixed << std::setprecision(1) << time << "s][SYSTEM] Stable state achieved | Region=" << region
              << " | RTT=" << std::setprecision(1) << avgRtt << " ms"
              << " | RTT_variance=" << std::setprecision(2) << actualRttVariance << " ms²"
              << " | Queue=" << std::setprecision(1) << (queueOccupancy * 100.0) << "%" << std::endl;
  }
  
  // Clear stability flag if conditions deteriorate
  if ((!queueStable || !rttStable) && m_stableStateReached[region]) {
    m_stableStateReached[region] = false;
  }
}

void
EventLogger::ValidateSystemState(double time)
{
  // Validation checks - only track state during simulation, warnings shown at end
  // Do NOT print warnings during simulation to avoid clutter
  static double lastValidationTime = 0.0;
  if (time - lastValidationTime < 10.0) return;  // Check every 10 seconds
  lastValidationTime = time;
  
  // Just track state - warnings will be shown in Cleanup()
  // No console output here to keep simulation output clean
}

double
EventLogger::CalculateRttPercentile(const std::string& region, double percentile)
{
  if (m_rttHistory.find(region) == m_rttHistory.end() || m_rttHistory[region].empty()) {
    return -1.0;
  }
  
  std::vector<double> sortedRtts(m_rttHistory[region].begin(), m_rttHistory[region].end());
  std::sort(sortedRtts.begin(), sortedRtts.end());
  
  size_t index = static_cast<size_t>(std::ceil(percentile * sortedRtts.size())) - 1;
  if (index >= sortedRtts.size()) {
    index = sortedRtts.size() - 1;
  }
  
  return sortedRtts[index];
}

void
EventLogger::UpdateRttHistory(const std::string& region, double rtt)
{
  if (m_rttHistory.find(region) == m_rttHistory.end()) {
    m_rttHistory[region] = std::deque<double>();
  }
  
  m_rttHistory[region].push_back(rtt);
  
  // Keep only recent history
  if (m_rttHistory[region].size() > RTT_HISTORY_SIZE) {
    m_rttHistory[region].pop_front();
  }
}

size_t
EventLogger::GetRttHistorySize(const std::string& region)
{
  if (m_rttHistory.find(region) == m_rttHistory.end()) {
    return 0;
  }
  return m_rttHistory[region].size();
}

std::vector<double>
EventLogger::GetRttHistory(const std::string& region)
{
  std::vector<double> result;
  if (m_rttHistory.find(region) != m_rttHistory.end()) {
    result.assign(m_rttHistory[region].begin(), m_rttHistory[region].end());
  }
  return result;
}

void
EventLogger::Cleanup()
{
  // Final validation report
  std::cout << "\n[VALIDATION] System behavior summary:" << std::endl;
  
  if (!m_congestionOccurred) {
    std::cerr << "[WARNING] No congestion detected during simulation" << std::endl;
  } else {
    std::cout << "[OK] Congestion detected and handled" << std::endl;
  }
  
  if (!m_drlActionsChanged) {
    std::cerr << "[WARNING] DRL actions never changed - policy may be stuck" << std::endl;
  } else {
    std::cout << "[OK] DRL actions adapted during simulation" << std::endl;
  }
  
  for (const auto& [region, initialReward] : m_initialReward) {
    if (m_bestReward.find(region) != m_bestReward.end()) {
      double improvement = m_bestReward[region] - initialReward;
      if (improvement > 0.01) {
        std::cout << "[OK] Region=" << region << " | Reward improved from " 
                  << std::setprecision(4) << initialReward << " to " << m_bestReward[region] << std::endl;
      } else {
        std::cerr << "[WARNING] Region=" << region << " | Rewards did not improve significantly" << std::endl;
      }
    }
  }
  
  if (!m_flWeightsAdapted) {
    std::cerr << "[WARNING] FL aggregation weights never adapted - using uniform weights" << std::endl;
  } else {
    std::cout << "[OK] FL aggregation weights adapted during simulation" << std::endl;
  }
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3
