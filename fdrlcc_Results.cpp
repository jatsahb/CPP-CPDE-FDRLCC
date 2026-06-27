/**
 * fdrlcc_Results.cpp
 * 
 * PhD Defense Framework Implementation
 * Per-second recording, NPI/CNPI calculation, and required file formats
 */

#include "fdrlcc_Results.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-metrics-collection.hpp"
#include "fdrlcc-console-colors.hpp"
#include "ns3/simulator.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Global CSV file streams (defined here, declared as extern in header)
std::ofstream g_network1sCsv;
std::ofstream g_learning1sCsv;
std::ofstream g_aggregate5sCsv;

// Per-second recording state
std::map<std::string, NetworkMetrics1s> g_lastNetworkMetrics;
std::map<std::string, LearningMetrics1s> g_lastLearningMetrics;
std::map<std::string, Aggregate5s> g_aggregateBuffer;  // Buffer for 5s aggregation

// NPI calculation state
NPIWeights g_npiWeights = {0.2, 0.1, 0.05, 0.1, 0.25, 0.05, 0.15, 0.1};
NPINormalization g_npiNorm = {100.0, 50.0, 0.01, 1.0, 0.01};

void
InitializePhdFrameworkFiles(const std::string& resultsDir, uint32_t scenario, uint32_t runNumber)
{
  // Ensure directory exists
  std::string cmdStr = "mkdir -p " + resultsDir;
  system(cmdStr.c_str());
  
  // Network metrics per second CSV
  std::ostringstream networkFile;
  networkFile << resultsDir << "/scenario_" << scenario << "_run_" << runNumber << "_network_1s.csv";
  g_network1sCsv.open(networkFile.str(), std::ios::out | std::ios::trunc);
  if (g_network1sCsv.is_open()) {
    g_network1sCsv << "time,throughput_mbps,avg_delay_ms,loss_ratio,interest_rate,data_rate" << std::endl;
    g_network1sCsv.flush();
    std::cout << "✓ PhD Framework: Network 1s CSV initialized (" << networkFile.str() << ")" << std::endl;
  } else {
    std::cerr << "[ERROR] Failed to open network_1s.csv" << std::endl;
  }
  
  // Learning metrics per second CSV
  std::ostringstream learningFile;
  learningFile << resultsDir << "/scenario_" << scenario << "_run_" << runNumber << "_learning_1s.csv";
  g_learning1sCsv.open(learningFile.str(), std::ios::out | std::ios::trunc);
  if (g_learning1sCsv.is_open()) {
    g_learning1sCsv << "time,region,actor_action,q_value,reward,reward_smooth,noise,grad_norm,grad_clip" << std::endl;
    g_learning1sCsv.flush();
    std::cout << "✓ PhD Framework: Learning 1s CSV initialized (" << learningFile.str() << ")" << std::endl;
  } else {
    std::cerr << "[ERROR] Failed to open learning_1s.csv" << std::endl;
  }
  
  // Aggregate 5s CSV
  std::ostringstream aggregateFile;
  aggregateFile << resultsDir << "/scenario_" << scenario << "_run_" << runNumber << "_aggregate_5s.csv";
  g_aggregate5sCsv.open(aggregateFile.str(), std::ios::out | std::ios::trunc);
  if (g_aggregate5sCsv.is_open()) {
    g_aggregate5sCsv << "time_5s,region,avg_throughput,avg_delay,loss_ratio,fairness,avg_reward,NPI,CNPI" << std::endl;
    g_aggregate5sCsv.flush();
    std::cout << "✓ PhD Framework: Aggregate 5s CSV initialized (" << aggregateFile.str() << ")" << std::endl;
  } else {
    std::cerr << "[ERROR] Failed to open aggregate_5s.csv" << std::endl;
  }
  
  // Initialize aggregate buffers
  for (const auto& region : g_topologyInfo.regions) {
    g_aggregateBuffer[region] = Aggregate5s();
  }
}

void
ClosePhdFrameworkFiles()
{
  if (g_network1sCsv.is_open()) {
    g_network1sCsv.close();
  }
  if (g_learning1sCsv.is_open()) {
    g_learning1sCsv.close();
  }
  if (g_aggregate5sCsv.is_open()) {
    g_aggregate5sCsv.close();
  }
}

