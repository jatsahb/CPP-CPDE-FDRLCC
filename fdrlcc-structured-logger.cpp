/**
 * fdrlcc-structured-logger.cpp
 * 
 * Implementation of structured logging subsystem for FDRLCC experiments
 */

#include "fdrlcc-structured-logger.hpp"
#include "fdrlcc-types.hpp"
#include "src_cpp/metrics/metric-engine.hpp"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <cmath>

NS_LOG_COMPONENT_DEFINE("FdrlccStructuredLogger");

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_OBJECT_ENSURE_REGISTERED(StructuredLogger);

TypeId
StructuredLogger::GetTypeId()
{
  static TypeId tid = TypeId("ns3::ndn::fdrl::StructuredLogger")
    .SetParent<Object>()
    .AddConstructor<StructuredLogger>();
  return tid;
}

StructuredLogger::StructuredLogger()
  : m_initialized(false)
  , m_running(false)
  , m_loggingInterval(Seconds(1.0))
  , m_writeCount(0)
{
}

StructuredLogger::~StructuredLogger()
{
  Shutdown();
}

void
StructuredLogger::DoInitialize()
{
  Object::DoInitialize();
}

void
StructuredLogger::DoDispose()
{
  Shutdown();
  Object::DoDispose();
}

bool
StructuredLogger::Initialize(const std::string& resultsDir, 
                              Ptr<MetricEngine> metricEngine)
{
  if (m_initialized) {
    NS_LOG_WARN("StructuredLogger already initialized");
    return true;
  }
  
  m_baseResultsDir = resultsDir;
  m_logsDir = resultsDir + "/logs";
  m_metricEngine = metricEngine;
  
  // Ensure directory structure exists
  EnsureDirectoryStructure();
  
  // Write config JSON (must be done before region files to capture config)
  // Note: Config will be written later when WriteConfigJson is called
  
  m_initialized = true;
  NS_LOG_INFO("StructuredLogger initialized with directory: " << m_logsDir);
  return true;
}

void
StructuredLogger::Shutdown()
{
  if (!m_initialized) {
    return;
  }
  
  Stop();
  CloseAll();
  
  m_initialized = false;
  NS_LOG_INFO("StructuredLogger shutdown complete");
}

void
StructuredLogger::Start(Time interval)
{
  if (!m_initialized || m_running) {
    return;
  }
  
  m_loggingInterval = interval;
  m_running = true;
  
  // Initialize region files for all known regions
  if (g_topologyInfo.regions.empty()) {
    NS_LOG_WARN("StructuredLogger: No regions available yet, will initialize later");
  } else {
    for (const auto& region : g_topologyInfo.regions) {
      InitializeRegionFiles(region);
    }
  }
  
  // Initialize global files
  std::string federationPath = m_logsDir + "/federation/federation_rounds.csv";
  m_federationFile.open(federationPath, std::ios::out | std::ios::trunc);
  if (m_federationFile.is_open()) {
    m_federationFile << "round,time,region,local_update_norm,global_model_norm,model_divergence,aggregation_weight,participation" << std::endl;
  }
  
  std::string fairnessPath = m_logsDir + "/fairness/fairness_timeseries.csv";
  m_fairnessFile.open(fairnessPath, std::ios::out | std::ios::trunc);
  if (m_fairnessFile.is_open()) {
    m_fairnessFile << "time,region,throughput,jain_fairness,queue_share" << std::endl;
  }
  
  // Start periodic logging
  PeriodicLog();
  
  NS_LOG_INFO("StructuredLogger started with interval " << interval.GetSeconds() << "s");
}

void
StructuredLogger::Stop()
{
  if (!m_running) {
    return;
  }
  
  m_running = false;
  if (m_loggingEvent.IsRunning()) {
    m_loggingEvent.Cancel();
  }
  
  NS_LOG_INFO("StructuredLogger stopped");
}

