#ifndef FDRLCC_HELPERS_FDRL_PERFORMANCE_LOGGER_HPP_
#define FDRLCC_HELPERS_FDRL_PERFORMANCE_LOGGER_HPP_

#include "ns3/nstime.h"
#include <string>
#include <fstream>
#include <memory>
#include <mutex>
#include <deque>
#include <map>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * \brief Research-grade performance logger for FDRLCC simulations
 * 
 * Implements a unified logging system that:
 * - Logs common metrics every 1 second (all algorithms)
 * - Logs AI-specific metrics every 2 seconds (DRL/FDRL)
 * - Logs FL aggregation events (FDRL only)
 * - Produces clean console output every 5 seconds
 * - Creates single CSV file per run with header comments
 */
class PerformanceLogger {
public:
  /**
   * \brief Get singleton instance
   */
  static PerformanceLogger& GetInstance();
  
  /**
   * \brief Initialize logger with results directory and run information
   * \param resultsDir Results directory path
   * \param algorithm Algorithm name (CUBIC, AIMD, BIC, DRL, FDRL)
   * \param runId Run number (01, 02, 03, etc.)
   * \param topology Topology description
   * \param seed Random seed used
   * \return true if initialization successful
   */
  bool Initialize(const std::string& resultsDir,
                  const std::string& algorithm,
                  uint32_t runId,
                  const std::string& topology,
                  uint32_t seed);
  
  /**
   * \brief Shutdown logger and close files
   */
  void Shutdown();
  
  // Common metrics (logged every 1 second)
  struct CommonMetrics {
    double timestamp = 0.0;
    std::string nodeId;
    double throughputMbps = 0.0;
    double e2eLatencyMs = 0.0;
    double jitterMs = 0.0;
    double packetLossRatio = 0.0;
    double interestGenRate = 0.0;
    double dataSatisfactionRate = 0.0;
    double csHitRatio = 0.0;
    double csUtilization = 0.0;
    double pitOccupancy = 0.0;
    size_t fibSize = 0;
    double cnpi = 0.0;
  };
  
  // AI-specific metrics (logged every 2 seconds)
  struct AIMetrics {
    double timestamp = 0.0;
    std::string nodeId;
    double rlAction = 0.0;
    double reward = 0.0;
    double rewardAvg = -1.0;  // -1 until window fills
    double rewardVariance = -1.0;  // -1 until window fills
    double lossValue = -1.0;  // Training loss, -1 if unavailable
    double policyEntropy = -1.0;  // Always -1 (not implemented)
    int convergenceIndicator = 0;  // 0 or 1
    int divergenceIndicator = 0;  // 0 or 1, NA for non-FL
    uint32_t trainingStep = 0;
  };
  
  // FL aggregation metrics (event-driven)
  struct FLMetrics {
    double timestamp = 0.0;
    uint32_t flRound = 0;
    double globalModelDivergence = 0.0;
    double modelUpdateNorm = 0.0;
    double avgLocalReward = 0.0;
    int64_t communicationOverheadBytes = -1;  // -1 if unavailable
  };
  
  /**
   * \brief Log common metrics (called every 1 second)
   */
  void LogCommonMetrics(const CommonMetrics& metrics);
  
  /**
   * \brief Log AI-specific metrics (called every 2 seconds)
   */
  void LogAIMetrics(const AIMetrics& metrics);
  
  /**
   * \brief Log FL aggregation event (called on aggregation completion)
   */
  void LogFLMetrics(const FLMetrics& metrics);
  
  /**
   * \brief Update reward history for rolling window calculations
   */
  void UpdateRewardHistory(const std::string& nodeId, double reward);
  
  /**
   * \brief Get reward average for a node (rolling window)
   */
  double GetRewardAverage(const std::string& nodeId) const;
  
  /**
   * \brief Get reward variance for a node (rolling window)
   */
  double GetRewardVariance(const std::string& nodeId) const;
  
  /**
   * \brief Calculate convergence indicator for a node
   */
  int CalculateConvergenceIndicator(const std::string& nodeId) const;
  
  /**
   * \brief Update divergence indicator (propagates to AI metrics)
   */
  void UpdateDivergenceIndicator(double flDivergence);
  
  /**
   * \brief Get current divergence indicator
   */
  double GetCurrentDivergence() const;
  
  /**
   * \brief Print console output (called every 5 seconds)
   * \param algorithm Algorithm type (CUBIC, DRL, FDRL)
   * \param enableAI Whether AI metrics should be displayed
   */
  void PrintConsoleOutput(const std::string& algorithm, bool enableAI);
  
  /**
   * \brief Set console output interval (in seconds)
   */
  void SetConsoleInterval(double interval);
  
  /**
   * \brief Check if logger is initialized
   */
  bool IsInitialized() const { return m_initialized; }
  
private:
  PerformanceLogger();
  ~PerformanceLogger();
  PerformanceLogger(const PerformanceLogger&) = delete;
  PerformanceLogger& operator=(const PerformanceLogger&) = delete;
  
  void WriteHeaderComments();
  void WriteCSVHeader();
  void WriteRow(const std::string& row);
  std::string FormatValue(double value, int precision = 2);
  std::string FormatValue(int value);
  std::string FormatValue(uint32_t value);
  std::string FormatValue(int64_t value);
  std::string FormatValue(const std::string& value);
  std::string FormatValue(size_t value);
  
  // Rolling window for reward statistics
  struct RewardWindow {
    std::deque<double> samples;
    static constexpr size_t MAX_SIZE = 10;
    
    void Add(double reward) {
      samples.push_back(reward);
      if (samples.size() > MAX_SIZE) {
        samples.pop_front();
      }
    }
    
    double GetAverage() const {
      if (samples.size() < MAX_SIZE) return -1.0;
      double sum = 0.0;
      for (double r : samples) sum += r;
      return sum / static_cast<double>(samples.size());
    }
    
    double GetVariance() const {
      if (samples.size() < MAX_SIZE) return -1.0;
      double avg = GetAverage();
      double variance = 0.0;
      for (double r : samples) {
        double diff = r - avg;
        variance += diff * diff;
      }
      return variance / static_cast<double>(samples.size());
    }
    
    std::vector<double> GetRecentSamples(size_t count) const {
      if (samples.size() < count) return {};
      std::vector<double> result;
      size_t start = samples.size() - count;
      for (size_t i = start; i < samples.size(); ++i) {
        result.push_back(samples[i]);
      }
      return result;
    }
  };
  
  bool m_initialized = false;
  std::string m_resultsDir;
  std::string m_algorithm;
  uint32_t m_runId;
  std::string m_topology;
  uint32_t m_seed;
  
  std::ofstream m_file;
  std::mutex m_fileMutex;
  
  // Rolling windows per node
  std::map<std::string, RewardWindow> m_rewardWindows;
  
  // Current divergence (propagated to AI metrics)
  double m_currentDivergence = 0.0;
  
  // Console output tracking
  double m_consoleInterval = 5.0;  // Default 5 seconds
  double m_lastConsolePrint = 0.0;
  
  // Cached metrics for console output
  std::map<std::string, CommonMetrics> m_lastCommonMetrics;
  std::map<std::string, AIMetrics> m_lastAIMetrics;
  double m_lastFLTimestamp = 0.0;
  FLMetrics m_lastFLMetrics;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_HELPERS_FDRL_PERFORMANCE_LOGGER_HPP_

