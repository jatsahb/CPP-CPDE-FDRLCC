/**
 * fdrlcc-csv-management.cpp
 * 
 * CSV file management functions for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-csv-management.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-enhanced-metrics.hpp"
#include "fdrlcc_Results.hpp"
#include <fstream>
#include <iostream>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Periodic CSV flush (buffered I/O)
 */
void
FlushCsvFiles()
{
  if (g_csvWriteCount % CSV_FLUSH_INTERVAL == 0) {
    if (g_experienceDatasetCsv.is_open()) g_experienceDatasetCsv.flush();
    if (g_trainingMetricsCsv.is_open()) g_trainingMetricsCsv.flush();
    // REMOVED: g_policyActionsCsv - replaced by StructuredLogger
    if (g_flMetricsCsv.is_open()) g_flMetricsCsv.flush();
    // REMOVED: g_queueMetricsCsv - replaced by StructuredLogger
    // REMOVED: g_trainingStatusCsv - redundant with training_metrics.csv
    // REMOVED: g_gradientMetricsCsv, g_tdErrorMetricsCsv, g_fairnessMetricsCsv (merged into other files)
  }
  g_csvWriteCount++;
}

/**
 * Initialize training CSV files
 */
void
InitializeTrainingCsvFiles(const std::string& resultsDir)
{
  // REMOVED: training_rewards.csv - merged into training_metrics.csv
  // Training rewards are now written directly to training_metrics.csv
  
  // NOTE: Removed redundant CSV files:
  // - training_losses.csv (redundant with training_metrics.csv)
  // - actions_log.csv (redundant with policy_actions.csv)
  // - state_transition_log.csv (redundant with experience_dataset.csv)
  
  // STEP 5: FL Metrics CSV with causal benefit (mean_local_reward, reward_gain_after_aggregation)
  // CONSOLIDATED: Merges fl-aggregation.csv + fl_metrics.csv + fairness_metrics.csv
  g_flMetricsCsv.open(resultsDir + "/fl_metrics.csv", std::ios::out | std::ios::trunc);
  if (g_flMetricsCsv.is_open()) {
    g_flMetricsCsv << "timestamp,ablation_config,fl_round,regions_participated,aggregation_strategy,divergence_before,divergence_after,divergence_delta,global_model_version,aggregation_time_ms,global_reward,mean_local_reward,reward_gain_after_aggregation,noise_level,fairness_index,avg_mixing_ratio" << std::endl;
    std::cout << "✓ FL metrics logging enabled (fl_metrics.csv - consolidated)" << std::endl;
  }
  
  // Experience Dataset CSV (MANDATORY)
  g_experienceDatasetCsv.open(resultsDir + "/experience_dataset.csv", std::ios::out | std::ios::app);
  if (g_experienceDatasetCsv.is_open()) {
    // Write header only if file is new (check file size)
    std::ifstream checkFile(resultsDir + "/experience_dataset.csv");
    checkFile.seekg(0, std::ios::end);
    if (checkFile.tellg() == 0) {
      g_experienceDatasetCsv << "timestamp,ablation_config,global_step,region_id,state_dim,state,action_dim,action,executed_action,reward,next_state,done,episode_id" << std::endl;
    }
    checkFile.close();
    std::cout << "✓ Experience dataset logging enabled (experience_dataset.csv)" << std::endl;
  }
  
  // STEP 4: Training Metrics Dataset CSV with learning evidence (reward_delta, smoothed_reward)
  // CONSOLIDATED: Merges training_rewards.csv + training_metrics.csv + gradient_metrics.csv + td_error_metrics.csv
  g_trainingMetricsCsv.open(resultsDir + "/training_metrics.csv", std::ios::out | std::ios::trunc);
  if (g_trainingMetricsCsv.is_open()) {
    g_trainingMetricsCsv << "timestamp,ablation_config,global_step,region_id,training_step_id,"
                          << "actor_loss,critic_loss,total_loss,"
                          << "q_value_mean,q_value_std,"
                          << "reward_mean_window,reward_variance_window,"
                          << "learning_rate_actor,learning_rate_critic,"
                          << "reward,episode_reward,reward_ma,"
                          << "reward_delta,smoothed_reward,"  // STEP 4: Learning evidence
                          << "critic_norm_preclip,critic_norm_postclip,"
                          << "actor_norm_preclip,actor_norm_postclip,"
                          << "clipping_frequency,clip_count,update_count,"
                          << "td_error_mean,td_error_std,td_error_count,td_error_min,td_error_max" << std::endl;
    std::cout << "✓ Training metrics logging enabled (training_metrics.csv - consolidated)" << std::endl;
  }
  
  // REMOVED: Policy Actions Trace CSV - replaced by StructuredLogger
  // StructuredLogger handles action logging via LogDRLAction() in logs/drl/action_*.csv
  
  // Initialize training status engine
  g_trainingStatus.isActive = false;
  g_trainingStatus.isStalled = false;
  g_csvWriteCount = 0;
  std::cout << "✓ Training status monitoring engine initialized" << std::endl;
  
  // Initialize training summary
  g_trainingSummary.totalTrainingSteps = 0;
  g_trainingSummary.flRounds = 0;
  g_trainingSummary.initialReward = 0.0;
  g_trainingSummary.finalReward = 0.0;
  g_trainingSummary.initialLoss = 0.0;
  g_trainingSummary.finalLoss = 0.0;
  g_trainingSummary.finalDivergence = 0.0;
  
  // Initialize fail-safe tracking
  for (const auto& region : g_topologyInfo.regions) {
    g_lastReward[region] = 0.0;
    g_constantRewardCount[region] = 0;
    g_lastAction[region] = 0.0;
    g_constantActionCount[region] = 0;
    g_replayBufferFilled[region] = false;
    g_regionNoTrainingCount[region] = 0;
    // REFACTORED: Use unified RegionTrainingStats
    if (g_regionStats.find(region) == g_regionStats.end()) {
      g_regionStats[region] = RegionTrainingStats();
    }
    g_regionStats[region].lastReward = 0.0;
    g_regionStats[region].rewardWindow.clear();
    g_regionStats[region].rewardHistory.clear();
    g_regionStats[region].episodeReward = 0.0;
    g_regionLastWeights[region].clear();
    g_trainingSummary.regionTrainingSteps[region] = 0;
    g_trainingSummary.regionFinalRewards[region] = 0.0;
  }
  
  std::cout << "✓ Training instrumentation CSV files initialized" << std::endl;
  
  // Initialize enhanced metrics CSV files
  InitializeEnhancedMetricsCsv(resultsDir);
  
  // NOTE: PhD Framework files are initialized separately with scenario/run info
  // Call InitializePhdFrameworkFiles(resultsDir, scenario, runNumber) from main
}

/**
 * Close training CSV files
 */
void
CloseTrainingCsvFiles()
{
  // REMOVED: g_trainingRewardsCsv (merged into training_metrics.csv)
  // NOTE: Removed closing code for merged CSV files (see InitializeTrainingCsvFiles)
  if (g_flMetricsCsv.is_open()) {
    g_flMetricsCsv.close();
  }
  if (g_experienceDatasetCsv.is_open()) {
    g_experienceDatasetCsv.close();
  }
  if (g_trainingMetricsCsv.is_open()) {
    g_trainingMetricsCsv.close();
  }
  // REMOVED: g_policyActionsCsv - replaced by StructuredLogger
  
  // Close enhanced metrics CSV files
  CloseEnhancedMetricsCsv();
  
  // Close PhD Framework CSV files
  ClosePhdFrameworkFiles();
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

