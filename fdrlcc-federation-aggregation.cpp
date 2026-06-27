/**
 * fdrlcc-federation-aggregation.cpp
 * 
 * Federated Learning aggregation functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-federation-aggregation.hpp"
#include "fdrlcc-ablation-control.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-simulation-utils.hpp"
#include "fdrlcc-csv-management.hpp"
// REFACTORED (STEP 7): Removed fdrlcc-state-extraction.hpp - federation should not extract state
#include "fdrlcc-weight-persistence.hpp"
#include "fdrlcc-enhanced-metrics.hpp"
#include "fdrlcc-structured-logger.hpp"  // OPTION 3.5: Structured logging
#include "fdrlcc-console-colors.hpp"
#include "fdrlcc-ablation-control.hpp"
#include "fdrlcc-research-instrumentation.hpp"  // Research instrumentation
#include "fdrlcc-event-logger.hpp"  // Event-driven console logger
#include "fdrlcc-status-display.hpp"  // Structured status display
#include "fdrlcc-console-output.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include "src_cpp/controller/fdrl-ddpg-networks.hpp"
#include "src_cpp/helpers/fdrl-performance-logger.hpp"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <chrono>
#include <cstdlib>

NS_LOG_COMPONENT_DEFINE("FdrlccFederationAggregation");

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Standard Federated Averaging
 */
std::vector<double>
FedAvgAggregate(const std::map<std::string, std::vector<double>>& localWeights)
{
  if (localWeights.empty()) return g_globalWeights;
  
  size_t weightSize = localWeights.begin()->second.size();
  std::vector<double> globalWeights(weightSize, 0.0);
  
  // Sum all local weights
  for (const auto& [region, weights] : localWeights) {
    for (size_t i = 0; i < weights.size(); i++) {
      globalWeights[i] += weights[i];
    }
  }
  
  double numRegions = static_cast<double>(localWeights.size());
  for (auto& w : globalWeights) {
    w /= numRegions;
  }
  
  return globalWeights;
}

/**
 * Calculate divergence between local models (L2 distance from global)
 */
double
CalculateDivergence(const std::map<std::string, std::vector<double>>& localWeights)
{
  if (localWeights.empty() || g_globalWeights.empty()) return 0.0;
  
  double totalDivergence = 0.0;
  
  for (const auto& [region, weights] : localWeights) {
    double regionDivergence = 0.0;
    for (size_t i = 0; i < std::min(weights.size(), g_globalWeights.size()); i++) {
      double diff = weights[i] - g_globalWeights[i];
      regionDivergence += diff * diff;
    }
    totalDivergence += std::sqrt(regionDivergence);
  }
  
  return totalDivergence / static_cast<double>(localWeights.size());
}

/**
 * Calculate adaptive mixing ratio based on divergence and performance
 */
double
CalculateAdaptiveMixingRatio(const std::string& region, 
                             double divergence, 
                             double regionReward,
                             double avgReward)
{
  // Base mixing ratio
  const double alpha_base = 0.7;
  const double D_threshold = 0.5;  // Divergence normalization threshold
  
  // Divergence-based adjustment
  // High divergence → more global learning (increase α)
  double divergenceFactor = std::min(1.0, divergence / D_threshold);
  double delta_div = 0.2 * divergenceFactor;
  
  // Performance-based adjustment
  // Poor performers → more global learning (increase α)
  double delta_perf = 0.0;
  if (avgReward > 0) {
    double rewardDiff = avgReward - regionReward;
    double perfFactor = std::max(0.0, 
        std::min(1.0, rewardDiff / std::max(0.1, avgReward)));
    delta_perf = 0.15 * perfFactor;
  }
  
  // Calculate final adaptive mixing ratio
  double alpha = alpha_base + delta_div + delta_perf;
  
  // Clamp to [0.5, 0.9] range
  return std::max(0.5, std::min(0.9, alpha));
}

// ============================================================================
// ENHANCEMENT 2: Performance-Weighted Federated Averaging (PW-FedAvg)
// ============================================================================

/**
 * Performance-Weighted Federated Averaging
 * 
 * Mathematical Formula:
 *   w_global^(t) = Σ_{r=1}^R β_r^(t) × w_r^(t)
 * 
 * @param localWeights Map of region -> local weights
 * @param regionRewards Map of region -> reward values
 * @return Global weights aggregated using performance-weighted averaging
 */
std::vector<double>
PerformanceWeightedFedAvg(const std::map<std::string, std::vector<double>>& localWeights,
                          const std::map<std::string, double>& regionRewards)
{
  if (localWeights.empty()) return g_globalWeights;
  
  size_t weightSize = localWeights.begin()->second.size();
  std::vector<double> globalWeights(weightSize, 0.0);
  
  // Calculate total positive reward
  double totalPositiveReward = 0.0;
  for (const auto& [region, reward] : regionRewards) {
    totalPositiveReward += std::max(0.0, reward);  // Only positive rewards
  }
  
  // Fallback to standard FedAvg if no positive rewards
  if (totalPositiveReward < 1e-6) {
    return FedAvgAggregate(localWeights);
  }
  
  // Performance-weighted aggregation
  for (const auto& [region, weights] : localWeights) {
    // Calculate performance weight (proportional to positive reward)
    double regionWeight = std::max(0.0, regionRewards.at(region)) / totalPositiveReward;
    
    // Add weighted contribution
    for (size_t i = 0; i < weights.size(); i++) {
      globalWeights[i] += regionWeight * weights[i];
    }
  }
  
  return globalWeights;
}

