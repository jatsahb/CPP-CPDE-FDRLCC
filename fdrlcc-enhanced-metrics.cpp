/**
 * fdrlcc-enhanced-metrics.cpp
 * 
 * Enhanced metrics collection implementation
 */

#include "fdrlcc-enhanced-metrics.hpp"
#include "fdrlcc-types.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <limits>
#include <algorithm>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Global CSV file streams are defined in fdrlcc_unified.cpp (declared as extern in header)
// Statistics tracking
std::map<std::string, std::map<std::string, std::map<std::string, MetricStatistics>>> g_metricStatistics;

std::string
GetTrainingStatusString(const std::string& region)
{
  // Check training status from CheckTrainingStatus logic
  auto& drl = g_regionDRL[region];
  size_t bufferSize = drl.replayBuffer.Size();
  size_t trainingSteps = drl.trainingStep;
  
  // FIX 1: Use adaptive warmup size
  size_t effectiveWarmupSize = static_cast<size_t>(std::max(drl.minWarmupSize, drl.targetWarmupSize));
  
  if (bufferSize < effectiveWarmupSize) {
    return "WARMUP";
  }
  
  // Check if training is active, stable, or stalled
  // This mirrors the logic in CheckTrainingStatus
  bool isActive = (bufferSize >= effectiveWarmupSize) && 
                  (trainingSteps > 0) &&
                  (g_trainingStatus.isActive);
  
  bool isStable = (bufferSize >= effectiveWarmupSize) &&
                  (trainingSteps > 50) &&
                  (!g_trainingStatus.isStalled) &&
                  (g_trainingStatus.isActive == false);
  
  bool isStalled = g_trainingStatus.isStalled;
  
  if (isStable) return "STABLE";
  if (isStalled) return "STALLED";
  if (isActive) return "ACTIVE";
  return "INACTIVE";
}

void
InitializeEnhancedMetricsCsv(const std::string& resultsDir)
{
  // Ensure directory exists before opening files
  std::string cmdStr = "mkdir -p " + resultsDir;
  int result = system(cmdStr.c_str());
  if (result != 0) {
    std::cerr << "[WARNING] Failed to create directory: " << resultsDir << std::endl;
  }
  
  // REMOVED: Queue Metrics CSV - replaced by StructuredLogger
  // StructuredLogger handles queue metrics via LogCongestionMetrics() in logs/congestion/congestion_*.csv
  
  // REMOVED: gradient_metrics.csv - merged into training_metrics.csv
  // REMOVED: td_error_metrics.csv - merged into training_metrics.csv
  // REMOVED: fairness_metrics.csv - merged into fl_metrics.csv
  // These metrics are now written directly to their consolidated files
  
  // REMOVED: Training Status CSV - redundant with training_metrics.csv
  // Training status can be derived from training_metrics.csv (buffer_size, training_step already included)
  
  std::cout << "✓ Enhanced metrics CSV files initialized" << std::endl;
}

void
CloseEnhancedMetricsCsv()
{
  // REMOVED: g_queueMetricsCsv - replaced by StructuredLogger
  // REMOVED: g_trainingStatusCsv - redundant with training_metrics.csv
  // REMOVED: gradient_metrics.csv, td_error_metrics.csv, fairness_metrics.csv (merged into other files)
  if (g_statisticsSummaryCsv.is_open()) g_statisticsSummaryCsv.close();
}

void
WriteQueueMetrics(double timestamp, const std::string& region, const QueueMetrics& metrics)
{
  // REMOVED: g_queueMetricsCsv writing - replaced by StructuredLogger
  // StructuredLogger handles queue metrics via LogCongestionMetrics() in logs/congestion/congestion_*.csv
  // This function is kept for backward compatibility but does nothing
  
  // Update statistics (still needed for internal tracking)
  UpdateMetricStatistics("queue", "utilization", region, metrics.utilization);
  UpdateMetricStatistics("queue", "current", region, static_cast<double>(metrics.current));
  UpdateMetricStatistics("queue", "peak", region, static_cast<double>(metrics.peak));
  UpdateMetricStatistics("queue", "average", region, metrics.average);
}

void
WriteGradientMetrics(double timestamp, const std::string& region, const GradientMetrics& metrics)
{
  // REMOVED: gradient_metrics.csv writing - now merged into training_metrics.csv
  // Gradient metrics are written directly in training_metrics.csv during training
  // This function is kept for backward compatibility but does nothing
  
  // Update statistics (still needed for internal tracking)
  UpdateMetricStatistics("gradient", "critic_norm", region, metrics.critic_norm_postclip);
  UpdateMetricStatistics("gradient", "actor_norm", region, metrics.actor_norm_postclip);
  UpdateMetricStatistics("gradient", "clipping_frequency", region, metrics.clipping_frequency);
}