void
StructuredLogger::EnsureDirectoryStructure()
{
  // Create base logs directory
  struct stat info;
  if (stat(m_logsDir.c_str(), &info) != 0) {
    #ifdef _WIN32
    _mkdir(m_logsDir.c_str());
    #else
    mkdir(m_logsDir.c_str(), 0755);
    #endif
  }
  
  // Create subdirectories
  std::vector<std::string> subdirs = {
    "congestion",
    "drl",
    "federation",
    "fairness",
    "metadata"
  };
  
  for (const auto& subdir : subdirs) {
    std::string path = m_logsDir + "/" + subdir;
    if (stat(path.c_str(), &info) != 0) {
      #ifdef _WIN32
      _mkdir(path.c_str());
      #else
      mkdir(path.c_str(), 0755);
      #endif
    }
  }
}

void
StructuredLogger::InitializeRegionFiles(const std::string& region)
{
  if (m_regionFiles.find(region) != m_regionFiles.end()) {
    // Already initialized
    return;
  }
  
  RegionFiles files;
  
  // Congestion CSV
  std::string congestionPath = m_logsDir + "/congestion/congestion_" + region + ".csv";
  files.congestionFile.open(congestionPath, std::ios::out | std::ios::trunc);
  if (files.congestionFile.is_open()) {
    files.congestionFile << "time,region,queue_len,queue_util,pit_size,rtt_ms,loss_rate,nack_rate,link_util" << std::endl;
  }
  
  // STEP 1 & 2: DISABLED - Redundant/Debug-only logging removed for efficiency
  // DRL State CSV - DISABLED (debug only, raw states not needed for paper)
  // Raw state vectors are not used in any paper figure.
  // Derived metrics exist in summary.csv and congestion logs.
  // std::string statePath = m_logsDir + "/drl/state_" + region + ".csv";
  // files.stateFile.open(statePath, std::ios::out | std::ios::trunc);
  // if (files.stateFile.is_open()) {
  //   files.stateFile << "time,region,s0_queue,s1_rtt,s2_loss,s3_pit,s4_prev_action" << std::endl;
  // }
  
  // STEP 3: DRL Action CSV with causal logging (congestion_flag_at_action_time)
  // KEPT: Required for paper evidence (Congestion Events vs Action Intensity plot)
  std::string actionPath = m_logsDir + "/drl/action_" + region + ".csv";
  files.actionFile.open(actionPath, std::ios::out | std::ios::trunc);
  if (files.actionFile.is_open()) {
    files.actionFile << "time,region,action_raw,action_clipped,rate_factor,send_rate,action_reason,action_intensity,congestion_flag_at_action_time" << std::endl;
  }
  
  // STEP 1: DISABLED - Redundant learning log (duplicates training_metrics.csv)
  // DRL Learning CSV - DISABLED (fully duplicates training_metrics.csv)
  // No unique information, not required for any paper plot.
  // All learning metrics (td_error, critic_loss, actor_loss) are in training_metrics.csv
  // std::string learningPath = m_logsDir + "/drl/learning_" + region + ".csv";
  // files.learningFile.open(learningPath, std::ios::out | std::ios::trunc);
  // if (files.learningFile.is_open()) {
  //   files.learningFile << "time,region,reward,total_throughput,queue_penalty,delay_penalty,loss_penalty,actor_loss,critic_loss,td_error" << std::endl;
  // }
  
  m_regionFiles[region] = std::move(files);
}

void
StructuredLogger::PeriodicLog()
{
  if (!m_running || !m_metricEngine) {
    return;
  }
  
  double simTime = Simulator::Now().GetSeconds();
  
  // Log congestion metrics for all regions
  for (const auto& region : g_topologyInfo.regions) {
    if (!m_metricEngine->IsRegionInitialized(region)) {
      continue;
    }
    
    const MetricSnapshot& snapshot = m_metricEngine->GetLatestSnapshot(region);
    LogCongestionMetrics(region, snapshot, simTime);
    
    // STEP 2: DISABLED - Raw state logging removed (debug only)
    // Raw state vectors not needed for paper (derived metrics in summary.csv suffice)
    // if (g_regionDRL.find(region) != g_regionDRL.end()) {
    //   const auto& drl = g_regionDRL[region];
    //   LogDRLState(region, drl.state, simTime);
    // }
  }
  
  // Schedule next logging
  m_loggingEvent = Simulator::Schedule(m_loggingInterval, 
                                        &StructuredLogger::PeriodicLog, 
                                        this);
}

