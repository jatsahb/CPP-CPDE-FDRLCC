/**
 * fdrlcc-structured-logger.hpp
 * 
 * Structured logging subsystem for FDRLCC experiments
 * 
 * Purpose: Provide organized, per-region, semantically-scoped logs
 * for evaluation figures, experimental results, and paper claims.
 * 
 * Architecture:
 * - Subscribes to MetricEngine (single source of truth)
 * - Organizes logs under /logs/ directory structure
 * - Per-region CSV files for congestion, DRL, federation, fairness
 * - JSON config dump for reproducibility
 * - Optional via --enable-structured-logs flag
 */

#ifndef FDRLCC_STRUCTURED_LOGGER_HPP
#define FDRLCC_STRUCTURED_LOGGER_HPP

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include <string>
#include <map>
#include <fstream>
#include <vector>
#include <memory>

namespace ns3 {
namespace ndn {
namespace fdrl {

class MetricEngine;
struct MetricSnapshot;
struct RegionDRLState;
struct FdrlccConfig;

/**
 * StructuredLogger - Event-driven structured logging subsystem
 * 
 * Rules:
 * - MetricEngine is single source of truth
 * - StructuredLogger subscribes to MetricEngine updates
 * - Writes only to /logs/ directory
 * - Per-region files for analysis
 * - Consistent schemas
 * - Self-contained and reproducible
 */
class StructuredLogger : public Object
{
public:
  static TypeId GetTypeId();
  
  StructuredLogger();
  ~StructuredLogger() override;
  
  /**
   * Initialize structured logger with base results directory
   * @param resultsDir Base results directory (will create logs/ subdirectory)
   * @param metricEngine Pointer to MetricEngine (single source of truth)
   * @return true if initialization successful
   */
  bool Initialize(const std::string& resultsDir, 
                  Ptr<MetricEngine> metricEngine);
  
  /**
   * Shutdown logger and close all files
   */
  void Shutdown();
  
  /**
   * Start logging (begins periodic subscription to MetricEngine)
   * @param interval Logging interval (should match MetricEngine interval)
   */
  void Start(Time interval = Seconds(1.0));
  
  /**
   * Stop logging
   */
  void Stop();
  
  // ============================================================================
  // REQUIRED INTERFACE METHODS (as specified)
  // ============================================================================
  
  /**
   * Log a general event (REQUIRED: LogEvent)
   * @param time Simulation time
   * @param eventType Event type string enum
   * @param details Event details (JSON-like string or free text)
   */
  void LogEvent(double time, const std::string& eventType, const std::string& details);
  
  /**
   * Log congestion metrics (REQUIRED: LogCongestion)
   * @param time Simulation time
   * @param region Region ID
   * @param queue Queue length/utilization
   * @param delay Delay in ms
   * @param loss Loss rate
   * @param state State vector (optional, can be empty)
   */
  void LogCongestion(double time, 
                    const std::string& region, 
                    double queue, 
                    double delay, 
                    double loss,
                    const std::vector<double>& state = std::vector<double>());
  
  /**
   * Log DRL action (REQUIRED: LogDRLAction)
   * @param time Simulation time
   * @param region Region ID
   * @param stateVec State vector
   * @param action Action value
   * @param reward Reward value
   * @param saturated Whether action is saturated (at bounds)
   */
  void LogDRLAction(double time, 
                   const std::string& region, 
                   const std::vector<double>& stateVec, 
                   double action, 
                   double reward, 
                   bool saturated);
  
  /**
   * Log learning metrics (REQUIRED: LogLearning)
   * @param time Simulation time
   * @param region Region ID
   * @param tdError TD error
   * @param criticLoss Critic loss
   * @param actorLoss Actor loss
   */
  void LogLearning(double time, 
                  const std::string& region, 
                  double tdError, 
                  double criticLoss, 
                  double actorLoss);
  
  /**
   * Log federated learning round (REQUIRED: LogFL)
   * @param time Simulation time
   * @param round FL round number
   * @param phase FL phase (e.g., "aggregation", "distribution")
   * @param avgReward Average reward across regions
   * @param modelNorm Model weight norm
   * @param divergence Model divergence
   */
  void LogFL(double time, 
            uint32_t round, 
            const std::string& phase, 
            double avgReward, 
            double modelNorm, 
            double divergence);
  
  // ============================================================================
  // DETAILED INTERFACE METHODS (extended functionality)
  // ============================================================================
  
  /**
   * Log congestion metrics for a region (called when MetricEngine updates)
   * Extended version with full snapshot
   */
  void LogCongestionMetrics(const std::string& region, 
                            const MetricSnapshot& snapshot,
                            double simTime);
  
  /**
   * Log DRL state for a region
   */
  void LogDRLState(const std::string& region,
                   const std::vector<double>& state,
                   double simTime);
  
  /**
   * Log DRL action for a region
   */
  void LogDRLAction(const std::string& region,
                    double actionRaw,
                    double actionClipped,
                    double rateFactor,
                    double sendRate,
                    const std::string& actionReason,
                    double simTime);
  
  /**
   * Log DRL learning metrics for a region
   */
  void LogDRLLearning(const std::string& region,
                      double reward,
                      double totalThroughput,
                      double queuePenalty,
                      double delayPenalty,
                      double lossPenalty,
                      double actorLoss,
                      double criticLoss,
                      double tdError,
                      double simTime);
  
  /**
   * Log federation round metrics
   */
  void LogFederationRound(uint32_t round,
                          double simTime,
                          const std::string& region,
                          double localUpdateNorm,
                          double globalModelNorm,
                          double modelDivergence,
                          double aggregationWeight,
                          bool participation);
  
  /**
   * Log fairness timeseries
   */
  void LogFairnessMetrics(double simTime,
                          const std::string& region,
                          double throughput,
                          double jainFairness,
                          double queueShare);
  
  /**
   * Write simulation configuration to JSON
   * @param config FDRLCC configuration
   * @param scenario Scenario number
   * @param simTime Simulation time
   * @param algorithm Algorithm name
   * @param seed Random seed
   * @param runNumber Run number
   */
  void WriteConfigJson(const FdrlccConfig& config,
                       uint32_t scenario,
                       double simTime,
                       const std::string& algorithm,
                       uint32_t seed,
                       uint32_t runNumber);

protected:
  void DoInitialize() override;
  void DoDispose() override;

private:
  /**
   * Periodic callback to read from MetricEngine and log
   */
  void PeriodicLog();
  
  /**
   * Ensure directory structure exists
   */
  void EnsureDirectoryStructure();
  
  /**
   * Initialize per-region CSV files
   */
  void InitializeRegionFiles(const std::string& region);
  
  /**
   * Flush all open files
   */
  void FlushAll();
  
  /**
   * Close all open files
   */
  void CloseAll();

private:
  std::string m_baseResultsDir;
  std::string m_logsDir;
  Ptr<MetricEngine> m_metricEngine;
  
  bool m_initialized;
  bool m_running;
  Time m_loggingInterval;
  EventId m_loggingEvent;
  
  // Per-region file streams
  struct RegionFiles {
    std::ofstream congestionFile;
    std::ofstream stateFile;
    std::ofstream actionFile;
    std::ofstream learningFile;
  };
  
  std::map<std::string, RegionFiles> m_regionFiles;
  
  // Global file streams
  std::ofstream m_federationFile;
  std::ofstream m_fairnessFile;
  std::ofstream m_eventsFile;  // General events log
  
  // Buffering control
  static constexpr size_t FLUSH_INTERVAL = 10;  // Flush every N writes
  size_t m_writeCount;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_STRUCTURED_LOGGER_HPP

