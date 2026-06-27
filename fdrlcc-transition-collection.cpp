/**
 * fdrlcc-transition-collection.cpp
 * 
 * Transition collection for experience replay in FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-transition-collection.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-reward-calculation.hpp"
#include "fdrlcc-simulation-utils.hpp"
#include "fdrlcc-csv-management.hpp"
#include "fdrlcc-console-colors.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include "src_cpp/controller/fdrl-replay-buffer.hpp"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>

NS_LOG_COMPONENT_DEFINE("FdrlccTransitionCollection");

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Collect transition and push to replay buffer (called at every control step)
 * This ensures transitions are collected every 1s, not just every 5s (FL interval)
 */
void
CollectTransition(const std::string& region)
{
  auto& drl = g_regionDRL[region];
  double simTime = Simulator::Now().GetSeconds();
  
  // PHASE-1: Dynamic transition collection (NDN-aware)
  // ===================================================
  // In NDN, transitions should be collected when meaningful state changes occur,
  // not just at fixed intervals. However, for DRL we need regular transitions.
  // Solution: Collect immediately on first call (no delay), but ensure we have
  // both state and action before creating transition.
  
  // Initialize prevState/prevAction on first call (no skip - start immediately)
  if (!drl.hasPrevTransition) {
    // On first call, we have state and action from current step
    // Store them as "previous" for next transition
    drl.prevState = drl.state;
    drl.prevAction = drl.action;
    drl.hasPrevTransition = true;
    
    // PHASE-1: Track state diversity for adaptive warmup
    std::string stateSig = SerializeStateVector(drl.state);
    drl.uniqueStates.insert(stateSig);
    
    return;  // First call: just initialize, collect transition on next call
  }
  
  // REFACTORED: Get snapshot from MetricEngine (single source of truth)
  if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) {
    NS_LOG_WARN("MetricEngine not initialized for region " << region << " - skipping transition collection");
    return;
  }
  
  const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
  
  // FIX: Calculate reward using DRL reward function (consistent with training)
  double reward = CalculateDRLReward(region, snapshot);

  drl.reward = reward;

  constexpr double kRewardEmaBeta = 0.3;
  if (!drl.smoothedRewardInitialized) {
    drl.smoothedRewardForAggregation = reward;
    drl.smoothedRewardInitialized = true;
  } else {
    drl.smoothedRewardForAggregation =
        kRewardEmaBeta * reward
        + (1.0 - kRewardEmaBeta) * drl.smoothedRewardForAggregation;
  }

  drl.rewardHistory.push_back(reward);
  if (drl.rewardHistory.size() > 10) {
    drl.rewardHistory.erase(drl.rewardHistory.begin());
  }
  
  // PHASE-1: Track state diversity for adaptive warmup
  std::string stateSig = SerializeStateVector(drl.state);
  drl.uniqueStates.insert(stateSig);
  
  // PHASE-1: Update measured RTT from metrics (NDN-aware timing)
  // REFACTORED: Use avgDelayMs from MetricEngine snapshot
  // Use average delay as RTT estimate (Interest-Data round trip time)
  double currentRTT = snapshot.avgDelayMs;
  if (currentRTT > 0.1) {  // Valid RTT measurement
    // Update RTT history (circular buffer)
    drl.rttHistory[drl.rttHistoryIdx] = currentRTT;
    drl.rttHistoryIdx = (drl.rttHistoryIdx + 1) % 10;
    if (drl.rttHistorySize < 10) {
      drl.rttHistorySize++;
    }
    
    // Calculate smoothed RTT (exponential moving average)
    double sumRTT = 0.0;
    for (size_t i = 0; i < drl.rttHistorySize; i++) {
      sumRTT += drl.rttHistory[i];
    }
    drl.measuredRTT = sumRTT / static_cast<double>(drl.rttHistorySize);
    
    // PHASE-1: Adaptive action interval based on RTT (NDN-aware)
    // Action interval should be at least 2x RTT to observe action effects
    // But not too long (max 5s) to maintain responsiveness
    double minInterval = std::max(0.2, (drl.measuredRTT / 1000.0) * 2.0);  // 2x RTT, min 200ms
    double maxInterval = 5.0;  // Max 5s for responsiveness
    drl.adaptiveActionInterval = std::min(maxInterval, std::max(minInterval, drl.measuredRTT / 500.0));
    
    // PHASE-1: Adaptive warmup size based on experience diversity
    // More diverse states → can start training earlier
    // Less diverse states → need more samples
    double diversityRatio = static_cast<double>(drl.uniqueStates.size()) / 
                           static_cast<double>(std::max(1UL, drl.replayBuffer.Size() + 1));
    drl.targetWarmupSize = std::max(50.0, std::min(150.0, 100.0 / std::max(0.1, diversityRatio)));
  } else {
    // No valid RTT yet - use default
    drl.adaptiveActionInterval = 1.0;  // Default 1s until RTT measured
    drl.targetWarmupSize = 100.0;       // Default warmup size
  }
  
  // FIX 1: Push transition to replay buffer AT EVERY CONTROL STEP
  Transition transition(drl.prevState, drl.prevAction, reward, drl.state, false);
  drl.replayBuffer.Add(transition);
  
  size_t bufferSize = drl.replayBuffer.Size();
  
  // PHASE-1: Use adaptive warmup size (not hardcoded 100)
  size_t effectiveWarmupSize = static_cast<size_t>(std::max(drl.minWarmupSize, drl.targetWarmupSize));
  
  // FIX 1: Log buffer push only at major milestones (50%, 100% of warmup) - reduced verbosity
  static std::map<std::string, double> last_logged_milestone;
  if (last_logged_milestone.find(region) == last_logged_milestone.end()) {
    last_logged_milestone[region] = 0.0;
  }
  double current_milestone = (bufferSize > 0 && effectiveWarmupSize > 0) ? 
      (static_cast<double>(bufferSize) / static_cast<double>(effectiveWarmupSize)) * 100.0 : 0.0;
  
  // Log only at major milestones: 50%, 100% (reduced from 6 milestones to 2)
  bool should_log = (current_milestone >= 50.0 && last_logged_milestone[region] < 50.0) ||
                    (current_milestone >= 100.0 && last_logged_milestone[region] < 100.0);
  
  if (should_log) {
    double diversityPercent = (bufferSize > 0) ? 
        (static_cast<double>(drl.uniqueStates.size()) / static_cast<double>(bufferSize)) * 100.0 : 0.0;
    double simTime = Simulator::Now().GetSeconds();
    // Simplified single-line output instead of box
    std::cout << "[t=" << std::fixed << std::setprecision(1) << simTime << "s][BUFFER] Region=" << region
              << " | Size=" << bufferSize << "/" << effectiveWarmupSize 
              << " (" << std::setprecision(1) << current_milestone << "%) | Diversity=" << std::setprecision(1) << diversityPercent << "%" << std::endl;
    last_logged_milestone[region] = current_milestone;
  }
  
  // ============================================================================
  // PART 1: EXPERIENCE DATASET LOGGING (MANDATORY, with optional subsampling)
  // ============================================================================
  // STEP 3: Subsampling support - log every N transitions based on config
  // Replay buffer MUST remain unchanged (this ONLY affects disk logging)
  static std::map<std::string, uint32_t> transitionCount;  // Per-region counter
  if (transitionCount.find(region) == transitionCount.end()) {
    transitionCount[region] = 0;
  }
  transitionCount[region]++;
  
  // Only log if interval matches (or if interval is 1, log every transition)
  bool shouldLog = (g_fdrlccConfig.experienceLogInterval == 1) || 
                   (transitionCount[region] % g_fdrlccConfig.experienceLogInterval == 0);
  
  if (g_experienceDatasetCsv.is_open() && shouldLog) {
    // Get executed action (after noise/clipping) - stored in prevAction
    double executed_action = drl.prevAction[0];
    
    g_experienceDatasetCsv << std::fixed << std::setprecision(6)
                          << simTime << ","                                    // timestamp
                          << g_ablationConfig.ablationLabel << ","             // ablation_config
                          << drl.trainingStep << ","                         // training_step (REFACTORED: removed global step)
                          << region << ","                                    // region_id
                          << drl.prevState.size() << ","                      // state_dim
                          << SerializeStateVector(drl.prevState) << ","        // state
                          << drl.prevAction.size() << ","                     // action_dim
                          << drl.prevAction[0] << ","                         // action (raw actor output)
                          << executed_action << ","                           // executed_action (after noise/clipping)
                          << reward << ","                                    // reward
                          << SerializeStateVector(drl.state) << ","           // next_state
                          << "0,"                                             // done (0 for continuing, 1 for terminal)
                          << g_currentEpisodeId << std::endl;                 // episode_id
    FlushCsvFiles();
  }
  
  // NOTE: Removed state_transition_log.csv logging (redundant with experience_dataset.csv)
  
  // REFACTORED: Use unified RegionTrainingStats
  if (g_regionStats.find(region) == g_regionStats.end()) {
    g_regionStats[region] = RegionTrainingStats();
  }
  g_regionStats[region].UpdateReward(reward);
  
  // REMOVED: training_rewards.csv logging - now merged into training_metrics.csv
  // Rewards are written directly to training_metrics.csv during training step
  
  // Update for next step
  drl.prevState = drl.state;
  drl.prevAction = drl.action;
  drl.prevReward = reward;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