void
StructuredLogger::LogCongestionMetrics(const std::string& region, 
                                        const MetricSnapshot& snapshot,
                                        double simTime)
{
  auto it = m_regionFiles.find(region);
  if (it == m_regionFiles.end() || !it->second.congestionFile.is_open()) {
    return;
  }
  
  // Extract PIT size from topology (if available) - placeholder for now
  // Note: PIT size, NACK rate, link util would need to be added to MetricSnapshot or computed separately
  double pitSize = 0.0;  // TODO: Get from router metrics if available
  double nackRate = 0.0;  // TODO: Get from router metrics if available
  double linkUtil = 0.0;  // TODO: Get from router metrics if available
  
  // Calculate loss rate from snapshot
  double lossRate = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    lossRate = static_cast<double>(snapshot.totalPacketsDropped) / 
               static_cast<double>(snapshot.totalInterestsSent);
  }
  
  // Queue utilization is same as queueOccupancy (already normalized 0-1)
  double queueUtil = snapshot.queueOccupancy;
  
  // Queue length approximation (denormalize if needed)
  // Assuming MAX_PENDING_INTERESTS = 1000 for normalization
  double queueLen = snapshot.pendingInterests;
  
  it->second.congestionFile << std::fixed << std::setprecision(6)
                           << simTime << ","
                           << region << ","
                           << queueLen << ","
                           << queueUtil << ","
                           << pitSize << ","
                           << snapshot.avgDelayMs << ","
                           << lossRate << ","
                           << nackRate << ","
                           << linkUtil << std::endl;
}

void
StructuredLogger::LogDRLState(const std::string& region,
                              const std::vector<double>& state,
                              double simTime)
{
  // STEP 2: DISABLED - Raw state logging removed (debug only)
  // Raw state vectors are not used in any paper figure.
  // Derived metrics exist in summary.csv and congestion logs.
  // This function is kept for API compatibility but no longer writes to disk.
  
  // DISABLED: No longer writing to state_*.csv
  // auto it = m_regionFiles.find(region);
  // if (it == m_regionFiles.end() || !it->second.stateFile.is_open()) {
  //   return;
  // }
  // ... (logging code removed)
  // it->second.stateFile << std::fixed << std::setprecision(6)
  //                     << simTime << ","
  //                     << region << ","
  //                     << s0_queue << ","
  //                     << s1_rtt << ","
  //                     << s2_loss << ","
  //                     << s3_pit << ","
  //                     << s4_prev_action << std::endl;
}

void
StructuredLogger::LogDRLAction(const std::string& region,
                                double actionRaw,
                                double actionClipped,
                                double rateFactor,
                                double sendRate,
                                const std::string& actionReason,
                                double simTime)
{
  auto it = m_regionFiles.find(region);
  if (it == m_regionFiles.end() || !it->second.actionFile.is_open()) {
    return;
  }
  
  // STEP 3: Calculate action_intensity and get congestion_flag_at_action_time
  double actionIntensity = std::abs(rateFactor - 1.0);  // Distance from neutral (1.0)
  
  // STEP 3: Get congestion_flag_at_action_time from MetricEngine (ground truth)
  bool congestionFlagAtActionTime = false;
  if (m_metricEngine && m_metricEngine->IsRegionInitialized(region)) {
    const MetricSnapshot& snapshot = m_metricEngine->GetLatestSnapshot(region);
    // Use same thresholds as summary.csv for consistency
    const double QUEUE_THRESHOLD = 0.8;
    const double DELAY_THRESHOLD = 100.0;  // ms
    const double LOSS_THRESHOLD = 0.15;
    
    double lossRate = 0.0;
    if (snapshot.totalInterestsSent > 0) {
      lossRate = static_cast<double>(snapshot.totalPacketsDropped) / 
                 static_cast<double>(snapshot.totalInterestsSent);
    }
    
    congestionFlagAtActionTime = (snapshot.queueOccupancy > QUEUE_THRESHOLD) ||
                                  (snapshot.avgDelayMs > DELAY_THRESHOLD) ||
                                  (lossRate > LOSS_THRESHOLD);
  }
  
  it->second.actionFile << std::fixed << std::setprecision(6)
                       << simTime << ","
                       << region << ","
                       << actionRaw << ","
                       << actionClipped << ","
                       << rateFactor << ","
                       << sendRate << ","
                       << actionReason << ","
                       << actionIntensity << ","  // action_intensity (STEP 3)
                       << (congestionFlagAtActionTime ? 1 : 0) << std::endl;  // congestion_flag_at_action_time (STEP 3)
  
  // Buffered I/O
  m_writeCount++;
  if (m_writeCount % FLUSH_INTERVAL == 0) {
    it->second.actionFile.flush();
  }
}