void
RecordNetworkMetrics1s(double time_sec, const std::string& region, const NetworkMetrics1s& metrics)
{
  if (!g_network1sCsv.is_open()) return;
  
  g_network1sCsv << std::fixed << std::setprecision(2) << time_sec << ","
                  << std::setprecision(4) << metrics.throughput_mbps << ","
                  << std::setprecision(2) << metrics.avg_queue_delay_ms << ","
                  << std::setprecision(5) << metrics.packet_loss_ratio << ","
                  << std::setprecision(2) << metrics.interest_rate << ","
                  << std::setprecision(2) << metrics.data_rate << std::endl;
  g_network1sCsv.flush();
  
  // Store for aggregation
  g_lastNetworkMetrics[region] = metrics;
}

void
RecordLearningMetrics1s(double time_sec, const std::string& region, const LearningMetrics1s& metrics)
{
  if (!g_learning1sCsv.is_open()) return;
  
  g_learning1sCsv << std::fixed << std::setprecision(2) << time_sec << ","
                   << region << ","
                   << std::setprecision(3) << metrics.actor_action << ","
                   << std::setprecision(3) << metrics.critic_q_value << ","
                   << std::setprecision(3) << metrics.reward << ","
                   << std::setprecision(3) << metrics.reward_smoothed << ","
                   << std::setprecision(3) << metrics.exploration_noise << ","
                   << std::setprecision(6) << metrics.gradient_norm << ","
                   << (metrics.gradient_clipped ? "1" : "0") << std::endl;
  g_learning1sCsv.flush();
  
  // Store for aggregation
  g_lastLearningMetrics[region] = metrics;
}

double
CalculateNPI(const std::string& region, const Aggregate5s& agg, double fl_loss, double fl_div)
{
  // Normalize metrics
  double n_thr = std::min(agg.avg_throughput / g_npiNorm.T_MAX, 1.0);
  double n_delay = std::max(1.0 - agg.avg_delay / g_npiNorm.D_MAX, 0.0);
  double n_loss = std::max(1.0 - agg.loss_ratio / g_npiNorm.L_MAX, 0.0);
  double n_fair = agg.fairness;  // Already 0-1
  double n_reward = std::min(agg.avg_reward, 1.0);  // Assume reward is normalized
  double n_q = 0.0;  // Q-value not in aggregate, use 0 or get from learning metrics
  if (g_lastLearningMetrics.find(region) != g_lastLearningMetrics.end()) {
    n_q = std::min(std::abs(g_lastLearningMetrics[region].critic_q_value), 1.0);
  }
  double n_fl_loss = std::max(1.0 - fl_loss / g_npiNorm.LOSS_FL_MAX, 0.0);
  double n_fl_div = std::max(1.0 - fl_div / g_npiNorm.DIV_MAX, 0.0);
  
  // Weighted NPI
  double npi = g_npiWeights.throughput * n_thr +
               g_npiWeights.delay * n_delay +
               g_npiWeights.loss * n_loss +
               g_npiWeights.fairness * n_fair +
               g_npiWeights.reward * n_reward +
               g_npiWeights.q_value * n_q +
               g_npiWeights.fl_loss * n_fl_loss +
               g_npiWeights.fl_div * n_fl_div;
  
  return npi;
}

double
CalculateCNPI(const std::map<std::string, double>& npiPerRegion)
{
  if (npiPerRegion.empty()) return 0.0;
  
  double sum = 0.0;
  for (const auto& [region, npi] : npiPerRegion) {
    sum += npi;
  }
  
  return sum / static_cast<double>(npiPerRegion.size());
}

