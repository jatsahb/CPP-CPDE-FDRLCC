#ifndef FDRLCC_HELPERS_FDRL_RESULTS_LOGGER_HPP_
#define FDRLCC_HELPERS_FDRL_RESULTS_LOGGER_HPP_

#include "ns3/nstime.h"
#include <string>
#include <fstream>
#include <memory>

namespace ns3 {
namespace ndn {
namespace fdrl {

struct ActionVector;
struct MetricSnapshot;

/**
 * \brief Helper class for logging FDRLCC experiment results to files.
 *
 * Manages all result files and ensures they are properly initialized and written.
 */
class ResultsLogger
{
public:
  ResultsLogger();
  ~ResultsLogger();

  /**
   * \brief Initialize logger with results directory path.
   * \param resultsDir Path to results directory (will be created if needed)
   * \return true if initialization successful, false otherwise
   */
  bool Initialize(const std::string& resultsDir);

  /**
   * \brief Close all log files and finalize logging.
   */
  void Shutdown();

  // Action logging
  void LogAction(const ActionVector& action, Time timestamp, const std::vector<double>& state);

  // State logging
  void LogState(const std::vector<double>& state, Time timestamp);

  // Reward logging
  void LogReward(double reward, Time timestamp, const MetricSnapshot& metrics);

  // Federated learning logging
  void LogFederatedUpdate(Time timestamp, const std::vector<uint8_t>& localWeights, 
                          const std::vector<uint8_t>& globalWeights, int roundNumber);

  // Check if files have content
  bool HasEntries(const std::string& metricName) const;
  size_t GetEntryCount(const std::string& metricName) const;

private:
  void EnsureDirectoryExists(const std::string& path);
  void InitializeFiles();
  void WriteFileHeader(std::ofstream& file, const std::string& header);

private:
  std::string m_resultsDir;
  bool m_initialized;

  // File streams
  std::ofstream m_actionsFile;
  std::ofstream m_statesFile;
  std::ofstream m_rewardsFile;
  std::ofstream m_federatedWeightsFile;

  // Entry counters
  size_t m_actionEntries;
  size_t m_stateEntries;
  size_t m_rewardEntries;
  size_t m_federatedEntries;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_HELPERS_FDRL_RESULTS_LOGGER_HPP_