double
CalculateJainsFairnessIndex(const std::map<std::string, double>& regionThroughputs)
{
  if (regionThroughputs.empty()) return 0.0;
  
  double sumThroughput = 0.0;
  double sumSquaredThroughput = 0.0;
  
  for (const auto& [region, throughput] : regionThroughputs) {
    sumThroughput += throughput;
    sumSquaredThroughput += throughput * throughput;
  }
  
  if (sumThroughput < 1e-6) return 0.0;
  
  size_t R = regionThroughputs.size();
  double jainsIndex = (sumThroughput * sumThroughput) / (R * sumSquaredThroughput);
  
  return jainsIndex;  // Range: [1/R, 1]
}

double
CalculateStabilityIndex(const std::vector<double>& rewardHistory, size_t windowSize)
{
  if (rewardHistory.size() < 2) return 1.0;  // Perfect stability if no history
  
  size_t actualWindow = std::min(windowSize, rewardHistory.size());
  std::vector<double> recentRewards(
      rewardHistory.end() - actualWindow, 
      rewardHistory.end());
  
  // Calculate mean
  double mean = 0.0;
  for (double r : recentRewards) {
    mean += r;
  }
  mean /= actualWindow;
  
  // Calculate variance
  double variance = 0.0;
  for (double r : recentRewards) {
    double diff = r - mean;
    variance += diff * diff;
  }
  variance /= actualWindow;
  
  // Stability index (inverse of normalized variance)
  const double sigma_threshold = 0.1;
  double normalizedVariance = std::min(1.0, variance / sigma_threshold);
  double stability = std::max(0.0, 1.0 - normalizedVariance);
  
  return stability;  // Range: [0, 1]
}

double
CalculateEnhancedReward(const std::string& region, 
                        const MetricSnapshot& snapshot,
                        double fairnessIndex,
                        double stabilityIndex)
{
  // INCREASED REWARD SENSITIVITY: Higher weights and exponential scaling
  // Weights increased to make reward more sensitive to metric changes
  const double w_thr = 0.5;   // Throughput weight (increased from 0.4)
  const double w_del = -0.3;  // Delay penalty (increased magnitude from -0.25)
  const double w_drop = -0.2; // Drop penalty (increased from -0.15)
  const double w_fair = 0.15;  // Fairness reward
  const double w_stab = 0.05;  // Stability reward
  
  // INCREASED SENSITIVITY: Use exponential scaling for throughput
  double throughputReward = 0.0;
  if (snapshot.throughputMbps > 0.001) {
    const double scaleFactor = 3.0;  // Lower = more sensitive
    throughputReward = 1.0 - std::exp(-snapshot.throughputMbps / scaleFactor);
    throughputReward = std::min(1.0, throughputReward);
  }
  
  // REFACTORED: Use MetricEngine snapshot fields (rttMeanMs -> avgDelayMs, packetLossRate calculated from drops)
  // INCREASED SENSITIVITY: Exponential delay penalty
  double delayMs = snapshot.avgDelayMs;  // REFACTORED: avgDelayMs instead of rttMeanMs
  // Use actual delay measurement (no hardcoded minimum)
  // If delayMs is 0, it means no delay measurements yet (will be updated when data arrives)
  const double delayScale = 20.0;  // Lower = more sensitive
  double delayPenalty = 1.0 - std::exp(-delayMs / delayScale);
  delayPenalty = std::min(1.0, delayPenalty);
  
  // REFACTORED: Calculate drop penalty from totalPacketsDropped and totalInterestsSent
  // INCREASED SENSITIVITY: Exponential drop penalty
  double dropPenalty = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    double lossRate = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
    dropPenalty = lossRate;
  }
  if (dropPenalty > 0.0) {
    const double dropScale = 0.1;  // Lower = more sensitive
    dropPenalty = 1.0 - std::exp(-dropPenalty / dropScale);
    dropPenalty = std::min(1.0, dropPenalty);
  }
  
  // Multi-objective reward
  double reward = w_thr * throughputReward
                + w_del * delayPenalty
                + w_drop * dropPenalty
                + w_fair * fairnessIndex      // NEW: Fairness component
                + w_stab * stabilityIndex;    // NEW: Stability component
  
  return reward;
}