void
AggregateAndWrite5s(double time_5s, uint32_t fl_round, double fl_divergence)
{
  if (!g_aggregate5sCsv.is_open()) return;
  
  std::map<std::string, double> npiPerRegion;
  std::vector<std::string> regionOrder;  // Store order for CNPI update
  
  // Calculate averages and NPI for each region
  for (const auto& [region, agg] : g_aggregateBuffer) {
    if (agg.sample_count == 0) continue;  // Skip if no samples
    
    // Calculate averages
    Aggregate5s avgAgg;
    avgAgg.avg_throughput = agg.avg_throughput / static_cast<double>(agg.sample_count);
    avgAgg.avg_delay = agg.avg_delay / static_cast<double>(agg.sample_count);
    avgAgg.loss_ratio = agg.loss_ratio / static_cast<double>(agg.sample_count);
    avgAgg.fairness = agg.fairness / static_cast<double>(agg.sample_count);
    avgAgg.avg_reward = agg.avg_reward / static_cast<double>(agg.sample_count);
    
    // Get FL loss (use divergence as proxy if not available)
    double fl_loss = fl_divergence;  // Approximation
    
    double npi = CalculateNPI(region, avgAgg, fl_loss, fl_divergence);
    npiPerRegion[region] = npi;
    regionOrder.push_back(region);
  }
  
  // Calculate CNPI
  double cnpi = CalculateCNPI(npiPerRegion);
  
  // Write to aggregate CSV with CNPI
  for (const auto& region : regionOrder) {
    const auto& agg = g_aggregateBuffer[region];
    if (agg.sample_count == 0) continue;
    
    Aggregate5s avgAgg;
    avgAgg.avg_throughput = agg.avg_throughput / static_cast<double>(agg.sample_count);
    avgAgg.avg_delay = agg.avg_delay / static_cast<double>(agg.sample_count);
    avgAgg.loss_ratio = agg.loss_ratio / static_cast<double>(agg.sample_count);
    avgAgg.fairness = agg.fairness / static_cast<double>(agg.sample_count);
    avgAgg.avg_reward = agg.avg_reward / static_cast<double>(agg.sample_count);
    
    double npi = npiPerRegion[region];
    
    g_aggregate5sCsv << std::fixed << std::setprecision(2) << time_5s << ","
                     << region << ","
                     << std::setprecision(2) << avgAgg.avg_throughput << ","
                     << std::setprecision(2) << avgAgg.avg_delay << ","
                     << std::setprecision(5) << avgAgg.loss_ratio << ","
                     << std::setprecision(3) << avgAgg.fairness << ","
                     << std::setprecision(3) << avgAgg.avg_reward << ","
                     << std::setprecision(3) << npi << ","
                     << std::setprecision(3) << cnpi << std::endl;
  }
  g_aggregate5sCsv.flush();
  
  // Reset aggregate buffers
  for (auto& [region, agg] : g_aggregateBuffer) {
    agg = Aggregate5s();
  }
}

void
CreateMetadataFile(const std::string& resultsDir, uint32_t scenario, uint32_t runNumber,
                  const std::string& algorithm, const std::string& topology, uint32_t seed)
{
  std::ostringstream metadataFile;
  metadataFile << resultsDir << "/scenario_" << scenario << "_run_" << runNumber << "_metadata.txt";
  
  std::ofstream metadata(metadataFile.str(), std::ios::out | std::ios::trunc);
  if (metadata.is_open()) {
    metadata << "algorithm=" << algorithm << std::endl;
    metadata << "ablation=none" << std::endl;
    metadata << "topology=" << topology << std::endl;
    metadata << "seed=" << seed << std::endl;
    metadata << "scenario=" << scenario << std::endl;
    metadata << "run=" << runNumber << std::endl;
    metadata.close();
    std::cout << "✓ PhD Framework: Metadata file created (" << metadataFile.str() << ")" << std::endl;
  } else {
    std::cerr << "[ERROR] Failed to create metadata file" << std::endl;
  }
}

