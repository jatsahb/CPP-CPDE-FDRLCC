#include "fdrl-performance-logger.hpp"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

NS_LOG_COMPONENT_DEFINE("ndn.FdrlPerformanceLogger");

namespace ns3 {
namespace ndn {
namespace fdrl {

PerformanceLogger& PerformanceLogger::GetInstance() {
  static PerformanceLogger instance;
  return instance;
}

PerformanceLogger::PerformanceLogger() {
}

PerformanceLogger::~PerformanceLogger() {
  Shutdown();
}

bool PerformanceLogger::Initialize(const std::string& resultsDir,
                                   const std::string& algorithm,
                                   uint32_t runId,
                                   const std::string& topology,
                                   uint32_t seed) {
  if (m_initialized) {
    NS_LOG_WARN("PerformanceLogger already initialized");
    return true;
  }
  
  m_resultsDir = resultsDir;
  m_algorithm = algorithm;
  m_runId = runId;
  m_topology = topology;
  m_seed = seed;
  
  // Create results directory if needed
  struct stat info;
  if (stat(resultsDir.c_str(), &info) != 0) {
#ifdef _WIN32
    _mkdir(resultsDir.c_str());
#else
    mkdir(resultsDir.c_str(), 0755);
#endif
    NS_LOG_INFO("Created results directory: " << resultsDir);
  }
  
  // Generate filename: performance_metrics_<ALGO>_run<id>.csv
  std::ostringstream filename;
  filename << resultsDir << "/performance_metrics_"
           << algorithm << "_run"
           << std::setfill('0') << std::setw(2) << runId << ".csv";
  
  // Open file
  m_file.open(filename.str(), std::ios::out | std::ios::trunc);
  if (!m_file.is_open()) {
    NS_LOG_ERROR("Failed to open performance metrics file: " << filename.str());
    return false;
  }
  
  // Write header comments
  WriteHeaderComments();
  
  // Write CSV header row
  WriteCSVHeader();
  
  m_initialized = true;
  NS_LOG_INFO("PerformanceLogger initialized: " << filename.str());
  return true;
}

void PerformanceLogger::WriteHeaderComments() {
  m_file << "# algorithm=" << m_algorithm << "\n";
  m_file << "# run_id=" << std::setfill('0') << std::setw(2) << m_runId << "\n";
  m_file << "# topology=" << m_topology << "\n";
  m_file << "# seed=" << m_seed << "\n";
  m_file << "# start_time=0.0\n";
  m_file << "# reward_window_size=10\n";
  m_file << "# convergence_threshold=positive_trend\n";
  m_file << "# divergence_threshold=0.1\n";
  m_file << "# policy_entropy=not_implemented\n";
  m_file.flush();
}

void PerformanceLogger::WriteCSVHeader() {
  m_file << "timestamp,node_id,algorithm_type,"
         << "throughput_mbps,e2e_latency_ms,jitter_ms,packet_loss_ratio,"
         << "interest_gen_rate,data_satisfaction_rate,"
         << "cs_hit_ratio,cs_utilization,pit_occupancy,fib_size,cnpi,"
         << "rl_action,reward,reward_avg,reward_variance,loss_value,"
         << "policy_entropy,convergence_indicator,divergence_indicator,training_step,"
         << "fl_round,global_model_divergence,model_update_norm,avg_local_reward,"
         << "communication_overhead_bytes,event_type\n";
  m_file.flush();
}

void PerformanceLogger::Shutdown() {
  if (!m_initialized) {
    return;
  }
  
  std::lock_guard<std::mutex> lock(m_fileMutex);
  
  if (m_file.is_open()) {
    m_file.flush();
    m_file.close();
  }
  
  m_initialized = false;
  NS_LOG_INFO("PerformanceLogger shutdown complete");
}

std::string PerformanceLogger::FormatValue(double value, int precision) {
  if (value < 0 && value == -1.0) {
    return "-1";
  }
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << value;
  return oss.str();
}

std::string PerformanceLogger::FormatValue(int value) {
  return std::to_string(value);
}

std::string PerformanceLogger::FormatValue(uint32_t value) {
  return std::to_string(value);
}

std::string PerformanceLogger::FormatValue(int64_t value) {
  return std::to_string(value);
}

std::string PerformanceLogger::FormatValue(const std::string& value) {
  if (value == "NA") {
    return "NA";
  }
  return value;
}

std::string PerformanceLogger::FormatValue(size_t value) {
  return std::to_string(value);
}

void PerformanceLogger::WriteRow(const std::string& row) {
  std::lock_guard<std::mutex> lock(m_fileMutex);
  if (m_file.is_open()) {
    m_file << row << "\n";
    // Flush every 10 rows for performance (or can flush immediately)
    static int rowCount = 0;
    if (++rowCount % 10 == 0) {
      m_file.flush();
    }
  }
}

void PerformanceLogger::LogCommonMetrics(const CommonMetrics& metrics) {
  if (!m_initialized) return;
  
  // Cache for console output
  m_lastCommonMetrics[metrics.nodeId] = metrics;
  
  // Build CSV row
  std::ostringstream row;
  row << FormatValue(metrics.timestamp, 3) << ","
      << FormatValue(metrics.nodeId) << ","
      << FormatValue(m_algorithm) << ","
      << FormatValue(metrics.throughputMbps, 2) << ","
      << FormatValue(metrics.e2eLatencyMs, 2) << ","
      << FormatValue(metrics.jitterMs, 2) << ","
      << FormatValue(metrics.packetLossRatio, 4) << ","
      << FormatValue(metrics.interestGenRate, 1) << ","
      << FormatValue(metrics.dataSatisfactionRate, 1) << ","
      << FormatValue(metrics.csHitRatio, 2) << ","
      << FormatValue(metrics.csUtilization, 2) << ","
      << FormatValue(metrics.pitOccupancy, 3) << ","
      << FormatValue(metrics.fibSize) << ","
      << FormatValue(metrics.cnpi, 2) << ","
      << "NA,NA,NA,NA,NA,NA,NA,NA,NA,"  // AI fields (NA for COMMON)
      << "NA,NA,NA,NA,NA,"  // FL fields (NA for COMMON)
      << "COMMON";
  
  WriteRow(row.str());
}

void PerformanceLogger::LogAIMetrics(const AIMetrics& metrics) {
  if (!m_initialized) return;
  
  // Cache for console output
  m_lastAIMetrics[metrics.nodeId] = metrics;
  
  // Get reward statistics from rolling window
  double rewardAvg = GetRewardAverage(metrics.nodeId);
  double rewardVar = GetRewardVariance(metrics.nodeId);
  int convergence = CalculateConvergenceIndicator(metrics.nodeId);
  
  // Divergence indicator: 0 for non-FL, or based on current FL divergence
  int divergence = 0;
  if (m_algorithm == "FDRL" || m_algorithm == "FDRLCC") {
    divergence = (m_currentDivergence > 0.1) ? 1 : 0;
  }
  
  // Build CSV row (include common metrics from cache if available)
  std::ostringstream row;
  row << FormatValue(metrics.timestamp, 3) << ","
      << FormatValue(metrics.nodeId) << ","
      << FormatValue(m_algorithm) << ",";
  
  // Common metrics (use cached values if available, else NA)
  if (m_lastCommonMetrics.find(metrics.nodeId) != m_lastCommonMetrics.end()) {
    const CommonMetrics& common = m_lastCommonMetrics[metrics.nodeId];
    row << FormatValue(common.throughputMbps, 2) << ","
        << FormatValue(common.e2eLatencyMs, 2) << ","
        << FormatValue(common.jitterMs, 2) << ","
        << FormatValue(common.packetLossRatio, 4) << ","
        << FormatValue(common.interestGenRate, 1) << ","
        << FormatValue(common.dataSatisfactionRate, 1) << ","
        << FormatValue(common.csHitRatio, 2) << ","
        << FormatValue(common.csUtilization, 2) << ","
        << FormatValue(common.pitOccupancy, 3) << ","
        << FormatValue(common.fibSize) << ","
        << FormatValue(common.cnpi, 2) << ",";
  } else {
    row << "NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,";  // Common metrics not available
  }
  
  // AI metrics
  row << FormatValue(metrics.rlAction, 3) << ","
      << FormatValue(metrics.reward, 3) << ","
      << FormatValue(rewardAvg, 3) << ","
      << FormatValue(rewardVar, 3) << ","
      << FormatValue(metrics.lossValue, 3) << ","
      << FormatValue(metrics.policyEntropy, 3) << ","
      << FormatValue(convergence) << ","
      << FormatValue(divergence) << ","
      << FormatValue(metrics.trainingStep) << ","
      << "NA,NA,NA,NA,NA,"  // FL fields (NA for AI events)
      << "AI";
  
  WriteRow(row.str());
}

void PerformanceLogger::LogFLMetrics(const FLMetrics& metrics) {
  if (!m_initialized) return;
  
  // Cache for console output
  m_lastFLTimestamp = metrics.timestamp;
  m_lastFLMetrics = metrics;
  
  // Update current divergence (propagate to AI metrics)
  m_currentDivergence = metrics.globalModelDivergence;
  
  // Build CSV row (FL event: node_id=NA, node fields=NA)
  std::ostringstream row;
  row << FormatValue(metrics.timestamp, 3) << ","
      << "NA,"  // node_id
      << FormatValue(m_algorithm) << ","
      << "NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,NA,"  // All common metrics (NA)
      << "NA,NA,NA,NA,NA,NA,NA,NA,NA,"  // All AI metrics (NA)
      << FormatValue(metrics.flRound) << ","
      << FormatValue(metrics.globalModelDivergence, 3) << ","
      << FormatValue(metrics.modelUpdateNorm, 3) << ","
      << FormatValue(metrics.avgLocalReward, 3) << ","
      << FormatValue(metrics.communicationOverheadBytes) << ","
      << "FL_AGG";
  
  WriteRow(row.str());
}

void PerformanceLogger::UpdateRewardHistory(const std::string& nodeId, double reward) {
  m_rewardWindows[nodeId].Add(reward);
}

double PerformanceLogger::GetRewardAverage(const std::string& nodeId) const {
  auto it = m_rewardWindows.find(nodeId);
  if (it == m_rewardWindows.end()) {
    return -1.0;
  }
  return it->second.GetAverage();
}

double PerformanceLogger::GetRewardVariance(const std::string& nodeId) const {
  auto it = m_rewardWindows.find(nodeId);
  if (it == m_rewardWindows.end()) {
    return -1.0;
  }
  return it->second.GetVariance();
}

int PerformanceLogger::CalculateConvergenceIndicator(const std::string& nodeId) const {
  auto it = m_rewardWindows.find(nodeId);
  if (it == m_rewardWindows.end() || it->second.samples.size() < 10) {
    return 0;
  }
  
  const auto& samples = it->second.samples;
  
  // Method 1: Linear regression slope
  double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
  size_t n = samples.size();
  
  for (size_t i = 0; i < n; ++i) {
    double x = static_cast<double>(i);
    double y = samples[i];
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }
  
  double slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
  
  // Method 2: Compare recent vs early rewards
  double recentAvg = 0.0, earlyAvg = 0.0;
  size_t window = 3;
  if (n >= window * 2) {
    for (size_t i = n - window; i < n; ++i) {
      recentAvg += samples[i];
    }
    recentAvg /= window;
    
    for (size_t i = 0; i < window; ++i) {
      earlyAvg += samples[i];
    }
    earlyAvg /= window;
  }
  
  // Convergence if: positive slope OR recent > early * 1.1
  if (slope > 0 || (recentAvg > 0 && earlyAvg > 0 && recentAvg > earlyAvg * 1.1)) {
    return 1;
  }
  
  return 0;
}

void PerformanceLogger::UpdateDivergenceIndicator(double flDivergence) {
  m_currentDivergence = flDivergence;
}

double PerformanceLogger::GetCurrentDivergence() const {
  return m_currentDivergence;
}

void PerformanceLogger::SetConsoleInterval(double interval) {
  m_consoleInterval = interval;
}

void PerformanceLogger::PrintConsoleOutput(const std::string& algorithm, bool enableAI) {
  double currentTime = Simulator::Now().GetSeconds();
  
  // Check if it's time to print (every consoleInterval seconds)
  if (currentTime - m_lastConsolePrint < m_consoleInterval) {
    return;
  }
  
  m_lastConsolePrint = currentTime;
  
  if (m_consoleInterval <= 0) {
    return;  // Console output disabled
  }
  
  // Print for each node
  for (const auto& [nodeId, common] : m_lastCommonMetrics) {
    if (common.timestamp < currentTime - 1.0) {
      continue;  // Skip stale metrics
    }
    
    // Non-AI algorithm format
    if (!enableAI || algorithm == "CUBIC" || algorithm == "AIMD" || algorithm == "BIC") {
      std::cout << "[t=" << std::fixed << std::setprecision(0) << currentTime
                << "s] " << nodeId << " | "
                << "Thr=" << std::setprecision(2) << common.throughputMbps << "Mbps | "
                << "Lat=" << std::setprecision(1) << common.e2eLatencyMs << "ms | "
                << "Jit=" << std::setprecision(1) << common.jitterMs << "ms | "
                << "PLR=" << std::setprecision(1) << (common.packetLossRatio * 100.0) << "%\n";
      
      std::cout << "          "
                << "IGR/DSR=" << std::setprecision(1) << common.interestGenRate << "/s | "
                << "CSHR=" << std::setprecision(1) << (common.csHitRatio * 100.0) << "% | "
                << "PIT=" << std::setprecision(1) << (common.pitOccupancy * 100.0) << "% | "
                << "CNPI=" << std::setprecision(2) << common.cnpi << "\n";
    } else {
      // AI algorithm format
      auto aiIt = m_lastAIMetrics.find(nodeId);
      if (aiIt != m_lastAIMetrics.end()) {
        const AIMetrics& ai = aiIt->second;
        
        std::cout << "[t=" << std::fixed << std::setprecision(0) << currentTime
                  << "s] " << nodeId << " | "
                  << "Thr=" << std::setprecision(2) << common.throughputMbps << "Mbps | "
                  << "Lat=" << std::setprecision(1) << common.e2eLatencyMs << "ms | "
                  << "PLR=" << std::setprecision(1) << (common.packetLossRatio * 100.0) << "% | "
                  << "CNPI=" << std::setprecision(2) << common.cnpi << "\n";
        
        std::cout << "          RL["
                  << "action=" << std::showpos << std::setprecision(2) << (ai.rlAction - 1.0)
                  << std::noshowpos << " | "
                  << "reward=" << std::setprecision(2) << ai.reward << " | "
                  << "loss=" << std::setprecision(3) << (ai.lossValue > 0 ? ai.lossValue : 0.0) << " | "
                  << "conv=" << ai.convergenceIndicator << "]\n";
      }
    }
  }
  
  // Print FL aggregation event if recent
  if (enableAI && (algorithm == "FDRL" || algorithm == "FDRLCC")) {
    if (m_lastFLTimestamp > 0 && currentTime - m_lastFLTimestamp < 2.0) {
      std::cout << "[t=" << std::fixed << std::setprecision(0) << currentTime
                << "s] FL | "
                << "Round=" << m_lastFLMetrics.flRound << " | "
                << "Nodes=" << m_lastCommonMetrics.size() << " | "
                << "Divergence=" << std::setprecision(3) << m_lastFLMetrics.globalModelDivergence << " | "
                << "AvgReward=" << std::setprecision(2) << m_lastFLMetrics.avgLocalReward << "\n";
    }
  }
  
  std::cout.flush();
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

