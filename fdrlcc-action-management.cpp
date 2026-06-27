/**
 * fdrlcc-action-management.cpp
 * 
 * Action selection and management for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-action-management.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-csv-management.hpp"
#include "fdrlcc-state-extraction.hpp"
#include "fdrlcc-transition-collection.hpp"
#include "fdrlcc-training-logic.hpp"
#include "fdrlcc-research-instrumentation.hpp"  // Research instrumentation
#include "fdrlcc-event-logger.hpp"  // Event-driven console logger
#include "fdrlcc-structured-logger.hpp"  // OPTION 3.5: Structured logging
#include "fdrlcc-simulation-utils.hpp"
#include "fdrlcc-console-colors.hpp"
#include "src_cpp/apps/fdrl-consumer.hpp"
#include "src_cpp/control/action-executor.hpp"  // REFACTORED: Use ActionExecutor
#include "ns3/simulator.h"
#include "ns3/log.h"
#include <random>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <cmath>

NS_LOG_COMPONENT_DEFINE("FdrlccActionManagement");

namespace ns3 {
namespace ndn {
namespace fdrl {

// Random number generator for exploration noise (declared in fdrlcc-types.hpp)

/**
 * Select heuristic action (used for ablation: disable DRL)
 * Extracted from fdrl-controller.cpp heuristic logic
 */
std::vector<double>
SelectHeuristicAction(const std::string& region)
{
  std::vector<double> action(1, 1.0);
  
  // Get metrics from MetricEngine
  if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) {
    return action;  // Default action if metrics unavailable
  }
  
  const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
  double congestion = snapshot.queueOccupancy;
  double throughput = snapshot.throughputMbps;
  double latency = snapshot.avgDelayMs;
  double queueOcc = snapshot.queueOccupancy;
  double dropRatio = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    dropRatio = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
  }
  
  // Heuristic logic (from fdrl-controller.cpp)
  if (congestion > 0.8 || dropRatio > 0.15) {
    // High congestion: reduce rate significantly
    action[0] = 0.7 + (congestion - 0.8) * 0.5;  // 0.7 to 0.9 range
  }
  else if (congestion > 0.5 || dropRatio > 0.05) {
    // Moderate congestion: slight reduction
    action[0] = 0.85 + (congestion - 0.5) * 0.5;  // 0.85 to 1.0 range
  }
  else if (throughput < 0.05 && latency < 50.0) {
    // Low throughput but low latency: can increase rate
    action[0] = 1.1 + std::min(0.2, (0.05 - throughput) * 4.0);  // 1.1 to 1.3 range
  }
  else if (latency > 100.0 || queueOcc > 0.8) {
    // High latency or queue: reduce rate
    double latencyFactor = std::min(1.0, latency / 200.0);  // Normalize to 0-1
    action[0] = 0.8 + (1.0 - latencyFactor) * 0.3;  // 0.8 to 1.1 range
  }
  else {
    // Normal operation: slight variation based on metrics
    double metricVariation = (congestion * 0.3) + (queueOcc * 0.2) + (dropRatio * 0.5);
    action[0] = 0.95 + metricVariation * 0.1;  // 0.95 to 1.05 range
  }
  
  // Clamp to valid range
  action[0] = std::max(ActionExecutor::MIN_ACTION,
                       std::min(ActionExecutor::MAX_ACTION, action[0]));
  
  return action;
}

/**
 * Select action using DDPG Actor network with exploration noise
 * ABLATION 1: If disableDRL is true, use heuristic instead
 */