void
PerformFederatedAggregation()
{
  // Early return for non-FDRLCC algorithms (before any state changes)
  if (g_selectedAlgorithm != CCAlgorithm::FDRLCC) {
    return;
  }
  
  // ABLATION 2: Skip FL aggregation if disabled (local-only learning)
  if (g_ablationConfig.disableFL) {
    // Still log FL metrics for comparison (but no weight aggregation)
    // Log "local-only" mode to fl_metrics.csv
    g_flRound++;
    double simTime = Simulator::Now().GetSeconds();
    
    // Calculate mean local reward (for logging)
    double totalReward = 0.0;
    uint32_t activeRegions = 0;
    for (auto& [region, drl] : g_regionDRL) {
      if (drl.trainingStep > 0) {
        totalReward += drl.reward;
        activeRegions++;
      }
    }
    
    double meanLocalReward = (activeRegions > 0) ? (totalReward / static_cast<double>(activeRegions)) : 0.0;
    
    // Log local-only metrics (no aggregation occurred)
    if (g_flMetricsCsv.is_open()) {
      g_flMetricsCsv << std::fixed << std::setprecision(6)
                     << simTime << ","
                     << g_ablationConfig.ablationLabel << ","             // ablation_config
                     << g_ablationConfig.ablationLabel << ","  // ablation_config
                     << g_flRound << ","
                     << activeRegions << ","
                     << "local_only,"  // aggregation_strategy
                     << 0.0 << ","     // divergence_before
                     << 0.0 << ","     // divergence_after
                     << 0.0 << ","     // divergence_delta
                     << 0 << ","       // global_model_version
                     << 0.0 << ","     // aggregation_time_ms
                     << meanLocalReward << ","  // global_reward (same as mean_local_reward in local-only)
                     << meanLocalReward << ","  // mean_local_reward
                     << 0.0 << ","     // reward_gain_after_aggregation (no gain in local-only)
                     << g_explorationNoise << ","
                     << 0.0 << ","     // fairness_index (not calculated in local-only)
                     << 0.0 << std::endl;  // avg_mixing_ratio
    }
    
    // Schedule next FL round (actually just a logging checkpoint)
    Simulator::Schedule(Seconds(g_flInterval), &PerformFederatedAggregation);
    return;
  }
  
  g_flRound++;
  double simTime = Simulator::Now().GetSeconds();
  
  // Collect local weights, rewards, and throughputs for enhancements
  std::map<std::string, std::vector<double>> localWeights;
  std::map<std::string, double> regionRewards;  // For PW-FedAvg
  std::map<std::string, double> regionThroughputs;  // For fairness calculation
  double totalReward = 0.0;
  
  // REFACTORED: Use MetricEngine snapshots (STEP 7 - thin federation coordinator)
  // Federation should NOT recompute metrics - use MetricEngine snapshots only
  if (!g_metricEngine) {
    NS_LOG_WARN("MetricEngine not initialized - skipping federated aggregation");
    return;
  }
  
  // First pass: Collect all metrics needed for multi-objective reward
  for (auto& [region, drl] : g_regionDRL) {
    // REFACTORED: Get snapshot from MetricEngine (no manual metric collection)
    if (!g_metricEngine->IsRegionInitialized(region)) {
      NS_LOG_WARN("MetricEngine not initialized for region " << region << " - skipping");
      continue;
    }
    
    const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
    
    // Store throughput for fairness calculation
    regionThroughputs[region] = snapshot.throughputMbps;
  }
  
  // Calculate fairness index (network-wide, needs all throughputs)
  double fairnessIndex = CalculateJainsFairnessIndex(regionThroughputs);
  
  // Second pass: Calculate enhanced rewards and update models
  for (auto& [region, drl] : g_regionDRL) {
    // REFACTORED: Get snapshot from MetricEngine (no manual metric collection)
    if (!g_metricEngine->IsRegionInitialized(region)) {
      continue;
    }
    
    // REFACTORED (STEP 7): Federation should NOT compute rewards or access replay buffers
    // Rewards should be pre-computed and stored in drl.reward before federation is called
    // Use existing reward (already computed by reward calculation module)
    double reward = drl.reward;
    totalReward += reward;

    // PW-FedAvg: normalize by interest volume so high-send regions don't dominate weights
    const MetricSnapshot& pwSnapshot = g_metricEngine->GetLatestSnapshot(region);
    double interestNorm = std::sqrt(
        std::max(1.0, static_cast<double>(pwSnapshot.totalInterestsSent)));
    double pwScore = drl.smoothedRewardInitialized
        ? drl.smoothedRewardForAggregation
        : drl.reward;
    regionRewards[region] = pwScore / interestNorm;
    
    // REFACTORED (STEP 7): Federation should NOT access replay buffers
    // Check readiness only via training step counter (not replay buffer size)
    if (drl.trainingStep == 0) {
      // Track skipped regions silently (will print summary if all skipped)
      continue;  // Skip this region for this FL round
    }
    
    // REFACTORED (STEP 7): Federation should NOT update state
    // State extraction should happen outside federation (in action management or training logic)
    // Removed: drl.state = ExtractState(region);
    
    // Collect local actor network weights (for FL aggregation)
    localWeights[region] = drl.actor.Serialize();
  }
  
  // REFACTOR: Only calculate divergence for ACTIVE regions (already filtered above)
  // regions_participated = number of regions in localWeights (only active regions)
  uint32_t regions_participated = localWeights.size();
  
  // REFACTOR: Check if any active regions exist
  if (regions_participated == 0) {
    // Only log once per round, not repeatedly - use INFO level instead of ERROR
    static double lastErrorTime = -1.0;
    if (simTime - lastErrorTime > 4.9) {  // Log at most once per FL interval
      std::cout << "[INFO][" << std::fixed << std::setprecision(2) << simTime 
                << "s] FL round " << g_flRound 
                << " skipped: No active regions (training not started yet)" << std::endl;
      lastErrorTime = simTime;
    }
    // Schedule next FL round anyway
    Simulator::Schedule(Seconds(g_flInterval), &PerformFederatedAggregation);
    return;
  }
  
  // Log FL participation mask
  std::string participation_mask = "";
  for (const auto& [r, drlLocal] : g_regionDRL) {
    participation_mask += r + ":" + (drlLocal.trainingStep > 0 ? "ACTIVE" : "INACTIVE") + " ";
  }
  
  // Calculate divergence BEFORE aggregation (only for active regions)
  double divergence_before = CalculateDivergence(localWeights);
  
  // ============================================================================
  // STRUCTURED LOGGING: PRE_AGGREGATION metrics
  // ============================================================================
  if (g_enableStructuredLogs && g_structuredLogger) {
    // Calculate average local reward (across participating regions)
    double avgLocalReward = totalReward / static_cast<double>(regions_participated);
    
    // Fairness index (already calculated at line 324)
    // Store fairness index for later use (g_lastFLFairness is set later, use local variable)
    // Note: fairnessIndex is in scope from line 324
    
    // Calculate average local model norm (L2 norm across all participating regions)
    double avgLocalModelNorm = 0.0;
    for (const auto& [region, weights] : localWeights) {
      double localNorm = 0.0;
      for (double w : weights) {
        localNorm += w * w;
      }
      avgLocalModelNorm += std::sqrt(localNorm);
    }
    if (regions_participated > 0) {
      avgLocalModelNorm /= static_cast<double>(regions_participated);
    }
    
    // Log PRE_AGGREGATION phase
    // Note: LogFL doesn't have a fairness parameter, so we log it via LogEvent
    g_structuredLogger->LogFL(simTime,
                             g_flRound,
                             "PRE",
                             avgLocalReward,      // avg local reward
                             avgLocalModelNorm,   // local model norm
                             divergence_before);  // divergence before aggregation
    
    // Log fairness index separately via LogEvent (since LogFL doesn't include it)
    std::ostringstream fairnessDetails;
    fairnessDetails << "PRE_AGGREGATION fairness_index=" << std::fixed << std::setprecision(4) << fairnessIndex
                    << " avg_local_reward=" << std::setprecision(4) << avgLocalReward
                    << " local_model_norm=" << std::setprecision(4) << avgLocalModelNorm;
    g_structuredLogger->LogEvent(simTime, "FL_PRE_AGGREGATION", fairnessDetails.str());
  }
  
  // Log FL aggregation event (simplified single-line instead of box)
  // Removed duplicate box - will be shown in status display and FL ROUND COMPLETE message
  
  // ENHANCEMENT 2: Performance-Weighted FedAvg (instead of standard FedAvg)
  // ABLATION: Use uniform FedAvg if adaptive aggregation is disabled
  auto start_time = std::chrono::high_resolution_clock::now();
  std::string aggregation_strategy;
  if (IsAdaptiveAggregationEnabled()) {
    g_globalWeights = PerformanceWeightedFedAvg(localWeights, regionRewards);
    aggregation_strategy = "PW-FedAvg";
  } else {
    g_globalWeights = FedAvgAggregate(localWeights);
    aggregation_strategy = "FedAvg";
  }
  aggregation_strategy += IsAdaptiveMixingEnabled() ? "+AFM" : "+UniformMix";
  auto end_time = std::chrono::high_resolution_clock::now();
  double aggregation_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
  
  // Calculate average reward for adaptive mixing
  double avgReward = totalReward / static_cast<double>(g_regionDRL.size());
  
  // ENHANCEMENT 1: Adaptive Federated Mixing (AFM), or fixed uniform mixing for E4 ablation
  constexpr double kFixedMixingRatio = 0.7;
  constexpr double kTargetNetworkTau = 0.005;

  for (auto& [region, drl] : g_regionDRL) {
    double mixAlpha;
    if (IsAdaptiveMixingEnabled()) {
      mixAlpha = CalculateAdaptiveMixingRatio(
          region, divergence_before, drl.reward, avgReward);
      if (fairnessIndex < 0.85) {
        mixAlpha = std::min(mixAlpha, kFixedMixingRatio);
      }
    } else {
      mixAlpha = kFixedMixingRatio;
    }
    drl.adaptiveMixingRatio = mixAlpha;

    ActorNetwork globalActor;
    globalActor.Initialize();
    globalActor.Deserialize(g_globalWeights);

    drl.actor.SoftUpdate(globalActor, drl.adaptiveMixingRatio);

    drl.actor_target.SoftUpdate(drl.actor, kTargetNetworkTau);
    drl.critic_target.SoftUpdate(drl.critic, kTargetNetworkTau);

    NS_LOG_DEBUG("Region " << region << " mix=" << drl.adaptiveMixingRatio
                           << " (AFM=" << (IsAdaptiveMixingEnabled() ? "on" : "off") << ")");
  }
  
  // Calculate average mixing ratio for logging (before writing FL metrics)
  double avgMixingRatio = 0.0;
  for (const auto& [region, drl] : g_regionDRL) {
    avgMixingRatio += drl.adaptiveMixingRatio;
  }
  avgMixingRatio /= g_regionDRL.size();
  
  // Calculate divergence AFTER aggregation
  std::map<std::string, std::vector<double>> localWeightsAfter;
  for (auto& [region, drl] : g_regionDRL) {
    if (drl.trainingStep > 0) {
      localWeightsAfter[region] = drl.actor.Serialize();
    }
  }
  double divergence_after = CalculateDivergence(localWeightsAfter);
  double divergence_delta = divergence_after - divergence_before;
  
  // ============================================================================
  // STRUCTURED LOGGING: POST_AGGREGATION metrics
  // ============================================================================
  if (g_enableStructuredLogs && g_structuredLogger) {
    // Global reward (average reward across all regions)
    // avgReward is already calculated at line 401
    
    // Calculate global model norm (L2 norm of global weights)
    double globalModelNorm = 0.0;
    for (double w : g_globalWeights) {
      globalModelNorm += w * w;
    }
    globalModelNorm = std::sqrt(globalModelNorm);
    
    // Divergence between local and global models (already calculated)
    // divergence_after is the divergence after aggregation
    
    // Log POST_AGGREGATION phase
    g_structuredLogger->LogFL(simTime,
                             g_flRound,
                             "POST",
                             avgReward,          // global reward (average across all regions)
                             globalModelNorm,    // global model norm
                             divergence_after);  // divergence after aggregation
  }
  
  // ============================================================================
  // STEP 5: FL METRICS with causal benefit (mean_local_reward, reward_gain_after_aggregation)
  // ============================================================================
  // Log ONE row per FL round (not per region) with all FL data
  if (g_flMetricsCsv.is_open()) {
    // STEP 5: Calculate mean_local_reward BEFORE aggregation (for causal analysis)
    double meanLocalReward = totalReward / static_cast<double>(regions_participated);
    
    // STEP 5: Calculate reward_gain_after_aggregation
    // Note: globalReward (avgReward) is calculated AFTER aggregation, so this represents the gain
    double globalReward = avgReward;  // Average reward across all regions (after aggregation)
    double rewardGainAfterAggregation = globalReward - meanLocalReward;
    
    g_flMetricsCsv << std::fixed << std::setprecision(6)
                   << simTime << ","                                    // timestamp
                   << g_ablationConfig.ablationLabel << ","             // ablation_config
                   << g_flRound << ","                                  // fl_round
                   << regions_participated << ","                       // regions_participated
                   << aggregation_strategy << ","                        // aggregation_strategy
                   << divergence_before << ","                          // divergence_before
                   << divergence_after << ","                           // divergence_after
                   << divergence_delta << ","                           // divergence_delta
                   << g_flRound << ","                                  // global_model_version (using round number)
                   << aggregation_time_ms << ","                        // aggregation_time_ms
                   << globalReward << ","                                // global_reward (after aggregation)
                   << meanLocalReward << ","                             // mean_local_reward (STEP 5: before aggregation)
                   << rewardGainAfterAggregation << ","                 // reward_gain_after_aggregation (STEP 5)
                   << g_explorationNoise << ","                          // noise_level
                   << g_lastFLFairness << ","                           // fairness_index
                   << avgMixingRatio << std::endl;                      // avg_mixing_ratio
    FlushCsvFiles();
  }
  
  // REMOVED: WriteFairnessMetrics() - now merged into fl_metrics.csv above
  
  // OPTION 3.5: Structured logging - Log federation round metrics
  if (g_enableStructuredLogs && g_structuredLogger) {
    // Log for each participating region
    for (const auto& [region, weights] : localWeights) {
      // Calculate local update norm (L2 norm of local weights)
      double localUpdateNorm = 0.0;
      for (double w : weights) {
        localUpdateNorm += w * w;
      }
      localUpdateNorm = std::sqrt(localUpdateNorm);
      
      // Calculate global model norm (L2 norm of global weights)
      double globalModelNorm = 0.0;
      for (double w : g_globalWeights) {
        globalModelNorm += w * w;
      }
      globalModelNorm = std::sqrt(globalModelNorm);
      
      // Get aggregation weight (from PW-FedAvg or uniform 1.0/regions_participated)
      double aggregationWeight = 1.0 / static_cast<double>(regions_participated);  // Default uniform
      // TODO: Extract actual aggregation weight from PerformanceWeightedFedAvg if available
      
      // Check participation (region was in localWeights map)
      bool participation = true;
      
      g_structuredLogger->LogFederationRound(g_flRound,
                                             simTime,
                                             region,
                                             localUpdateNorm,
                                             globalModelNorm,
                                             divergence_after,  // Use divergence_after for this round
                                             aggregationWeight,
                                             participation);
    }
    
    // Log fairness metrics (global, one per round)
    if (g_metricEngine) {
      double jainFairness = g_lastFLFairness;
      
      // Calculate per-region throughput and queue share
      for (const auto& region : g_topologyInfo.regions) {
        if (g_metricEngine->IsRegionInitialized(region)) {
          const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
          double throughput = snapshot.throughputMbps;
          
          // Calculate queue share (normalized by total queue across all regions)
          double queueShare = snapshot.queueOccupancy;  // Already normalized 0-1 per region
          
          g_structuredLogger->LogFairnessMetrics(simTime,
                                                 region,
                                                 throughput,
                                                 jainFairness,
                                                 queueShare);
        }
      }
    }
  }
  
  // PART 6: WARNING - High FL divergence → regional imbalance (simplified single-line)
  if (divergence_after > 1.0) {
    std::cout << "[WARNING][" << std::fixed << std::setprecision(1) << simTime << "s] High divergence: " 
              << std::setprecision(4) << divergence_after << " (regional imbalance possible)" << std::endl;
  }
  
  // FIX: Enhanced exploration policy - adaptive noise based on reward/TD variance
  // Low reward variance → increase/reset noise (not just slow decay)
  // High TD variance → reduce actor LR (not noise) - TD variance = critic issue
  // Noise floor reached only when: reward variance stable, critic loss stable, gradients mostly unclipped
  
  double avg_reward_variance = 0.0;
  double avg_td_abs_mean = 0.0;
  double avg_clip_frequency = 0.0;
  // REFACTORED: Use unified RegionTrainingStats
  size_t region_count = 0;
  for (const auto& [r, drlLocal] : g_regionDRL) {
    if (g_regionStats.find(r) != g_regionStats.end() && 
        !g_regionStats[r].rewardWindow.empty() && 
        g_regionStats[r].rewardWindow.size() > 10) {
      double r_var = CalculateVariance(g_regionStats[r].rewardWindow);
      avg_reward_variance += r_var;
    }
    // Use recent TD statistics if available
    if (drlLocal.td_error_count > 10) {
      avg_td_abs_mean += std::abs(drlLocal.td_error_mean) + drlLocal.td_error_std;
    }
    // Get clipping frequency
    if (drlLocal.critic_update_count > 0) {
      avg_clip_frequency += drlLocal.critic_clip_frequency;
    }
    region_count++;
  }
  if (region_count > 0) {
    avg_reward_variance /= region_count;
    avg_td_abs_mean /= region_count;
    avg_clip_frequency /= region_count;
  }
  
  // PHASE 2: Improved exploration strategy - adaptive noise based on multiple factors
  // On low reward variance (< 0.001) → increase noise (not just slow decay)
  // This addresses policy stagnation by increasing exploration
  if (avg_reward_variance < 0.001 && g_flRound > 10) {
    double old_noise = g_explorationNoise;
    // PHASE 2: More aggressive noise increase when rewards are stagnant
    g_explorationNoise = std::min(0.3, g_explorationNoise * 1.25);  // Increase by 25% (was 15%), cap at 0.3
    // Simplified single-line output instead of box
    std::cout << "[t=" << std::fixed << std::setprecision(1) << simTime << "s][EXPLORATION] Noise increased: " 
              << std::setprecision(4) << old_noise << " → " << std::setprecision(4) << g_explorationNoise 
              << " | Variance=" << std::setprecision(6) << avg_reward_variance << std::endl;
  }
  
  // FIX: On high TD variance (> 10.0) → reduce actor LR (not noise)
  // TD variance indicates critic instability, not exploration issue
  // (This is handled per-region in TrainDRLAgent actor update section)
  
  // FIX: Noise decay only when conditions are met (not purely time-based)
  // Conditions: reward variance stable (0.001 < var < 0.01), loss stable, gradients mostly unclipped
  bool can_decay_noise = (avg_reward_variance > 0.001 && avg_reward_variance < 0.01) &&  // Reward variance stable
                         (avg_clip_frequency < 0.20) &&  // Gradients mostly unclipped (<20%)
                         (g_flRound > 10);  // After warmup
  
  double noise_decay_rate = 0.9995;  // Default (slow)
  if (can_decay_noise && avg_td_abs_mean < 5.0) {
    noise_decay_rate = 0.99;  // Faster decay when genuinely stable
  }
  
  // Apply decay only if conditions met
  if (can_decay_noise) {
    g_explorationNoise *= noise_decay_rate;
  }
  // REFACTORED: Use config for minimum exploration noise
  g_explorationNoise = std::max(g_fdrlccConfig.minExplorationNoise, g_explorationNoise);
  
  // Log exploration status (only when significant changes occur)
  static double last_logged_noise = -1.0;
  if (std::abs(g_explorationNoise - last_logged_noise) > 0.01 || simTime < 2.0) {
    last_logged_noise = g_explorationNoise;
    // Only log if noise changed significantly or at start
  }
  
  // Update training summary
  g_trainingSummary.flRounds = g_flRound;
  g_trainingSummary.finalDivergence = divergence_after;
  
  // Save checkpoints periodically (every 5 FL rounds)
  if (g_saveCheckpoints && (g_flRound % 5 == 0 || g_flRound <= 3)) {
    std::string checkpointDir = g_resultsDir + "/checkpoints";
    std::string mkdirCmd = "mkdir -p " + checkpointDir;
    system(mkdirCmd.c_str());
    
    // Save actor checkpoints for each region
    for (auto& [r, drlLocal] : g_regionDRL) {
      std::string actorFile = checkpointDir + "/actor_" + r + "_round_" + std::to_string(g_flRound) + ".txt";
      std::vector<double> actorWeights = drlLocal.actor.Serialize();
      std::ofstream actorOut(actorFile);
      if (actorOut.is_open()) {
        for (double w : actorWeights) {
          actorOut << std::setprecision(8) << w << " ";
        }
        actorOut.close();
        
        // Checkpoint saved (logged to file, no console spam)
      }
      
      // Save critic checkpoint
      std::string criticFile = checkpointDir + "/critic_" + r + "_round_" + std::to_string(g_flRound) + ".txt";
      std::vector<double> criticWeights = drlLocal.critic.Serialize();
      std::ofstream criticOut(criticFile);
      if (criticOut.is_open()) {
        for (double w : criticWeights) {
          criticOut << std::setprecision(8) << w << " ";
        }
        criticOut.close();
      }
    }
    
    // Save global model checkpoint
    std::string globalFile = checkpointDir + "/global_model_round_" + std::to_string(g_flRound) + ".txt";
    std::ofstream globalOut(globalFile);
    if (globalOut.is_open()) {
      for (double w : g_globalWeights) {
        globalOut << std::setprecision(8) << w << " ";
      }
      globalOut.close();
      
      // Global checkpoint saved (logged to file, no console spam)
    }
  }
  
  // FIX 6: Decay exploration noise with minimum enforced
  // TUNING: Noise decay now handled adaptively above (performance-aware, not time-based)
  // Removed static decay - noise now decays based on learning stability
  
  // Note: avgMixingRatio already calculated above (before FL metrics writing)
  // Note: fl-aggregation.csv removed (redundant with fl_metrics.csv)
  // All FL metrics are logged to fl_metrics.csv with more detailed information
  
  // Store latest FL metrics (used for console output and performance logger)
  g_lastFLDivergence = divergence_after;
  g_lastFLFairness = fairnessIndex;
  g_lastFLAvgMixing = avgMixingRatio;
  
  // ENHANCEMENT: Save weights after each FL round (for checkpointing, only if enabled)
  // Save checkpoints to simulation_results/checkpoints (shared) AND to current run's directory
  static std::string checkpointBaseDir = "src/ndnSIM/fdrlcc/results/simulation_results";
  
  if (g_saveCheckpoints && (g_flRound % 5 == 0 || g_flRound <= 3)) {
    // Save checkpoints every 5 rounds or for first 3 rounds
    
    // 1. Save to shared checkpoints directory (for loading in future runs)
    //    Round-specific files accumulate across runs (may overwrite if same round number)
    //    weights_latest.txt is always overwritten with most recent checkpoint
    SaveWeightsAfterFLRound(checkpointBaseDir, g_flRound);
    
    // 2. Also save to current simulation's results directory (preserves run-specific checkpoints)
    if (!g_resultsDir.empty()) {
      SaveWeightsAfterFLRound(g_resultsDir, g_flRound);
    }
  }
  
  // Calculate region weights for console monitor
  std::map<std::string, double> regionWeights;
  double totalPositiveRewardForWeights = 0.0;
  for (const auto& [region, reward] : regionRewards) {
    totalPositiveRewardForWeights += std::max(0.0, reward);
  }
  
  if (IsAdaptiveAggregationEnabled() && totalPositiveRewardForWeights > 1e-6) {
    // Calculate performance weights (same as in PerformanceWeightedFedAvg)
    for (const auto& [region, reward] : regionRewards) {
      double weight = std::max(0.0, reward) / totalPositiveRewardForWeights;
      regionWeights[region] = weight;
    }
  } else {
    // Uniform weights for standard FedAvg
    if (!regionRewards.empty()) {
      double uniformWeight = 1.0 / static_cast<double>(regionRewards.size());
      for (const auto& [region, reward] : regionRewards) {
        regionWeights[region] = uniformWeight;
      }
    }
  }
  
  // RESEARCH INSTRUMENTATION: Record FL contributions for each region
  for (const auto& [region, drl] : g_regionDRL) {
    if (drl.trainingStep == 0) continue;  // Skip inactive regions
    
    // Calculate local reward mean (from reward history)
    double localRewardMean = drl.reward;
    if (!drl.rewardHistory.empty()) {
      double sum = 0.0;
      for (double r : drl.rewardHistory) {
        sum += r;
      }
      localRewardMean = sum / drl.rewardHistory.size();
    }
    
    // Calculate model divergence (L2 norm of difference)
    double modelDivergence = 0.0;
    if (!g_globalWeights.empty()) {
      std::vector<double> localWeights = drl.actor.Serialize();
      if (localWeights.size() == g_globalWeights.size()) {
        double sumSqDiff = 0.0;
        for (size_t i = 0; i < localWeights.size(); ++i) {
          double diff = localWeights[i] - g_globalWeights[i];
          sumSqDiff += diff * diff;
        }
        modelDivergence = std::sqrt(sumSqDiff);
      }
    }
    
    // Get aggregation weight
    double aggregationWeight = 0.0;
    if (regionWeights.find(region) != regionWeights.end()) {
      aggregationWeight = regionWeights[region];
    }
    
    // Global reward after update (use average reward)
    double globalRewardAfter = avgReward;
    
    ResearchInstrumentation::RecordFLContribution(g_flRound, region, localRewardMean,
                                                 modelDivergence, aggregationWeight, globalRewardAfter);
  }
  
  if (ConsoleOutput::IsCompact()) {
    ConsoleOutput::UpdateOnFLRound(simTime, g_totalSimulationTime, g_flRound,
                                   divergence_after, avgReward, fairnessIndex);
  } else {
    EventLogger::LogFLAggregation(simTime, regionWeights.size(), regionWeights,
                                  divergence_after, avgReward);
    StatusDisplay::PrintStatus(true);
    std::cout << "[t=" << std::fixed << std::setprecision(1) << simTime << "s][FL] Round " << g_flRound
              << " complete | Regions=" << regions_participated
              << " | Fairness=" << std::setprecision(3) << fairnessIndex
              << " | Divergence=" << std::setprecision(4) << divergence_after
              << " | Reward=" << std::setprecision(3) << avgReward
              << " | Noise=" << std::setprecision(3) << g_explorationNoise << std::endl;
  }

  // RESEARCH INSTRUMENTATION: Record FL aggregation metrics
  std::ostringstream weightsStr;
  bool first = true;
  for (const auto& [region, weight] : regionWeights) {
    if (!first) weightsStr << ",";
    weightsStr << region << "=" << std::setprecision(4) << weight;
    first = false;
  }
  ResearchInstrumentation::RecordFLAggregationMetrics(simTime, regionWeights.size(), 
                                                      weightsStr.str(), divergence_after, avgReward);
  
  // ========================================================================
  // PerformanceLogger: Log FL Aggregation Event (event-driven)
  // ========================================================================
  auto& perfLogger = PerformanceLogger::GetInstance();
  
  // Calculate model update norm (Frobenius norm of weight differences)
  double modelUpdateNorm = 0.0;
  if (!localWeights.empty() && !g_globalWeights.empty()) {
    // Calculate average norm of differences between local and global weights
    // Simplified: use first region's weight differences
    auto firstRegionWeights = localWeights.begin()->second;
    if (firstRegionWeights.size() == g_globalWeights.size()) {
      double sumSqDiff = 0.0;
      for (size_t i = 0; i < g_globalWeights.size(); ++i) {
        double diff = firstRegionWeights[i] - g_globalWeights[i];
        sumSqDiff += diff * diff;
      }
      modelUpdateNorm = std::sqrt(sumSqDiff);
    }
  }
  
  // Estimate communication overhead (num_regions * model_size_bytes)
  int64_t communicationOverhead = -1;  // Default: unavailable
  if (!g_globalWeights.empty()) {
    // Estimate: serialized model size * number of regions
    size_t modelSizeBytes = g_globalWeights.size() * sizeof(double);
    communicationOverhead = static_cast<int64_t>(modelSizeBytes * g_regionDRL.size());
  }
  
  // Update divergence indicator in PerformanceLogger (propagates to AI metrics)
  perfLogger.UpdateDivergenceIndicator(divergence_after);
  
  PerformanceLogger::FLMetrics flMetrics;
  flMetrics.timestamp = simTime;
  flMetrics.flRound = g_flRound;
  flMetrics.globalModelDivergence = divergence_after;
  flMetrics.modelUpdateNorm = modelUpdateNorm;
  flMetrics.avgLocalReward = avgReward;
  flMetrics.communicationOverheadBytes = communicationOverhead;
  
  perfLogger.LogFLMetrics(flMetrics);
  
  // Schedule next FL round
  Simulator::Schedule(Seconds(g_flInterval), &PerformFederatedAggregation);
}


} // namespace fdrl
} // namespace ndn
} // namespace ns3