// Per-second recording function (called by scheduler)
void
RecordMetricsPerSecond()
{
  double simTime = Simulator::Now().GetSeconds();
  
  // Collect network metrics for each region
  for (const auto& region : g_topologyInfo.regions) {
    NetworkMetrics1s netMetrics;
    
    // Get consumers for this region
    std::vector<Ptr<FdrlConsumer>> consumersInRegion;
    for (auto& consumer : g_consumers) {
      if (g_consumerRegions.find(consumer) != g_consumerRegions.end() &&
          g_consumerRegions[consumer] == region) {
        consumersInRegion.push_back(consumer);
      }
    }
    
    if (consumersInRegion.empty()) continue;
    
    // Calculate metrics from consumers (similar to PrintEnhancedConsoleOutput)
    uint64_t totalInterests = 0;
    uint64_t totalData = 0;
    uint64_t totalTimeouts = 0;
    double totalDelay = 0.0;
    uint32_t delayCount = 0;
    
    for (auto& consumer : consumersInRegion) {
      if (consumer) {
        totalInterests += consumer->GetTotalInterestsSent();
        totalData += consumer->GetTotalDataReceived();
        totalTimeouts += consumer->GetTotalTimeouts();
        
        double delaySum = consumer->GetTotalDelaySum();
        uint64_t delayCnt = consumer->GetDelayCount();
        if (delayCnt > 0) {
          totalDelay += delaySum;
          delayCount += delayCnt;
        }
      }
    }
    
    // Calculate average delay
    double avgDelay = (delayCount > 0) ? (totalDelay / static_cast<double>(delayCount)) : 0.0;
    
    // Get previous values for rate calculation
    static std::map<std::string, std::pair<double, std::pair<uint64_t, uint64_t>>> prevValues;
    double prevTime = prevValues[region].first;
    uint64_t prevInterests = prevValues[region].second.first;
    uint64_t prevData = prevValues[region].second.second;
    
    double dt = simTime - prevTime;
    if (dt < 0.001) dt = 1.0;  // Use 1 second for first call
    
    // Calculate rates
    double interestRate = (dt > 0) ? ((totalInterests - prevInterests) / dt) : 0.0;
    double dataRate = (dt > 0) ? ((totalData - prevData) / dt) : 0.0;
    double throughputMbps = (dataRate * 1024.0 * 8.0) / 1e6;
    double packetLossRate = (totalInterests > 0) ? (static_cast<double>(totalTimeouts) / totalInterests) : 0.0;
    
    // Update previous values
    prevValues[region] = std::make_pair(simTime, std::make_pair(totalInterests, totalData));
    
    // Set network metrics
    netMetrics.throughput_mbps = throughputMbps;
    netMetrics.avg_queue_delay_ms = avgDelay;
    netMetrics.packet_loss_ratio = packetLossRate;
    netMetrics.interest_rate = interestRate;
    netMetrics.data_rate = dataRate;
    
    RecordNetworkMetrics1s(simTime, region, netMetrics);
    
    // Collect learning metrics
    LearningMetrics1s learnMetrics;
    learnMetrics.region_id = region;
    
    if (g_regionDRL.find(region) != g_regionDRL.end()) {
      const auto& drl = g_regionDRL[region];
      learnMetrics.actor_action = drl.rateFactor;
      learnMetrics.critic_q_value = 0.0;  // Get from last training if available
      // REFACTORED: Use unified RegionTrainingStats
      learnMetrics.reward = (g_regionStats.find(region) != g_regionStats.end()) ? 
                            g_regionStats[region].lastReward : 0.0;
      learnMetrics.reward_smoothed = 0.0;  // Calculate from window if available
      learnMetrics.exploration_noise = g_explorationNoise;
      learnMetrics.gradient_norm = 0.0;  // Get from last training
      learnMetrics.gradient_clipped = false;  // Get from last training
    }
    
    RecordLearningMetrics1s(simTime, region, learnMetrics);
    
    // Update aggregate buffer
    if (g_aggregateBuffer.find(region) != g_aggregateBuffer.end()) {
      auto& agg = g_aggregateBuffer[region];
      agg.avg_throughput += netMetrics.throughput_mbps;
      agg.avg_delay += netMetrics.avg_queue_delay_ms;
      agg.loss_ratio += netMetrics.packet_loss_ratio;
      agg.avg_reward += learnMetrics.reward;
      agg.sample_count++;  // Increment sample count
      // Fairness will be updated from FL metrics (set separately)
    }
  }
  
  // Schedule next recording (every 1 second)
  Simulator::Schedule(Seconds(1.0), &RecordMetricsPerSecond);
}

void
SchedulePerSecondRecording(double simTime)
{
  // Start per-second recording
  Simulator::Schedule(Seconds(1.0), &RecordMetricsPerSecond);
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

