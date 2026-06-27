#include "fdrl-results-logger.hpp"

#include "../controller/fdrl-action-policy.hpp"
// REFACTORED: MetricStore deleted - using MetricEngine instead
#include "src_cpp/metrics/metric-engine.hpp"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <iomanip>

NS_LOG_COMPONENT_DEFINE("ndn.FdrlResultsLogger");

namespace ns3 {
namespace ndn {
namespace fdrl {

ResultsLogger::ResultsLogger()
  : m_initialized(false)
  , m_actionEntries(0)
  , m_stateEntries(0)
  , m_rewardEntries(0)
  , m_federatedEntries(0)
{
}

ResultsLogger::~ResultsLogger()
{
  Shutdown();
}

bool
ResultsLogger::Initialize(const std::string& resultsDir)
{
  if (m_initialized) {
    NS_LOG_WARN("ResultsLogger already initialized");
    return true;
  }

  m_resultsDir = resultsDir;
  EnsureDirectoryExists(m_resultsDir);

  InitializeFiles();

  m_initialized = true;
  NS_LOG_INFO("ResultsLogger initialized with directory: " << m_resultsDir);
  return true;
}

void
ResultsLogger::Shutdown()
{
  if (!m_initialized) {
    return;
  }

  if (m_actionsFile.is_open()) {
    m_actionsFile.close();
  }
  if (m_statesFile.is_open()) {
    m_statesFile.close();
  }
  if (m_rewardsFile.is_open()) {
    m_rewardsFile.close();
  }
  if (m_federatedWeightsFile.is_open()) {
    m_federatedWeightsFile.close();
  }

  m_initialized = false;
  NS_LOG_INFO("ResultsLogger shutdown complete");
}

void
ResultsLogger::EnsureDirectoryExists(const std::string& path)
{
  struct stat info;
  if (stat(path.c_str(), &info) != 0) {
    // Directory doesn't exist, create it
    #ifdef _WIN32
    _mkdir(path.c_str());
    #else
    mkdir(path.c_str(), 0755);
    #endif
    NS_LOG_INFO("Created results directory: " << path);
  }
}

void
ResultsLogger::InitializeFiles()
{
  // Initialize actions file
  std::string actionsPath = m_resultsDir + "/fdrl-actions.txt";
  m_actionsFile.open(actionsPath, std::ios::out | std::ios::trunc);
  if (m_actionsFile.is_open()) {
    WriteFileHeader(m_actionsFile, "Time\tInterestRateFactor\tQueueThresholdFactor\tForwardingWeightDelta\tCacheAdjustment\tStateVector");
    NS_LOG_INFO("Opened actions file: " << actionsPath);
  } else {
    NS_LOG_ERROR("Failed to open actions file: " << actionsPath);
  }

  // Initialize states file
  std::string statesPath = m_resultsDir + "/fdrl-states.txt";
  m_statesFile.open(statesPath, std::ios::out | std::ios::trunc);
  if (m_statesFile.is_open()) {
    WriteFileHeader(m_statesFile, "Time\tStateVector");
    NS_LOG_INFO("Opened states file: " << statesPath);
  } else {
    NS_LOG_ERROR("Failed to open states file: " << statesPath);
  }

  // Initialize rewards file
  std::string rewardsPath = m_resultsDir + "/fdrl-rewards.txt";
  m_rewardsFile.open(rewardsPath, std::ios::out | std::ios::trunc);
  if (m_rewardsFile.is_open()) {
    WriteFileHeader(m_rewardsFile, "Time\tReward\tThroughput\tLatency\tPacketLoss\tCongestionLevel\tCacheHitRatio");
    NS_LOG_INFO("Opened rewards file: " << rewardsPath);
  } else {
    NS_LOG_ERROR("Failed to open rewards file: " << rewardsPath);
  }

  // Initialize federated weights file
  std::string federatedPath = m_resultsDir + "/federated-weights.txt";
  m_federatedWeightsFile.open(federatedPath, std::ios::out | std::ios::trunc);
  if (m_federatedWeightsFile.is_open()) {
    WriteFileHeader(m_federatedWeightsFile, "Time\tRoundNumber\tEvent\tLocalWeightsSize\tGlobalWeightsSize");
    NS_LOG_INFO("Opened federated weights file: " << federatedPath);
  } else {
    NS_LOG_ERROR("Failed to open federated weights file: " << federatedPath);
  }
}

void
ResultsLogger::WriteFileHeader(std::ofstream& file, const std::string& header)
{
  if (file.is_open()) {
    file << "# " << header << "\n";
    file.flush();
  }
}

void
ResultsLogger::LogAction(const ActionVector& action, Time timestamp, const std::vector<double>& state)
{
  if (!m_initialized || !m_actionsFile.is_open()) {
    return;
  }

  double timeSeconds = timestamp.GetSeconds();
  m_actionsFile << std::fixed << std::setprecision(6) << timeSeconds << "\t"
                << action.interestRateFactor << "\t"
                << action.queueThresholdFactor << "\t"
                << action.forwardingWeightDelta << "\t"
                << action.cacheAdjustment << "\t";

  // Write state vector
  for (size_t i = 0; i < state.size(); ++i) {
    if (i > 0) m_actionsFile << ",";
    m_actionsFile << state[i];
  }
  m_actionsFile << "\n";
  m_actionsFile.flush();

  m_actionEntries++;
}

void
ResultsLogger::LogState(const std::vector<double>& state, Time timestamp)
{
  if (!m_initialized || !m_statesFile.is_open()) {
    return;
  }

  double timeSeconds = timestamp.GetSeconds();
  m_statesFile << std::fixed << std::setprecision(6) << timeSeconds << "\t";

  // Write state vector
  for (size_t i = 0; i < state.size(); ++i) {
    if (i > 0) m_statesFile << ",";
    m_statesFile << state[i];
  }
  m_statesFile << "\n";
  m_statesFile.flush();

  m_stateEntries++;
}

void
ResultsLogger::LogReward(double reward, Time timestamp, const MetricSnapshot& metrics)
{
  if (!m_initialized || !m_rewardsFile.is_open()) {
    return;
  }

  // REFACTORED: Use MetricEngine snapshot fields (rttMeanMs -> avgDelayMs, packetLossRate/congestionLevel removed)
  double timeSeconds = timestamp.GetSeconds();
  double lossRate = 0.0;
  if (metrics.totalInterestsSent > 0) {
    lossRate = static_cast<double>(metrics.totalPacketsDropped) / static_cast<double>(metrics.totalInterestsSent);
  }
  m_rewardsFile << std::fixed << std::setprecision(6) << timeSeconds << "\t"
                << reward << "\t"
                << metrics.throughputMbps << "\t"
                << metrics.avgDelayMs << "\t"  // REFACTORED: avgDelayMs instead of rttMeanMs
                << lossRate << "\t"  // REFACTORED: Calculate from totalPacketsDropped/totalInterestsSent
                << metrics.queueOccupancy << "\t"  // REFACTORED: Use queueOccupancy as congestion indicator
                << metrics.cacheHitRatio << "\n";
  m_rewardsFile.flush();

  m_rewardEntries++;
}

void
ResultsLogger::LogFederatedUpdate(Time timestamp, const std::vector<uint8_t>& localWeights,
                                  const std::vector<uint8_t>& globalWeights, int roundNumber)
{
  if (!m_initialized || !m_federatedWeightsFile.is_open()) {
    return;
  }

  double timeSeconds = timestamp.GetSeconds();
  m_federatedWeightsFile << std::fixed << std::setprecision(6) << timeSeconds << "\t"
                         << roundNumber << "\t"
                         << "FederatedUpdate" << "\t"
                         << localWeights.size() << "\t"
                         << globalWeights.size() << "\n";
  m_federatedWeightsFile.flush();

  m_federatedEntries++;
}

bool
ResultsLogger::HasEntries(const std::string& metricName) const
{
  if (metricName == "actions") {
    return m_actionEntries > 0;
  } else if (metricName == "states") {
    return m_stateEntries > 0;
  } else if (metricName == "rewards") {
    return m_rewardEntries > 0;
  } else if (metricName == "federated") {
    return m_federatedEntries > 0;
  }
  return false;
}

size_t
ResultsLogger::GetEntryCount(const std::string& metricName) const
{
  if (metricName == "actions") {
    return m_actionEntries;
  } else if (metricName == "states") {
    return m_stateEntries;
  } else if (metricName == "rewards") {
    return m_rewardEntries;
  } else if (metricName == "federated") {
    return m_federatedEntries;
  }
  return 0;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