std::vector<double>
SelectAction(const std::string& region, const std::vector<double>& state)
{
  // ABLATION 1: Disable DRL - use heuristic control instead
  if (g_ablationConfig.disableDRL) {
    return SelectHeuristicAction(region);
  }
  
  auto& drl = g_regionDRL[region];
  std::vector<double> action(1, 1.0);
  double simTime = Simulator::Now().GetSeconds();
  
  // Actor network forward pass (output in [MIN_ACTION, MAX_ACTION])
  double baseAction = drl.actor.Forward(state);

  baseAction = std::max(ActionExecutor::MIN_ACTION,
                        std::min(ActionExecutor::MAX_ACTION, baseAction));
  
  // REFACTORED: Use config for minimum exploration noise
  double effective_noise = std::max(g_explorationNoise, g_fdrlccConfig.minExplorationNoise);
  
  // Increase noise when action is near boundaries (stuck prevention)
  if (baseAction < 0.6) {
    // Near minimum: increase noise to allow escape
    effective_noise *= 1.5;  // 50% more noise when near minimum
  } else if (baseAction > 1.9) {
    // Near maximum: increase noise slightly
    effective_noise *= 1.2;
  }
  effective_noise = std::min(effective_noise, 0.5);  // Cap noise at 0.5
  
  double noise = g_normalDist(g_rng) * effective_noise;
  double applied_action = std::max(ActionExecutor::MIN_ACTION,
                                   std::min(ActionExecutor::MAX_ACTION, baseAction + noise));
  action[0] = applied_action;
  
  // ============================================================================
  // PART 4: POLICY ACTIONS TRACE
  // ============================================================================
  // REMOVED: g_policyActionsCsv logging - replaced by StructuredLogger
  // StructuredLogger handles action logging via LogDRLAction() in logs/drl/action_*.csv
  // NOTE: Removed actions_log.csv logging (redundant with StructuredLogger)
  
  // CRITICAL FIX: Check for constant actions and increase exploration if stuck
  if (std::abs(applied_action - g_lastAction[region]) < 1e-6) {
    g_constantActionCount[region]++;
    if (g_constantActionCount[region] > 10) {
      // Increase exploration noise when actions are stuck (prevents getting stuck at min/max)
      double oldNoise = g_explorationNoise;
      g_explorationNoise = std::min(0.5, g_explorationNoise * 1.2);  // Increase by 20%
      std::cout << ColorWarning("[STUCK_ACTION]") << " Region=" << region 
                << " | Action constant for " << g_constantActionCount[region] 
                << " steps! Value=" << applied_action 
                << " | Increasing exploration: " << oldNoise << " -> " << g_explorationNoise << std::endl;
    }
    if (g_constantActionCount[region] > 50) {
      std::cerr << ColorError("[ERROR]") << " Region=" << region << " | Action constant for " 
                << g_constantActionCount[region] << " steps! Value=" << applied_action << std::endl;
    }
  } else {
    g_constantActionCount[region] = 0;
    // Reset exploration noise gradually when actions start varying again
    // REFACTORED: Use config for minimum exploration noise
    if (g_explorationNoise > g_fdrlccConfig.minExplorationNoise * 1.5) {
      g_explorationNoise = std::max(g_fdrlccConfig.minExplorationNoise, g_explorationNoise * 0.95);
    }
  }
  g_lastAction[region] = applied_action;
  
    // ENHANCEMENT: Log DDPG inference details (only to file, not console)
    if (g_consoleLog.is_open() && g_saveDetailedLogs) {
      g_consoleLog << "[AI-CPP] Region=" << region << ": DDPG inference - base_action=" 
                   << std::setprecision(4) << baseAction << ", noise=" << noise 
                   << ", final_action=" << action[0] << ", exploration_noise_level=" 
                   << g_explorationNoise << std::endl;
      g_consoleLog.flush();
    }
  
  return action;
}

/**
 * Apply FDRLCC actions to consumers (called every second)
 * FIX: Now collects transitions and triggers training at every control step
 */