void
StructuredLogger::LogDRLLearning(const std::string& region,
                                  double reward,
                                  double totalThroughput,
                                  double queuePenalty,
                                  double delayPenalty,
                                  double lossPenalty,
                                  double actorLoss,
                                  double criticLoss,
                                  double tdError,
                                  double simTime)
{
  auto it = m_regionFiles.find(region);
  if (it == m_regionFiles.end() || !it->second.learningFile.is_open()) {
    return;
  }
  
  it->second.learningFile << std::fixed << std::setprecision(6)
                         << simTime << ","
                         << region << ","
                         << reward << ","
                         << totalThroughput << ","
                         << queuePenalty << ","
                         << delayPenalty << ","
                         << lossPenalty << ","
                         << actorLoss << ","
                         << criticLoss << ","
                         << tdError << std::endl;
}

void
StructuredLogger::LogFederationRound(uint32_t round,
                                      double simTime,
                                      const std::string& region,
                                      double localUpdateNorm,
                                      double globalModelNorm,
                                      double modelDivergence,
                                      double aggregationWeight,
                                      bool participation)
{
  if (!m_federationFile.is_open()) {
    return;
  }
  
  m_federationFile << std::fixed << std::setprecision(6)
                   << round << ","
                   << simTime << ","
                   << region << ","
                   << localUpdateNorm << ","
                   << globalModelNorm << ","
                   << modelDivergence << ","
                   << aggregationWeight << ","
                   << (participation ? 1 : 0) << std::endl;
}

void
StructuredLogger::LogFairnessMetrics(double simTime,
                                      const std::string& region,
                                      double throughput,
                                      double jainFairness,
                                      double queueShare)
{
  if (!m_fairnessFile.is_open()) {
    return;
  }
  
  m_fairnessFile << std::fixed << std::setprecision(6)
                 << simTime << ","
                 << region << ","
                 << throughput << ","
                 << jainFairness << ","
                 << queueShare << std::endl;
}

