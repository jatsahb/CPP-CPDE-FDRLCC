/**
 * fdrlcc_Results.hpp
 * 
 * PhD Defense Framework: Per-second recording, NPI/CNPI calculation, and required file formats
 */

#ifndef FDRLCC_RESULTS_HPP
#define FDRLCC_RESULTS_HPP

#include <string>
#include <fstream>
#include <map>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Per-second metrics structures
struct NetworkMetrics1s {
  double throughput_mbps = 0.0;
  double avg_queue_delay_ms = 0.0;
  double packet_loss_ratio = 0.0;
  double interest_rate = 0.0;
  double data_rate = 0.0;
};

struct LearningMetrics1s {
  std::string region_id;
  double actor_action = 0.0;
  double critic_q_value = 0.0;
  double reward = 0.0;
  double reward_smoothed = 0.0;
  double exploration_noise = 0.0;
  double gradient_norm = 0.0;
  bool gradient_clipped = false;
};

struct Aggregate5s {
  double avg_throughput = 0.0;
  double avg_delay = 0.0;
  double loss_ratio = 0.0;
  double fairness = 0.0;
  double avg_reward = 0.0;
  double npi = 0.0;  // Normalized Performance Index
  double cnpi = 0.0; // Composite NPI
  uint32_t sample_count = 0;  // Number of samples accumulated
};

// NPI weights (sum = 1.0)
struct NPIWeights {
  double throughput = 0.2;
  double delay = 0.1;
  double loss = 0.05;
  double fairness = 0.1;
  double reward = 0.25;
  double q_value = 0.05;
  double fl_loss = 0.15;
  double fl_div = 0.1;
};

// Normalization constants
struct NPINormalization {
  double T_MAX = 100.0;      // Max throughput Mbps
  double D_MAX = 50.0;       // Worst delay ms
  double L_MAX = 0.01;       // Max loss ratio (1%)
  double LOSS_FL_MAX = 1.0;  // Max local loss
  double DIV_MAX = 0.01;     // Max model divergence
};

// Global CSV file streams (defined in fdrlcc_Results.cpp)
extern std::ofstream g_network1sCsv;
extern std::ofstream g_learning1sCsv;
extern std::ofstream g_aggregate5sCsv;

// Per-second recording state
extern std::map<std::string, NetworkMetrics1s> g_lastNetworkMetrics;
extern std::map<std::string, LearningMetrics1s> g_lastLearningMetrics;
extern std::map<std::string, Aggregate5s> g_aggregateBuffer;  // Buffer for 5s aggregation

// NPI calculation state
extern NPIWeights g_npiWeights;
extern NPINormalization g_npiNorm;

/**
 * Initialize PhD Defense Framework files
 */
void InitializePhdFrameworkFiles(const std::string& resultsDir, uint32_t scenario, uint32_t runNumber);

/**
 * Close PhD Defense Framework files
 */
void ClosePhdFrameworkFiles();

/**
 * Record network metrics per second
 */
void RecordNetworkMetrics1s(double time_sec, const std::string& region, const NetworkMetrics1s& metrics);

/**
 * Record learning metrics per second
 */
void RecordLearningMetrics1s(double time_sec, const std::string& region, const LearningMetrics1s& metrics);

/**
 * Calculate NPI (Normalized Performance Index) for a region
 */
double CalculateNPI(const std::string& region, const Aggregate5s& agg, double fl_loss, double fl_div);

/**
 * Calculate CNPI (Composite NPI) across all regions
 */
double CalculateCNPI(const std::map<std::string, double>& npiPerRegion);

/**
 * Aggregate metrics for 5-second interval and write to aggregate_5s.csv
 */
void AggregateAndWrite5s(double time_5s, uint32_t fl_round, double fl_divergence);

/**
 * Create metadata.txt file
 */
void CreateMetadataFile(const std::string& resultsDir, uint32_t scenario, uint32_t runNumber,
                       const std::string& algorithm, const std::string& topology, uint32_t seed);

/**
 * Schedule per-second recording (called from main)
 */
void SchedulePerSecondRecording(double simTime);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_RESULTS_HPP