void
WriteTDErrorMetrics(double timestamp, const std::string& region, const TDErrorMetrics& metrics)
{
  // REMOVED: td_error_metrics.csv writing - now merged into training_metrics.csv
  // TD error metrics are written directly in training_metrics.csv during training
  // This function is kept for backward compatibility but does nothing
  
  // Update statistics (still needed for internal tracking)
  UpdateMetricStatistics("td_error", "mean", region, metrics.mean);
  UpdateMetricStatistics("td_error", "std", region, metrics.std);
  UpdateMetricStatistics("td_error", "min", region, metrics.min);
  UpdateMetricStatistics("td_error", "max", region, metrics.max);
}

void
WriteFairnessMetrics(double timestamp, double fairness, uint32_t flRound)
{
  // REMOVED: fairness_metrics.csv writing - now merged into fl_metrics.csv
  // Fairness metrics are written directly in fl_metrics.csv during FL aggregation
  // This function is kept for backward compatibility but does nothing
  
  // Update statistics (still needed for internal tracking)
  UpdateMetricStatistics("fl", "fairness", "global", fairness);
}

void
WriteTrainingStatus(double timestamp, const std::string& region, const std::string& status)
{
  // REMOVED: g_trainingStatusCsv writing - redundant with training_metrics.csv
  // Training status (buffer_size, training_step) is already included in training_metrics.csv
  // This function is kept for backward compatibility but does nothing
}

void
UpdateMetricStatistics(const std::string& category, 
                       const std::string& metric,
                       const std::string& region,
                       double value)
{
  auto& stats = g_metricStatistics[category][metric][region];
  stats.category = category;
  stats.metric = metric;
  stats.region = region;
  
  stats.sum += value;
  stats.sumSquared += value * value;
  stats.count++;
  stats.min = std::min(stats.min, value);
  stats.max = std::max(stats.max, value);
  
  if (!stats.initialSet) {
    stats.initial = value;
    stats.initialSet = true;
  }
  stats.final = value;
}

void
ComputeFinalStatistics()
{
  for (auto& [category, categoryMap] : g_metricStatistics) {
    for (auto& [metric, metricMap] : categoryMap) {
      for (auto& [region, stats] : metricMap) {
        if (stats.count > 0) {
          // Compute mean
          stats.mean = stats.sum / static_cast<double>(stats.count);
          
          // Compute std dev
          double variance = (stats.sumSquared / static_cast<double>(stats.count)) - (stats.mean * stats.mean);
          stats.stdDev = std::sqrt(std::max(0.0, variance));
          
          // Compute improvement
          if (std::abs(stats.initial) > 1e-6) {
            stats.improvementPercent = 
              ((stats.final - stats.initial) / std::abs(stats.initial)) * 100.0;
          } else if (stats.initial != 0.0) {
            stats.improvementPercent = 
              ((stats.final - stats.initial) / 1.0) * 100.0;
          }
          
          // Determine trend
          double changeThreshold = std::abs(stats.initial) * 0.05; // 5% threshold
          if (changeThreshold < 1e-6) changeThreshold = 0.01; // Minimum threshold
          
          if (stats.final > stats.initial + changeThreshold) {
            stats.trend = "increasing";
          } else if (stats.final < stats.initial - changeThreshold) {
            stats.trend = "decreasing";
          } else {
            stats.trend = "stable";
          }
        }
      }
    }
  }
}

void
WriteStatisticsSummary(const std::string& resultsDir)
{
  ComputeFinalStatistics();
  
  // Ensure directory exists
  std::string cmdStr = "mkdir -p " + resultsDir;
  int result = system(cmdStr.c_str());
  if (result != 0) {
    std::cerr << "[WARNING] Failed to create directory: " << resultsDir << std::endl;
  }
  
  g_statisticsSummaryCsv.open(resultsDir + "/statistics_summary.csv", std::ios::out | std::ios::trunc);
  if (!g_statisticsSummaryCsv.is_open()) {
    std::cerr << "[ERROR] Failed to open statistics_summary.csv in directory: " << resultsDir << std::endl;
    return;
  }
  
  // Write header
  g_statisticsSummaryCsv << "category,metric,region,mean,std_dev,min,max,initial,final,improvement_pct,trend,count" << std::endl;
  
  // Write all statistics
  for (const auto& [category, categoryMap] : g_metricStatistics) {
    for (const auto& [metric, metricMap] : categoryMap) {
      for (const auto& [region, stats] : metricMap) {
        if (stats.count > 0) {
          g_statisticsSummaryCsv << std::fixed << std::setprecision(6)
                                << stats.category << ","
                                << stats.metric << ","
                                << stats.region << ","
                                << stats.mean << ","
                                << stats.stdDev << ","
                                << stats.min << ","
                                << stats.max << ","
                                << stats.initial << ","
                                << stats.final << ","
                                << std::setprecision(2) << stats.improvementPercent << ","
                                << stats.trend << ","
                                << stats.count << std::endl;
        }
      }
    }
  }
  
  g_statisticsSummaryCsv.flush();  // Flush before closing
  g_statisticsSummaryCsv.close();
  std::cout << "✓ Statistics summary written to " << resultsDir << "/statistics_summary.csv" << std::endl;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