void
ApplyFDRLCCActions()
{
  double simTime = Simulator::Now().GetSeconds();
  
  for (auto& [region, drl] : g_regionDRL) {
    // FIX: Increment control step counter
    drl.controlStep++;
    
    // Extract current state
    drl.state = ExtractState(region);
    
    // ENHANCEMENT: Log state being sent to DDPG model (only to file, not console)
    if (g_consoleLog.is_open() && g_saveDetailedLogs) {
      g_consoleLog << "[AI-CPP] Region=" << region << " @ t=" << std::fixed << std::setprecision(2) << simTime 
                   << "s: Sending STATE to DDPG model [5 dims]: ";
      for (size_t i = 0; i < drl.state.size(); ++i) {
        g_consoleLog << std::setprecision(4) << drl.state[i];
        if (i < drl.state.size() - 1) g_consoleLog << ", ";
      }
      g_consoleLog << std::endl;
      g_consoleLog.flush();
    }
    
    // Select action and apply soft cap on rate-factor growth
    drl.action = SelectAction(region, drl.state);
    double requested = drl.action[0];
    const double prevFactor = drl.lastAppliedRateFactor;
    requested = std::max(prevFactor - ActionExecutor::MAX_RATE_FACTOR_STEP,
                         std::min(prevFactor + ActionExecutor::MAX_RATE_FACTOR_STEP, requested));
    requested = std::max(ActionExecutor::MIN_ACTION,
                       std::min(ActionExecutor::MAX_ACTION, requested));
    drl.action[0] = requested;
    drl.rateFactor = requested;
    
    // RESEARCH INSTRUMENTATION: Record pre-action state for causal analysis
    if (g_metricEngine && g_metricEngine->IsRegionInitialized(region)) {
      const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
      double rttBefore = snapshot.avgDelayMs;
      double queueBefore = snapshot.queueOccupancy;
      double lossBefore = (snapshot.totalInterestsSent > 0) ? 
          (static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent)) : 0.0;
      ResearchInstrumentation::RecordPreActionState(simTime, region, rttBefore, queueBefore, lossBefore);
    }
    
    // REFACTORED: Use config for minimum exploration noise
    double effective_noise = std::max(g_explorationNoise, g_fdrlccConfig.minExplorationNoise);
    if (g_consoleLog.is_open() && g_saveDetailedLogs) {
      g_consoleLog << "[NOISE] Region=" << region << " | sigma=" << std::setprecision(4) 
                   << effective_noise << " (base=" << g_explorationNoise << ", min=" << g_fdrlccConfig.minExplorationNoise << ")" << std::endl;
      g_consoleLog.flush();
    }
    
    // ENHANCEMENT: Log action received from DDPG model (only to file, not console)
    if (g_consoleLog.is_open() && g_saveDetailedLogs) {
      g_consoleLog << "[AI-CPP] Region=" << region << " @ t=" << std::fixed << std::setprecision(2) << simTime 
                   << "s: Received ACTION from DDPG model: rate_factor=" << std::setprecision(4) << drl.rateFactor 
                   << " (action[0]=" << drl.action[0] << ")" << std::endl;
      g_consoleLog.flush();
    }
    
    // RESEARCH INSTRUMENTATION: Record agent decision
    double currentReward = drl.reward;
    ResearchInstrumentation::RecordAgentDecision(simTime, region, drl.state, drl.rateFactor, effective_noise, currentReward);
    
    // EVENT LOGGER: Phase 3 - DRL Reaction (log when action is selected)
    // Will log after action is applied successfully
    
    // REFACTORED: Use ActionExecutor - fixes action-actuator gap (STEP 6)
    // CRITICAL: Validate BEFORE applying - reject invalid actions (no training)
    if (!ActionExecutor::ValidateAction(drl.rateFactor)) {
      // Invalid action - REJECT and DO NOT TRAIN
      NS_LOG_WARN("Region " << region << ": Invalid action " << drl.rateFactor 
                            << " - REJECTING (will not train on this step)");
      
      // Log rejection
      if (g_consoleLog.is_open() && g_saveDetailedLogs) {
        g_consoleLog << "[ACTION_REJECT] Region=" << region 
                     << " | rateFactor=" << drl.rateFactor 
                     << " | reason=OUT_OF_BOUNDS" << std::endl;
        g_consoleLog.flush();
      }
      
      // Skip training for this step (invalid action)
      // Don't update replay buffer or train on invalid actions
      continue;  // Skip to next region
    }
    
    // Apply action using ActionExecutor (validated action)
    std::vector<ActionExecutionReport> reports = ActionExecutor::ApplyActionToRegion(region, drl.rateFactor);
    
    // Verify all actions were applied correctly
    bool allValid = true;
    for (const auto& report : reports) {
      if (!report.IsValid()) {
        allValid = false;
        NS_LOG_WARN("Region " << region << ": Action application failed - " 
                              << report.errorMessage);
      }
    }
    
    // If any action failed, REJECT and DO NOT TRAIN
    if (!allValid || reports.empty()) {
      NS_LOG_WARN("Region " << region << ": Action application failed - REJECTING (will not train)");
      
      // Log failure
      if (g_consoleLog.is_open() && g_saveDetailedLogs) {
        for (const auto& report : reports) {
          if (!report.IsValid()) {
            g_consoleLog << "[ACTION_FAIL] Region=" << region 
                         << " | rateFactor=" << report.requestedRateFactor
                         << " | error=" << report.errorMessage << std::endl;
          }
        }
        if (reports.empty()) {
          g_consoleLog << "[ACTION_FAIL] Region=" << region 
                       << " | reason=NO_CONSUMERS_IN_REGION" << std::endl;
        }
        g_consoleLog.flush();
      }
      
      // Skip training for this step (failed action)
      continue;  // Skip to next region
    }

    drl.lastAppliedRateFactor = reports.front().appliedRateFactor;
    
    // REMOVED: Verbose debug output - [ACTION_APPLY]
    
    // EVENT LOGGER: Phase 3 - DRL Reaction (log when action is successfully applied)
    EventLogger::LogDRLAction(simTime, region, drl.rateFactor, drl.reward);
    
    // OPTION 3.5: Structured logging - Log DRL action
    if (g_enableStructuredLogs && g_structuredLogger) {
      // Calculate send rate from reports (average effective frequency after)
      double avgSendRate = 0.0;
      if (!reports.empty()) {
        for (const auto& report : reports) {
          avgSendRate += report.effectiveFrequencyAfter;
        }
        avgSendRate /= reports.size();
      }
      
      // Get action values from SelectAction (baseAction, applied_action)
      double baseAction = drl.action[0];  // This is applied_action after SelectAction
      double noise = 0.0;  // Would need to pass from SelectAction if needed
      
      // Determine action reason
      std::string actionReason = "DDPG_EXPLORATION";
      if (baseAction >= g_fdrlccConfig.minAction && baseAction <= g_fdrlccConfig.maxAction) {
        actionReason = "DDPG_NORMAL";
      } else if (baseAction < g_fdrlccConfig.minAction) {
        actionReason = "CLIPPED_MIN";
      } else if (baseAction > g_fdrlccConfig.maxAction) {
        actionReason = "CLIPPED_MAX";
      }
      
      g_structuredLogger->LogDRLAction(region,
                                       baseAction,  // action_raw (from actor)
                                       drl.rateFactor,  // action_clipped (final applied, may be clamped)
                                       drl.rateFactor,  // rate_factor
                                       avgSendRate,  // send_rate (Hz)
                                       actionReason,
                                       simTime);
    }
    
    // RESEARCH INSTRUMENTATION: Record DRL agent metrics
    std::ostringstream stateSummary;
    for (size_t i = 0; i < drl.state.size(); ++i) {
      if (i > 0) stateSummary << ",";
      stateSummary << std::setprecision(4) << drl.state[i];
    }
    ResearchInstrumentation::RecordDRLAgentMetrics(simTime, region, stateSummary.str(), 
                                                   drl.rateFactor, drl.reward, effective_noise);
    
    // Log to file for detailed tracking
    if (g_consoleLog.is_open() && g_saveDetailedLogs) {
      for (const auto& report : reports) {
        g_consoleLog << "[ACTION_APPLY] Region=" << region 
                     << " | rateFactor=" << std::setprecision(3) << report.appliedRateFactor
                     << " | baseFreq=" << report.baseFrequency << " Hz"
                     << " | effectiveFreq_before=" << report.effectiveFrequencyBefore << " Hz"
                     << " | effectiveFreq_after=" << report.effectiveFrequencyAfter << " Hz"
                     << " | status=" << (report.IsValid() ? "SUCCESS" : "FAILED") << std::endl;
      }
      g_consoleLog.flush();
    }
    
    // FIX 1: Collect transition from previous step (state_t, action_t, reward, state_t+1)
    // This happens AFTER applying action, so we observe the effect
    CollectTransition(region);
    
    // RESEARCH INSTRUMENTATION: Record post-action state and compute deltas
    if (g_metricEngine && g_metricEngine->IsRegionInitialized(region)) {
      // Wait a small interval to observe action effect (use next metric collection)
      // For now, record immediately - actual effect will be visible in next window
      const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
      double rttAfter = snapshot.avgDelayMs;
      double queueAfter = snapshot.queueOccupancy;
      double lossAfter = (snapshot.totalInterestsSent > 0) ? 
          (static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent)) : 0.0;
      ResearchInstrumentation::RecordPostActionState(simTime, region, drl.rateFactor, rttAfter, queueAfter, lossAfter);
    }
    
    // FIX 2: Train agent independently of FL rounds
    TrainDRLAgent(region);
    
    // PART 5: Check training status periodically (every 10 control steps)
    if (drl.controlStep % 10 == 0) {
      CheckTrainingStatus(region, simTime);
    }
    
    // FIX 5: Debug assertion - verify state changed or reward is non-zero
    if (drl.hasPrevTransition && drl.controlStep > 1) {
      bool stateChanged = false;
      for (size_t i = 0; i < drl.state.size() && i < drl.prevState.size(); ++i) {
        if (std::abs(drl.state[i] - drl.prevState[i]) > 1e-6) {
          stateChanged = true;
          break;
        }
      }
      if (!stateChanged && std::abs(drl.prevReward) < 1e-6) {
        std::cerr << ColorWarning("[WARNING]") << " Region=" << region << " | State unchanged AND reward zero! "
                  << "This may indicate a problem with the environment." << std::endl;
      }
    }
    
    NS_LOG_DEBUG("Region " << region << " @ t=" << simTime << ": rate_factor=" << drl.rateFactor);
  }
  
  // FIX 8: Hard fail-safes
  for (auto& [region, drl] : g_regionDRL) {
    size_t bufferSize = drl.replayBuffer.Size();
    
    // PHASE-1: Use adaptive warmup size (not hardcoded 100)
    size_t effectiveWarmupSize = static_cast<size_t>(std::max(drl.minWarmupSize, drl.targetWarmupSize));
    
    // Fail-safe 1: Buffer too small after 30s (informational, not critical)
    // Only log once per region when buffer reaches 50% of warmup
    static std::map<std::string, bool> buffer_logged;
    if (bufferSize < effectiveWarmupSize && simTime > 30.0) {
      double progressPercent = (static_cast<double>(bufferSize) / static_cast<double>(effectiveWarmupSize)) * 100.0;
      if (!buffer_logged[region] && progressPercent >= 50.0) {
        double diversityPercent = (bufferSize > 0) ? 
            (static_cast<double>(drl.uniqueStates.size()) / static_cast<double>(bufferSize)) * 100.0 : 0.0;
        std::cout << ColorOutput::Info() << "╔══════════════════════════════════════════════════════════════════════════════╗" << ColorOutput::Reset() << std::endl;
        std::cout << ColorOutput::Info() << "║" << ColorOutput::Reset() << " [t=" << std::fixed << std::setprecision(1) << simTime << "s] " 
                  << ColorOutput::Info() << "BUFFER FILLING" << ColorOutput::Reset() << " | Region=" << region << ColorOutput::Info() 
                  << std::string(std::max(0, 40 - (int)region.length()), ' ') << "║" << ColorOutput::Reset() << std::endl;
        std::cout << ColorOutput::Info() << "╠══════════════════════════════════════════════════════════════════════════════╣" << ColorOutput::Reset() << std::endl;
        std::cout << ColorOutput::Info() << "║" << ColorOutput::Reset() << " Progress: " << std::setprecision(1) << progressPercent 
                  << "% (" << bufferSize << "/" << effectiveWarmupSize << ") | Diversity: " << std::setprecision(1) << diversityPercent << "%" 
                  << ColorOutput::Info() << std::string(std::max(0, 20), ' ') << "║" << ColorOutput::Reset() << std::endl;
        std::cout << ColorOutput::Info() << "╚══════════════════════════════════════════════════════════════════════════════╝" << ColorOutput::Reset() << std::endl;
        buffer_logged[region] = true;
      }
    }
    
    // REFACTORED: Use unified RegionTrainingStats
    if (g_regionStats.find(region) != g_regionStats.end() && 
        g_regionStats[region].rewardWindow.size() >= 200) {
      double mean = 0.0;
      for (double r : g_regionStats[region].rewardWindow) {
        mean += r;
      }
      mean /= g_regionStats[region].rewardWindow.size();
      
      double variance = 0.0;
      for (double r : g_regionStats[region].rewardWindow) {
        variance += (r - mean) * (r - mean);
      }
      variance /= g_regionStats[region].rewardWindow.size();
      
      if (variance < 1e-10) {
        // Throttle: log at most once per region per 60s to avoid console spam
        static std::map<std::string, double> last_variance_error_logged;
        double& last = last_variance_error_logged[region];
        if (last <= 0.0 || (simTime - last) >= 60.0) {
          last = simTime;
          std::cerr << ColorError("[ERROR]") << " Region=" << region << " | Reward variance == 0 for >200 steps! "
                    << "Rewards are constant - model is not learning! (throttled: once per 60s)" << std::endl;
        }
      }
    }
    
    // Fail-safe 3: No training calls
    g_regionNoTrainingCount[region]++;
    if (g_regionNoTrainingCount[region] > 1000 && bufferSize >= effectiveWarmupSize) {
      std::cerr << ColorError("[ERROR]") << " Region=" << region << " | No training calls for >1000 steps "
                << "despite buffer ready! Training loop is broken!" << std::endl;
      g_regionNoTrainingCount[region] = 0;  // Reset to avoid spam
    }
  }
  
  // PHASE-1: Schedule next action application with ADAPTIVE interval (NDN-aware)
  // ============================================================================
  // Action interval adapts to measured RTT:
  // - Fast networks (low RTT): More frequent actions (faster response)
  // - Slow networks (high RTT): Less frequent actions (wait for effects)
  // - Ensures actions have observable effects before next action
  // 
  // Use the minimum adaptive interval across all regions (most responsive)
  double minInterval = 1.0;  // Default 1s
  for (const auto& [region, drl] : g_regionDRL) {
    if (drl.adaptiveActionInterval > 0.1) {
      minInterval = std::min(minInterval, drl.adaptiveActionInterval);
    }
  }
  
  // Clamp to reasonable bounds (200ms - 5s)
  minInterval = std::max(0.2, std::min(5.0, minInterval));
  
  Simulator::Schedule(Seconds(minInterval), &ApplyFDRLCCActions);
  
  // Log adaptive interval (only when it changes significantly, reduce spam)
  static double lastLoggedInterval = 0.0;
  if (std::abs(minInterval - lastLoggedInterval) > 0.5 || simTime < 2.0) {
    // Log only significant changes (0.5s difference) or at start
    lastLoggedInterval = minInterval;
  }
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

