/**
 * fdrlcc-enhanced-metrics.hpp
 * 
 * Enhanced metrics collection for high-priority metrics
 * Implements: Queue, Gradient, TD Error, Fairness, Training Status, Statistics
 */

#ifndef FDRLCC_ENHANCED_METRICS_HPP
#define FDRLCC_ENHANCED_METRICS_HPP

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <limits>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Queue Metrics Structure
 */
struct QueueMetrics {
  double utilization = 0.0;    // Queue utilization ratio
  uint64_t current = 0;       // Current queue size
  uint64_t peak = 0;          // Peak queue size
  uint64_t max = 0;           // Maximum queue capacity
  double average = 0.0;       // Average queue size (over time window)
  std::vector<uint64_t> samples;  // Queue samples for average calculation
};

/**
 * Gradient Metrics Structure
 */
struct GradientMetrics {
  double critic_norm_preclip = 0.0;   // Critic gradient norm before clipping
  double critic_norm_postclip = 0.0;  // Critic gradient norm after clipping
  double actor_norm_preclip = 0.0;    // Actor gradient norm before clipping
  double actor_norm_postclip = 0.0;   // Actor gradient norm after clipping
  double clipping_frequency = 0.0;    // Clipping frequency (0.0-1.0)
  uint32_t clip_count = 0;            // Number of times gradients were clipped
  uint32_t update_count = 0;          // Total number of updates
};

/**
 * TD Error Metrics Structure
 */
struct TDErrorMetrics {
  double mean = 0.0;          // Mean TD error
  double std = 0.0;           // Standard deviation of TD error
  size_t count = 0;           // Sample count
  double min = 0.0;           // Minimum TD error
  double max = 0.0;           // Maximum TD error
};

/**
 * Training Status String
 */
std::string GetTrainingStatusString(const std::string& region);

/**
 * Statistics Tracking Structure
 */
struct MetricStatistics {
  std::string category;
  std::string metric;
  std::string region;
  
  // Running statistics
  double sum = 0.0;
  double sumSquared = 0.0;
  size_t count = 0;
  double min = std::numeric_limits<double>::max();
  double max = std::numeric_limits<double>::lowest();
  double initial = 0.0;
  double final = 0.0;
  bool initialSet = false;
  
  // Computed at end
  double mean = 0.0;
  double stdDev = 0.0;
  double improvementPercent = 0.0;
  std::string trend; // "increasing", "decreasing", "stable"
};

/**
 * Initialize enhanced metrics CSV files
 */
void InitializeEnhancedMetricsCsv(const std::string& resultsDir);

/**
 * Close enhanced metrics CSV files
 */
void CloseEnhancedMetricsCsv();

/**
 * Write queue metrics to CSV
 */
void WriteQueueMetrics(double timestamp, const std::string& region, const QueueMetrics& metrics);

/**
 * Write gradient metrics to CSV
 */
void WriteGradientMetrics(double timestamp, const std::string& region, const GradientMetrics& metrics);

/**
 * Write TD error metrics to CSV
 */
void WriteTDErrorMetrics(double timestamp, const std::string& region, const TDErrorMetrics& metrics);

/**
 * Write fairness metrics to CSV
 */
void WriteFairnessMetrics(double timestamp, double fairness, uint32_t flRound);

/**
 * Write training status to CSV
 */
void WriteTrainingStatus(double timestamp, const std::string& region, const std::string& status);

/**
 * Update statistics for a metric
 */
void UpdateMetricStatistics(const std::string& category, 
                           const std::string& metric,
                           const std::string& region,
                           double value);

/**
 * Compute final statistics from tracked data
 */
void ComputeFinalStatistics();

/**
 * Write statistics summary to CSV
 */
void WriteStatisticsSummary(const std::string& resultsDir);

// Global CSV file streams for enhanced metrics (defined in fdrlcc_unified.cpp)
extern std::ofstream g_queueMetricsCsv;
// NOTE: Removed extern declarations for merged CSV files:
// - g_gradientMetricsCsv (merged into training_metrics.csv)
// - g_tdErrorMetricsCsv (merged into training_metrics.csv)
// - g_fairnessMetricsCsv (merged into fl_metrics.csv)
extern std::ofstream g_trainingStatusCsv;
extern std::ofstream g_statisticsSummaryCsv;

// Statistics tracking map: category -> metric -> region -> statistics
extern std::map<std::string, std::map<std::string, std::map<std::string, MetricStatistics>>> g_metricStatistics;

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_ENHANCED_METRICS_HPP