void
StructuredLogger::WriteConfigJson(const FdrlccConfig& config,
                                   uint32_t scenario,
                                   double simTime,
                                   const std::string& algorithm,
                                   uint32_t seed,
                                   uint32_t runNumber)
{
  std::string configPath = m_logsDir + "/metadata/simulation_config.json";
  std::ofstream configFile(configPath, std::ios::out | std::ios::trunc);
  
  if (!configFile.is_open()) {
    NS_LOG_ERROR("Failed to open config JSON file: " << configPath);
    return;
  }
  
  // Write JSON config (simple format, no external JSON library needed)
  configFile << "{\n";
  configFile << "  \"simulation\": {\n";
  configFile << "    \"scenario\": " << scenario << ",\n";
  configFile << "    \"simulation_time\": " << std::fixed << std::setprecision(2) << simTime << ",\n";
  configFile << "    \"algorithm\": \"" << algorithm << "\",\n";
  configFile << "    \"seed\": " << seed << ",\n";
  configFile << "    \"run_number\": " << runNumber << "\n";
  configFile << "  },\n";
  configFile << "  \"fdrlcc_config\": {\n";
  configFile << "    \"fl_interval\": " << std::fixed << std::setprecision(2) << config.flInterval << ",\n";
  configFile << "    \"replay_warmup_size\": " << config.replayWarmupSize << ",\n";
  configFile << "    \"train_every_n_steps\": " << config.trainEveryNSteps << ",\n";
  configFile << "    \"training_batch_size\": " << config.trainingBatchSize << ",\n";
  configFile << "    \"min_exploration_noise\": " << std::fixed << std::setprecision(4) << config.minExplorationNoise << ",\n";
  configFile << "    \"gamma\": " << std::fixed << std::setprecision(4) << config.gamma << ",\n";
  configFile << "    \"metric_collection_interval\": " << std::fixed << std::setprecision(2) << config.metricCollectionInterval << ",\n";
  configFile << "    \"action_interval\": " << std::fixed << std::setprecision(2) << config.actionInterval << ",\n";
  configFile << "    \"min_action\": " << std::fixed << std::setprecision(2) << config.minAction << ",\n";
  configFile << "    \"max_action\": " << std::fixed << std::setprecision(2) << config.maxAction << "\n";
  configFile << "  },\n";
  configFile << "  \"topology\": {\n";
  configFile << "    \"regions\": [\n";
  for (size_t i = 0; i < g_topologyInfo.regions.size(); ++i) {
    configFile << "      \"" << g_topologyInfo.regions[i] << "\"";
    if (i < g_topologyInfo.regions.size() - 1) {
      configFile << ",";
    }
    configFile << "\n";
  }
  configFile << "    ]\n";
  configFile << "  }\n";
  configFile << "}\n";
  
  configFile.close();
  NS_LOG_INFO("Simulation config written to: " << configPath);
}

// ============================================================================
// REQUIRED INTERFACE METHODS IMPLEMENTATION
// ============================================================================

void
StructuredLogger::LogEvent(double time, const std::string& eventType, const std::string& details)
{
  // CSV logging (always log to CSV)
  if (!m_eventsFile.is_open()) {
    std::string eventsPath = m_logsDir + "/events.csv";
    m_eventsFile.open(eventsPath, std::ios::out | std::ios::trunc);
    if (m_eventsFile.is_open()) {
      m_eventsFile << "time,event_type,details" << std::endl;
    }
  }
  
  if (m_eventsFile.is_open()) {
    m_eventsFile << std::fixed << std::setprecision(6)
                 << time << ","
                 << eventType << ","
                 << "\"" << details << "\"" << std::endl;
    
    // Buffered I/O - flush periodically
    m_writeCount++;
    if (m_writeCount % FLUSH_INTERVAL == 0) {
      m_eventsFile.flush();
    }
  }
  
  // Console logging: Only high-level events (timeline narrative)
  // Filter to only show: PHASE_TRANSITION, CONGESTION_ENTER, CONGESTION_EXIT, FL round events
  bool isHighLevelEvent = (eventType == "PHASE_TRANSITION" || 
                           eventType == "PHASE_INIT" ||
                           eventType == "CONGESTION_ENTER" || 
                           eventType == "CONGESTION_EXIT" ||
                           eventType.find("FL_") == 0);  // FL_* events
  
  if (isHighLevelEvent) {
    // Narrative-style console output
    if (eventType == "PHASE_TRANSITION" || eventType == "PHASE_INIT") {
      std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] " 
                << details << std::endl;
    } else if (eventType == "CONGESTION_ENTER") {
      std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] ⚠️  Congestion detected: " 
                << details << std::endl;
    } else if (eventType == "CONGESTION_EXIT") {
      std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] ✓  Congestion cleared: " 
                << details << std::endl;
    } else if (eventType.find("FL_") == 0) {
      // FL events - already handled by LogFL, but log here for completeness
      std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] " 
                << details << std::endl;
    }
  }
  // All other events: CSV only, no console output
}

void
StructuredLogger::LogCongestion(double time, 
                               const std::string& region, 
                               double queue, 
                               double delay, 
                               double loss,
                               const std::vector<double>& state)
{
  // Ensure region files are initialized
  if (m_regionFiles.find(region) == m_regionFiles.end()) {
    InitializeRegionFiles(region);
  }
  
  auto it = m_regionFiles.find(region);
  if (it == m_regionFiles.end() || !it->second.congestionFile.is_open()) {
    return;
  }
  
  // Use simplified congestion logging (matches required interface)
  // queue = queue_len, delay = rtt_ms, loss = loss_rate
  // Other fields (pit_size, nack_rate, link_util) set to 0.0
  it->second.congestionFile << std::fixed << std::setprecision(6)
                           << time << ","
                           << region << ","
                           << queue << ","  // queue_len
                           << queue << ","  // queue_util (use same value, caller can normalize)
                           << 0.0 << ","    // pit_size (not provided)
                           << delay << ","  // rtt_ms
                           << loss << ","   // loss_rate
                           << 0.0 << ","    // nack_rate (not provided)
                           << 0.0 << std::endl;  // link_util (not provided)
  
  // Buffered I/O
  m_writeCount++;
  if (m_writeCount % FLUSH_INTERVAL == 0) {
    it->second.congestionFile.flush();
  }
}

void
StructuredLogger::LogDRLAction(double time, 
                               const std::string& region, 
                               const std::vector<double>& stateVec, 
                               double action, 
                               double reward, 
                               bool saturated)
{
  // Ensure region files are initialized
  if (m_regionFiles.find(region) == m_regionFiles.end()) {
    InitializeRegionFiles(region);
  }
  
  auto it = m_regionFiles.find(region);
  if (it == m_regionFiles.end() || !it->second.actionFile.is_open()) {
    return;
  }
  
  // STEP 3: Determine action_reason and action_intensity for causal analysis
  std::string actionReason = saturated ? "SATURATED" : "NORMAL";
  
  // Determine action_intensity: how much change from neutral (1.0)
  double actionIntensity = std::abs(action - 1.0);
  
  // Determine action_reason based on action value
  if (action < 1.0) {
    actionReason = "DECREASE";  // Rate reduction
  } else if (action > 1.0) {
    actionReason = "INCREASE";  // Rate increase
  } else {
    actionReason = "HOLD";      // No change
  }
  
  // STEP 3: Get congestion_flag_at_action_time from MetricEngine (ground truth)
  bool congestionFlagAtActionTime = false;
  if (m_metricEngine && m_metricEngine->IsRegionInitialized(region)) {
    const MetricSnapshot& snapshot = m_metricEngine->GetLatestSnapshot(region);
    // Use same thresholds as summary.csv for consistency
    const double QUEUE_THRESHOLD = 0.8;
    const double DELAY_THRESHOLD = 100.0;  // ms
    const double LOSS_THRESHOLD = 0.15;
    
    double lossRate = 0.0;
    if (snapshot.totalInterestsSent > 0) {
      lossRate = static_cast<double>(snapshot.totalPacketsDropped) / 
                 static_cast<double>(snapshot.totalInterestsSent);
    }
    
    congestionFlagAtActionTime = (snapshot.queueOccupancy > QUEUE_THRESHOLD) ||
                                  (snapshot.avgDelayMs > DELAY_THRESHOLD) ||
                                  (lossRate > LOSS_THRESHOLD);
  }
  
  it->second.actionFile << std::fixed << std::setprecision(6)
                       << time << ","
                       << region << ","
                       << action << ","  // action_raw
                       << action << ","  // action_clipped
                       << action << ","  // rate_factor
                       << 0.0 << ","     // send_rate (not provided in required interface)
                       << actionReason << ","  // action_reason (increase/decrease/hold)
                       << actionIntensity << ","  // action_intensity (STEP 3)
                       << (congestionFlagAtActionTime ? 1 : 0) << std::endl;  // congestion_flag_at_action_time (STEP 3)
  
  // Buffered I/O
  m_writeCount++;
  if (m_writeCount % FLUSH_INTERVAL == 0) {
    it->second.actionFile.flush();
  }
  
  // Console logging: REMOVED - detailed DRL actions go to CSV only
  // Per-step raw metrics should not be printed to console
}

void
StructuredLogger::LogLearning(double time, 
                             const std::string& region, 
                             double tdError, 
                             double criticLoss, 
                             double actorLoss)
{
  // STEP 1: DISABLED - Redundant logging removed for efficiency
  // This function is kept for API compatibility but no longer writes to disk.
  // All learning metrics (td_error, critic_loss, actor_loss) are already logged
  // in training_metrics.csv, which is the authoritative source for paper plots.
  // 
  // Redundant: data already logged in training_metrics.csv
  // No unique information, not required for any paper plot.
  
  // Ensure region files are initialized (for other files like action_*.csv)
  if (m_regionFiles.find(region) == m_regionFiles.end()) {
    InitializeRegionFiles(region);
  }
  
  // DISABLED: No longer writing to learning_*.csv
  // auto it = m_regionFiles.find(region);
  // if (it == m_regionFiles.end() || !it->second.learningFile.is_open()) {
  //   return;
  // }
  // ... (logging code removed)
  
  // Console logging: REMOVED - detailed learning metrics go to CSV only
  // Per-step raw metrics should not be printed to console
}

void
StructuredLogger::LogFL(double time, 
                       uint32_t round, 
                       const std::string& phase, 
                       double avgReward, 
                       double modelNorm, 
                       double divergence)
{
  if (!m_federationFile.is_open()) {
    return;
  }
  
  // Use simplified FL logging (matches required interface)
  // Round-level logging (region = "GLOBAL" for round-level metrics)
  // Note: Original LogFederationRound is per-region, this is per-round
  m_federationFile << std::fixed << std::setprecision(6)
                   << round << ","
                   << time << ","
                   << "GLOBAL" << ","  // region (global for round-level)
                   << modelNorm << "," // local_update_norm (use modelNorm)
                   << modelNorm << "," // global_model_norm (use modelNorm)
                   << divergence << "," // model_divergence
                   << 1.0 << ","       // aggregation_weight (not provided)
                   << 1 << std::endl;  // participation (always 1 for round-level)
  
  // Buffered I/O
  m_writeCount++;
  if (m_writeCount % FLUSH_INTERVAL == 0) {
    m_federationFile.flush();
  }
  
  // Console logging: Only high-level FL round events (timeline narrative)
  // Show only PRE (round start) and POST (round end) phases
  if (phase == "PRE") {
    // FL round start - narrative style
    std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] 🔄 FL Round " << round 
              << " starting (avg reward: " << std::setprecision(3) << avgReward 
              << ", divergence: " << std::setprecision(4) << divergence << ")" << std::endl;
  } else if (phase == "POST") {
    // FL round end - narrative style
    std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] ✓  FL Round " << round 
              << " completed (avg reward: " << std::setprecision(3) << avgReward 
              << ", model norm: " << std::setprecision(4) << modelNorm 
              << ", divergence: " << std::setprecision(4) << divergence << ")" << std::endl;
  }
  // All other phases: CSV only, no console output
}

void
StructuredLogger::FlushAll()
{
  for (auto& [region, files] : m_regionFiles) {
    if (files.congestionFile.is_open()) files.congestionFile.flush();
    // STEP 2: DISABLED - state_*.csv removed
    // if (files.stateFile.is_open()) files.stateFile.flush();
    if (files.actionFile.is_open()) files.actionFile.flush();
    // STEP 1: DISABLED - learning_*.csv removed
    // if (files.learningFile.is_open()) files.learningFile.flush();
  }
  
  if (m_federationFile.is_open()) m_federationFile.flush();
  if (m_fairnessFile.is_open()) m_fairnessFile.flush();
  if (m_eventsFile.is_open()) m_eventsFile.flush();
}

void
StructuredLogger::CloseAll()
{
  FlushAll();
  
  for (auto& [region, files] : m_regionFiles) {
    if (files.congestionFile.is_open()) files.congestionFile.close();
    // STEP 2: DISABLED - state_*.csv removed
    // if (files.stateFile.is_open()) files.stateFile.close();
    if (files.actionFile.is_open()) files.actionFile.close();
    // STEP 1: DISABLED - learning_*.csv removed
    // if (files.learningFile.is_open()) files.learningFile.close();
  }
  
  if (m_federationFile.is_open()) m_federationFile.close();
  if (m_fairnessFile.is_open()) m_fairnessFile.close();
  if (m_eventsFile.is_open()) m_eventsFile.close();
  
  m_regionFiles.clear();
  m_writeCount = 0;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

