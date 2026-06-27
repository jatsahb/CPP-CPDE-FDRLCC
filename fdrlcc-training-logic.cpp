/**
 * fdrlcc-training-logic.cpp
 * 
 * DRL training logic for FDRLCC
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#include "fdrlcc-training-logic.hpp"
#include "fdrlcc-types.hpp"
#include "fdrlcc-simulation-utils.hpp"
#include "fdrlcc-csv-management.hpp"
#include "fdrlcc-console-colors.hpp"
#include "fdrlcc-enhanced-metrics.hpp"
#include "fdrlcc-structured-logger.hpp"  // OPTION 3.5: Structured logging
#include "fdrlcc-research-instrumentation.hpp"  // Research instrumentation
#include "src_cpp/controller/fdrl-replay-buffer.hpp"
#include "ns3/simulator.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

namespace ns3 {
namespace ndn {
namespace fdrl {

void
CheckTrainingStatus(const std::string& region, double simTime)
{
  auto& drl = g_regionDRL[region];
  auto& status = g_trainingStatus;
  
  size_t bufferSize = drl.replayBuffer.Size();
  uint32_t trainingSteps = drl.trainingStep;
  
  // Calculate loss variance
  double loss_variance = 0.0;
  if (status.lossHistory[region].size() > 10) {
    loss_variance = CalculateVariance(status.lossHistory[region]);
  }
  
  // Calculate reward variance
  double reward_variance = 0.0;
  if (!status.rewardHistory[region].empty() && status.rewardHistory[region].size() > 10) {
    reward_variance = CalculateVariance(status.rewardHistory[region]);
  }
  
  // Check buffer growth (used in status determination)
  status.lastBufferSize = bufferSize;
  
  // Check weight changes
  bool weights_changing = false;
  if (status.lastWeights.find(region) != status.lastWeights.end()) {
    std::vector<double> current_weights = drl.actor.Serialize();
    if (current_weights.size() == status.lastWeights[region].size()) {
      double weight_delta = 0.0;
      for (size_t i = 0; i < current_weights.size(); ++i) {
        double diff = current_weights[i] - status.lastWeights[region][i];
        weight_delta += diff * diff;
      }
      weight_delta = std::sqrt(weight_delta);
      weights_changing = (weight_delta > 1e-8);
    }
  }
  
  // TUNING: Compute training status from metrics with trends (not simple heuristics)
  // Calculate loss trend (slope) from recent history
  double loss_slope = 0.0;
  bool has_loss_trend = false;
  if (status.lossHistory[region].size() > 20) {
    // Linear regression on last 20 points
    size_t n = status.lossHistory[region].size();
    size_t window = std::min(size_t(20), n);
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
    for (size_t i = n - window; i < n; ++i) {
      double x = static_cast<double>(i - (n - window));
      double y = status.lossHistory[region][i];
      sum_x += x;
      sum_y += y;
      sum_xy += x * y;
      sum_x2 += x * x;
    }
    double denom = window * sum_x2 - sum_x * sum_x;
    if (std::abs(denom) > 1e-6) {
      loss_slope = (window * sum_xy - sum_x * sum_y) / denom;
      has_loss_trend = true;
    }
  }
  
  // Get recent loss and TD statistics
  double recent_loss = status.lossHistory[region].empty() ? 0.0 : status.lossHistory[region].back();
  double td_abs_mean_recent = (drl.td_error_count > 10) ? (std::abs(drl.td_error_mean) + drl.td_error_std) : 0.0;
  
  // TUNING: ACTIVE = Loss trending down OR TD abs_mean decreasing, with sufficient variance
  // PHASE-1: Use adaptive warmup size
  size_t effectiveWarmupSize = static_cast<size_t>(std::max(drl.minWarmupSize, drl.targetWarmupSize));
  bool isActive = (bufferSize >= effectiveWarmupSize) && 
                  (trainingSteps > 0) &&
                  (loss_variance > 0.01) &&  // Sufficient variance indicates learning
                  ((has_loss_trend && loss_slope < -0.1) ||  // Loss decreasing
                   (td_abs_mean_recent > 0.0 && td_abs_mean_recent < 10.0) ||  // TD errors reasonable
                   (reward_variance > 0.001));  // Reward variance indicates exploration
  
  // TUNING: STABLE = Loss flat and low, TD small, noise near minimum
  bool isStable = (bufferSize >= effectiveWarmupSize) &&
                  (trainingSteps > 50) &&
                  (recent_loss < 50.0) &&
                  (has_loss_trend && std::abs(loss_slope) < 0.5) &&  // Loss flat
                  (td_abs_mean_recent > 0.0 && td_abs_mean_recent < 5.0) &&  // TD small
                  (g_explorationNoise < 0.08);  // Noise near minimum (0.05)
  
  // TUNING: STALLED = Reward variance ≈ 0 OR actor gradients vanishing OR loss not improving
  bool isStalled = (bufferSize >= effectiveWarmupSize) &&
                   (trainingSteps > 20) &&
                   ((reward_variance < 1e-6 && !status.rewardHistory[region].empty()) ||  // Reward variance ≈ 0
                    (has_loss_trend && loss_slope >= 0.0 && recent_loss > 100.0) ||  // Loss not improving
                    (!weights_changing && trainingSteps > 50));  // Weights not changing
  
  // Determine training status string
  std::string statusStr = "INACTIVE";
  if (isStable && !status.isStalled && trainingSteps > 50) {
    statusStr = "STABLE";
  } else if (isActive) {
    statusStr = "ACTIVE";
  } else if (isStalled) {
    statusStr = "STALLED";
  }
  
  // Write training status to CSV
  WriteTrainingStatus(simTime, region, statusStr);
  
  // REMOVED: Verbose debug output - [TRAINING STATUS]
  // Update status flags without printing
  if (isStable && !status.isStalled && trainingSteps > 50) {
    status.isActive = false;
    status.isStalled = false;
  } else if (isActive && !status.isActive) {
    status.isActive = true;
    status.isStalled = false;
  } else if (isStalled && !status.isStalled) {
    status.isActive = false;
    status.isStalled = true;
  }
  
  // REMOVED: Verbose debug output - [WARNING] and [INFO] messages
}
/**
 * FIX: Train DRL agent independently of FL rounds
 * Called every N control steps when buffer is ready
 */
void
TrainDRLAgent(const std::string& region)
{
  auto& drl = g_regionDRL[region];
  double simTime = Simulator::Now().GetSeconds();
  
  // FIX 2: Check warmup and training frequency
  size_t bufferSize = drl.replayBuffer.Size();
  
  // FIX 1: Use adaptive warmup size (not hardcoded REPLAY_WARMUP_SIZE)
  size_t effectiveWarmupSize = static_cast<size_t>(std::max(drl.minWarmupSize, drl.targetWarmupSize));
  
  // PART 7: ERRORS (FATAL) - Abort training on critical issues
  // Error 1: Replay buffer empty after warmup
  if (bufferSize == 0 && simTime > 10.0) {
    std::cerr << ColorError("[ERROR]") << "[" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] Replay buffer empty after warmup period! "
              << "Training cannot proceed. Root cause: No transitions collected." << ColorOutput::Reset() << std::endl;
    return;
  }
  
  // FIX 1: Use adaptive warmup size
  if (bufferSize < effectiveWarmupSize) {
    // Not enough samples yet (adaptive warmup not reached)
    // REMOVED: Verbose debug output
    return;
  }
  
  // TUNING: Training trigger logic - check diversity and stability
  // FIX: Check buffer diversity using MULTIPLE state dimensions, not just first one
  // The first dimension (queueOccupancy) may be very stable, causing false negatives
  bool buffer_diverse = true;
  double state_var = 0.0;  // FIX 5: Declare in outer scope for logging
  if (bufferSize > 50) {
    // Sample a few states from buffer to check diversity
    auto sample_batch = drl.replayBuffer.Sample(std::min(size_t(20), bufferSize));
    if (sample_batch.size() > 5) {
      // FIX: Compute variance across ALL state dimensions, not just first one
      // This gives better diversity detection when first dimension is stable
      constexpr size_t STATE_DIM = 6;  // PHASE 1: 6D state vector
      std::vector<double> state_means(STATE_DIM, 0.0);
      std::vector<double> state_vars(STATE_DIM, 0.0);
      
      // Calculate mean for each dimension
      for (const auto& trans : sample_batch) {
        if (!trans.state.empty() && trans.state.size() >= STATE_DIM) {
          for (size_t dim = 0; dim < STATE_DIM; ++dim) {
            state_means[dim] += trans.state[dim];
          }
        }
      }
      for (size_t dim = 0; dim < STATE_DIM; ++dim) {
        state_means[dim] /= sample_batch.size();
      }
      
      // Calculate variance for each dimension
      for (const auto& trans : sample_batch) {
        if (!trans.state.empty() && trans.state.size() >= STATE_DIM) {
          for (size_t dim = 0; dim < STATE_DIM; ++dim) {
            double diff = trans.state[dim] - state_means[dim];
            state_vars[dim] += diff * diff;
          }
        }
      }
      for (size_t dim = 0; dim < STATE_DIM; ++dim) {
        state_vars[dim] /= sample_batch.size();
      }
      
      // Use maximum variance across dimensions (if any dimension varies, buffer is diverse)
      state_var = *std::max_element(state_vars.begin(), state_vars.end());
      buffer_diverse = (state_var > 1e-6);  // States should vary in at least one dimension
    }
  }
  
  // REFACTORED: Use unified RegionTrainingStats
  double reward_var_check = 0.0;
  if (g_regionStats.find(region) != g_regionStats.end() && 
      !g_regionStats[region].rewardWindow.empty() && 
      g_regionStats[region].rewardWindow.size() > 10) {
    reward_var_check = CalculateVariance(g_regionStats[region].rewardWindow);
  }
  bool reward_nonzero = (reward_var_check > 1e-6);
  
  // FIX 2: Relax training blocking conditions - allow training to start for all regions
  // Only apply strict checks after initial training has started
  static std::map<std::string, uint32_t> training_cooldown;
  if (training_cooldown.find(region) == training_cooldown.end()) {
    training_cooldown[region] = 0;
  }
  
  // Reduce cooldown each step
  if (training_cooldown[region] > 0) {
    training_cooldown[region]--;
  }
  
  // REFACTORED: Use config for training frequency
  if (drl.controlStep % g_fdrlccConfig.trainEveryNSteps != 0) {
    return;
  }
  
  // PHASE 2: Further relax training blocking conditions
  // Increase initial training window from 50 to 100 steps for better exploration
  // After 100 steps, apply stricter checks to ensure quality
  // Note: is_initial_training is used implicitly via require_diversity check
  
  // PHASE 2: More lenient diversity check - allow training even with low diversity initially
  // Only require diversity after significant training (200+ steps)
  bool require_diversity = (drl.trainingStep >= 200);
  
  // PHASE 2: Skip training only if:
  // 1. Cooldown active (always respect cooldown)
  // 2. Buffer not diverse AND require_diversity=true (allow during initial phase)
  // 3. Rewards zero AND past initial training AND buffer large enough AND require_diversity=true
  if (training_cooldown[region] > 0) {
    // REMOVED: Verbose debug output
    return;  // Cooldown active
  }
  
  // PHASE 2: Only check diversity if we require it (after 200 steps)
  if (require_diversity && !buffer_diverse) {
    // REMOVED: Verbose debug output
    return;  // Buffer not diverse enough (only after 200 steps)
  }
  
  // PHASE 2: More lenient reward check - only skip if rewards are truly constant AND buffer is large
  if (require_diversity && !reward_nonzero && bufferSize > 200) {
    // REMOVED: Verbose debug output
    return;  // Rewards are constant (only after 200 steps and large buffer)
  }
  
  // REFACTORED: Use config for batch size
  if (bufferSize < g_fdrlccConfig.trainingBatchSize) {
    std::cerr << "[ERROR][" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] Buffer size (" << bufferSize 
              << ") < batch size (" << g_fdrlccConfig.trainingBatchSize << ")! "
              << "Root cause: Insufficient experience for training." << std::endl;
    return;
  }
  
  // TUNING: Activate cooldown if critic loss > 200 for multiple steps
  static std::map<std::string, uint32_t> unstable_steps;
  if (unstable_steps.find(region) == unstable_steps.end()) {
    unstable_steps[region] = 0;
  }
  
  // PART 7: Error 2: No training calls detected
  g_regionNoTrainingCount[region]++;
  // FIX 1: Use adaptive warmup size
  if (g_regionNoTrainingCount[region] > 1000 && bufferSize >= effectiveWarmupSize) {
    std::cerr << ColorError("[ERROR]") << "[" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] No training calls for >1000 steps despite buffer ready! "
              << "Buffer: " << bufferSize << "/" << effectiveWarmupSize 
              << " | Root cause: Training loop broken or control step counter issue." 
              << ColorOutput::Reset() << std::endl;
    g_regionNoTrainingCount[region] = 0;  // Reset to avoid spam
  }
  
  // REFACTORED: Use config for batch size
  auto batch = drl.replayBuffer.Sample(g_fdrlccConfig.trainingBatchSize);
  
  // ============================================================================
  // PHASE 1: TRAINING LOOP VERIFICATION - CRITIC UPDATE
  // REFACTOR: Zero-mean Bellman residual (advantage-style), no Q-value normalization
  // ============================================================================
  
  // CRITICAL FIX: Reset gradients BEFORE batch processing (not in Backward())
  drl.critic.ResetGradients();
  
  double critic_loss = 0.0;
  std::vector<double> delta_raw;      // Raw Bellman residuals (TD errors)
  std::vector<double> delta_hat;      // Normalized residuals (zero-mean)
  
  // Trace full training flow: state → action → env step → reward → next_state → buffer.push → buffer.sample → loss → backward
  
  // ---- STEP 1: Compute Bellman residuals (ADVANTAGE) ----
  // REFACTOR: Never normalize raw Q-values, only normalize the residual
  std::vector<double> q_values_current;  // For logging only (sanity check)
  
  for (size_t i = 0; i < batch.size(); ++i) {
    const auto& trans = batch[i];
    
    // Current Q-value (Forward pass caches layer outputs for Backward)
    double current_q = drl.critic.Forward(trans.state, trans.action[0]);
    q_values_current.push_back(current_q);
    
    // Compute target Q-value using target networks
    // REFACTOR: Fix discount/reward consistency - use gamma=0.95 (Option A)
    const double gamma = 0.95;  // Changed from 0.99 for consistency with reward scale
    double target_q = trans.reward;
    if (!trans.done) {
      // ABLATION 5: Use main networks instead of target networks if disabled
      double next_action;
      if (!g_ablationConfig.disableTargetNetworks) {
        // Normal: use target networks for stable Q-value
        next_action = drl.actor_target.Forward(trans.next_state);
        target_q += gamma * drl.critic_target.Forward(trans.next_state, next_action);
      } else {
        // Ablation: use main networks (less stable, but still valid)
        next_action = drl.actor.Forward(trans.next_state);
        target_q += gamma * drl.critic.Forward(trans.next_state, next_action);
      }
    }
    
    // Raw Bellman residual (TD error)
    double delta = target_q - current_q;
    delta_raw.push_back(delta);
  }
  
  // ---- STEP 2: Batch-local normalization of residual (zero-mean) ----
  // REFACTOR: Normalize delta to zero-mean, not Q-values
  double mean_delta = CalculateMean(delta_raw);
  double std_delta = std::sqrt(CalculateVariance(delta_raw));
  const double eps = 1e-8;
  if (std_delta < eps) std_delta = 1.0;  // Prevent division by zero
  
  // Normalize residuals to zero-mean
  for (double delta_val : delta_raw) {
    double delta_hat_val = (delta_val - mean_delta) / std_delta;
    delta_hat.push_back(delta_hat_val);
  }
  
  // Store std_delta for actor update
  double stored_std_delta = std_delta;
  
  // Log delta statistics (CRITICAL: mean should be ~0 after normalization)
  // Note: delta_min, delta_max, delta_hat_min, delta_hat_max removed (unused)
  double delta_hat_mean = CalculateMean(delta_hat);
  double delta_hat_std = std::sqrt(CalculateVariance(delta_hat));
  
  
  // VALIDATION: mean(delta_hat) should be ~0 (implementation check)
  if (std::abs(delta_hat_mean) > 1e-6) {
    std::cerr << "[ERROR][" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] INVALID: mean(delta_hat)=" << std::setprecision(8) << delta_hat_mean 
              << " (should be ~0)" << std::endl;
  }
  
  // Note: Q-value statistics (q_min, q_max, q_mean) removed (unused)
  
  // ---- STEP 3: Critic loss (Huber on normalized residual) ----
  const double huber_delta = 1.0;  // Threshold where loss transitions from quadratic to linear
  
  for (size_t i = 0; i < batch.size(); ++i) {
    const auto& trans = batch[i];
    double delta_hat_val = delta_hat[i];
    double abs_delta_hat = std::abs(delta_hat_val);
    
    // Huber loss on delta_hat ONLY (not on Q-values)
    if (abs_delta_hat <= huber_delta) {
      // Quadratic region: 0.5 * delta_hat^2
      critic_loss += 0.5 * delta_hat_val * delta_hat_val;
    } else {
      // Linear region: huber_delta * (|delta_hat| - 0.5 * huber_delta)
      critic_loss += huber_delta * (abs_delta_hat - 0.5 * huber_delta);
    }
    
    // ---- STEP 4: Backprop ----
    // Gradient of Huber loss w.r.t. delta_hat
    double grad;
    if (abs_delta_hat <= huber_delta) {
      // Quadratic region: gradient = delta_hat
      grad = delta_hat_val;
    } else {
      // Linear region: gradient = huber_delta * sign(delta_hat)
      grad = (delta_hat_val > 0.0) ? huber_delta : -huber_delta;
    }
    
    // Backpropagate through critic
    // Note: The gradient needs to be backpropagated through the normalization
    // Since delta_hat = (delta - mean_delta) / std_delta, and delta = target_q - current_q,
    // The gradient w.r.t. current_q is: -grad / std_delta
    drl.critic.Backward(trans.state, trans.action[0], -grad / std_delta);
  }
  
  critic_loss /= g_fdrlccConfig.trainingBatchSize;  // Average loss
  
  // REFACTOR: Use delta_hat statistics (already computed and logged above at line ~3415)
  // delta_hat_mean and delta_hat_std are already declared above, just compute td_abs_mean here
  double td_abs_mean = 0.0;
  
  if (!delta_hat.empty()) {
    for (double d : delta_hat) {
      td_abs_mean += std::abs(d);
    }
    td_abs_mean /= delta_hat.size();
    
    // Update running statistics for monitoring (not used in computation)
    // Note: delta_hat_mean and delta_hat_std are already computed above (line ~3415)
    const double alpha = 0.01;  // Smoothing factor for running stats
    if (drl.td_error_count == 0) {
      drl.td_error_mean = delta_hat_mean;
      drl.td_error_std = delta_hat_std > 1e-6 ? delta_hat_std : 1.0;
    } else {
      drl.td_error_mean = (1.0 - alpha) * drl.td_error_mean + alpha * delta_hat_mean;
      drl.td_error_std = (1.0 - alpha) * drl.td_error_std + alpha * (delta_hat_std > 1e-6 ? delta_hat_std : drl.td_error_std);
    }
    drl.td_error_count++;
    if (drl.td_error_std < 1e-6) drl.td_error_std = 1.0;
    
    // REMOVED: WriteTDErrorMetrics() call - TD error metrics now written directly to training_metrics.csv
    // TD error metrics are included in the consolidated training_metrics.csv output above
    // Note: min/max are calculated on-the-fly from mean/std when writing to CSV
  }
  
  // Average gradients by batch size (CRITICAL: gradients are summed, need to average)
  drl.critic.AverageGradients(batch.size());
  
  // FIX: Conditional gradient clipping with frequency tracking
  // Compute gradient norm BEFORE clipping (true magnitude)
  double critic_grad_norm_preclip = drl.critic.GetGradientNorm();
  
  // FIX: Adaptive clipping threshold (reduce when gradients stabilize)
  // Start with 5.0, reduce to 2.0 once stable (forces smaller gradients)
  double critic_clip_threshold = (critic_loss < 50.0 && td_abs_mean < 5.0 && drl.trainingStep > 50) ? 2.0 : 5.0;
  
  // FIX: Conditional clipping - only clip if gradient norm exceeds threshold
  // Track clipping frequency for adaptive LR adjustment
  bool critic_was_clipped = (critic_grad_norm_preclip > critic_clip_threshold);
  double critic_grad_norm_postclip = critic_grad_norm_preclip;
  
  if (critic_was_clipped) {
    // Only clip when necessary (emergency brake, not steering wheel)
    drl.critic.ClipGradients(critic_clip_threshold);
    critic_grad_norm_postclip = drl.critic.GetGradientNorm();
    drl.critic_clip_count++;
  }
  
  // Track clipping frequency
  drl.critic_update_count++;
  drl.critic_clip_frequency = static_cast<double>(drl.critic_clip_count) / drl.critic_update_count;
  
  // MANDATORY ASSERTION: Gradients must be non-zero
  if (critic_grad_norm_preclip < 1e-8) {
    std::cerr << "[FATAL][" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] CRITIC GRADIENTS ARE ZERO! grad_norm_preclip=" 
              << critic_grad_norm_preclip << " | Training is invalid!" << std::endl;
    // Don't abort, but log as critical error
  }
  
  // FIX: Adaptive LR reduction if clipping frequency too high (>30% indicates scale mismatch)
  if (drl.critic_clip_frequency > 0.30 && drl.critic_update_count > 20) {
    drl.learning_rate_critic *= 0.95;  // Reduce LR by 5%
  }
  
  
  // REFACTOR: Simplified learning rate (no progressive schedule - normalization handles stability)
  // Keep LR fixed (no adaptive changes based on clipping frequency - that would be compensation)
  // Clamp LR to reasonable bounds
  drl.learning_rate_critic = std::max(0.001, std::min(0.01, drl.learning_rate_critic));
  
  // Track weight norm AFTER update (to detect weight growth)
  drl.critic.UpdateWeights(drl.learning_rate_critic);
  
  // Note: critic_weight_norm_after removed (unused)
  
  
  // NOTE: Adaptive LR reduction for high clipping frequency is handled above in clipping section
  
  // REMOVED: Verbose debug output - [WARNING] about CRITIC weight norm
  
  // ============================================================================
  // PHASE 1: TRAINING LOOP VERIFICATION - ACTOR UPDATE
  // REFACTOR: Actor MUST use normalized advantage (delta_hat), not raw Q gradients
  // ============================================================================
  
  // PHASE 2: Enhanced weight verification and reinitialization
  double actor_weight_norm_before = drl.actor.GetWeightNorm();
  double critic_weight_norm_before = drl.critic.GetWeightNorm();
  
  // PHASE 2: Check both actor and critic weights
  if (actor_weight_norm_before < 1e-6) {
    std::cerr << ColorError("[CRITICAL]") << " Region=" << region 
              << " | Actor weights are ZERO (norm=" << actor_weight_norm_before 
              << ")! Reinitializing actor network..." << ColorOutput::Reset() << std::endl;
    drl.actor.Initialize();
    drl.actor_target.Initialize();
    // Re-check after initialization
    actor_weight_norm_before = drl.actor.GetWeightNorm();
    // REMOVED: Verbose debug output - [FIXED]
  }
  
  // PHASE 2: Also check critic weights
  if (critic_weight_norm_before < 1e-6) {
    // REMOVED: Verbose debug output - [CRITICAL] and [FIXED]
    drl.critic.Initialize();
    drl.critic_target.Initialize();
    critic_weight_norm_before = drl.critic.GetWeightNorm();
  }
  
  // PHASE 2: Log weight norms periodically for monitoring
  if (drl.trainingStep % 10 == 0) {
    // REMOVED: Verbose debug output - [WEIGHT_VERIFY]
  }
  
  // REFACTOR: Conditional actor update (skip if critic is unstable - UNSTABLE status)
  // Actor depends on critic, so unstable critic → unstable actor updates
  bool skip_actor_update = (critic_loss > 100.0 || delta_hat_std > 2.0);  // Use delta_hat_std instead of td_abs_mean
  if (skip_actor_update) {
    // REMOVED: Verbose debug output - [ACTOR_SKIP]
    // Still increment training step and return
    // REFACTORED: Removed g_globalTrainingStep (not needed - use drl.trainingStep)
    drl.trainingStep++;
    return;
  }
  
  // CRITICAL FIX: Reset gradients BEFORE batch processing
  drl.actor.ResetGradients();
  
  // REFACTOR: Actor loss = -Mean(delta_hat) (maximize normalized advantage)
  double actor_loss = 0.0;
  
  // PHASE 1 FIX: Improved actor gradient calculation - use raw delta if normalized is too small
  // Use raw delta if std_delta is too small, or add minimum threshold
  double min_std_delta = 1e-3;  // Minimum threshold to prevent division by very small numbers
  double effective_std_delta = std::max(min_std_delta, stored_std_delta);
  
  for (size_t i = 0; i < batch.size(); ++i) {
    const auto& trans = batch[i];
    
    // CRITICAL FIX: Actor backward needs cached layer outputs from Forward()
    // Do Forward() first to cache outputs for current state
    (void)drl.actor.Forward(trans.state);  // Cache outputs for backward pass (return value unused)
    
    // PHASE 1 FIX: Improved gradient calculation - use raw delta if normalized is too small
    double delta_hat_val = delta_hat[i];
    
    // Calculate normalized gradient scale
    double gradient_scale = delta_hat_val / effective_std_delta;
    
    // PHASE 1 FIX: More lenient threshold (0.01 instead of 1e-6) - use raw delta if normalized is too small
    // This ensures gradients are always meaningful, not just technically non-zero
    if (std::abs(gradient_scale) < 0.01) {
      // Use raw delta with appropriate scaling to ensure reasonable magnitude
      // Scale raw delta to match expected gradient magnitude (typically 0.1-1.0 range)
      double raw_delta_scaled = delta_raw[i] * 0.1;  // Scale raw delta
      gradient_scale = raw_delta_scaled;
    }
    
    // Backpropagate with calculated gradient
    drl.actor.Backward(trans.state, gradient_scale);
    
    // Actor loss: negative delta_hat (maximize advantage)
    actor_loss -= delta_hat_val;
    
  }
  
  actor_loss /= batch.size();  // Average loss
  
  // Average gradients by batch size
  drl.actor.AverageGradients(batch.size());
  
  
  // PHASE 2: VERIFY GRADIENT FLOW - Compute gradient norm BEFORE clipping (true magnitude)
  double actor_grad_norm_preclip = drl.actor.GetGradientNorm();
  
  // Gradient clipping
  // CRITICAL FIX: Increased clipping threshold from 1.0 to 2.0 for Region A
  // With Q-gradient scaling (10x for Region A), gradient norms will be larger
  // Need higher threshold to prevent excessive clipping while still protecting against explosion
  double actor_clip_threshold = (region == "A") ? 2.0 : 1.0;
  drl.actor.ClipGradients(actor_clip_threshold);
  
  // Compute gradient norm AFTER clipping (for verification)
  double actor_grad_norm_postclip = drl.actor.GetGradientNorm();
  
  
  // MANDATORY ASSERTION: Gradients must be non-zero
  if (actor_grad_norm_preclip < 1e-8) {
    std::cerr << ColorError("[FATAL]") << "[" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] ACTOR GRADIENTS ARE ZERO! grad_norm_preclip=" 
              << std::setprecision(8) << actor_grad_norm_preclip 
              << " | Training is invalid!" << std::endl;
    std::cerr << "[DIAGNOSTIC] Region=" << region 
              << " | weight_norm=" << actor_weight_norm_before
              << " | stored_std_delta=" << stored_std_delta
              << " | delta_hat_range=[" << std::setprecision(4)
              << *std::min_element(delta_hat.begin(), delta_hat.end()) << ", "
              << *std::max_element(delta_hat.begin(), delta_hat.end()) << "]"
              << " | Possible causes: weights zero, std_delta too large, or delta_hat all zero"
              << ColorOutput::Reset() << std::endl;
    
    // FIX 4: If gradients are zero and weights are non-zero, try reinitializing actor
    if (actor_weight_norm_before > 1e-6) {
      std::cerr << ColorWarning("[ATTEMPTING_FIX]") << " Region=" << region 
                << " | Reinitializing actor network due to zero gradients..." 
                << ColorOutput::Reset() << std::endl;
      drl.actor.Initialize();
      drl.actor_target.Initialize();
      // Don't continue with this training step - return and try again next time
      return;
    }
  }
  
  // Check if gradients are being clipped (use region-specific threshold)
  double actor_clip_threshold_check = (region == "A") ? 2.0 : 1.0;
  bool actor_was_clipped = (actor_grad_norm_preclip > actor_clip_threshold_check);
  // REMOVED: Verbose debug output - gradient clipping warnings
  // if (actor_was_clipped) { ... }
  // if (actor_grad_norm_preclip < 0.001 && actor_grad_norm_preclip >= 1e-8) { ... }
  
  // TUNING: Adaptive actor learning rate (base LR, adjusted based on critic stability)
  // Initialize base LR for region
  if (drl.trainingStep == 0) {
    drl.learning_rate_actor = (region == "A") ? 0.0005 : 0.0001;  // Initial LR
  }
  
  // Reduce actor LR if critic TD variance is high (unstable critic → slower actor updates)
  if (td_abs_mean > 10.0) {
    drl.learning_rate_actor *= 0.95;  // Reduce by 5% when critic unstable
  }
  
  // Reduce actor LR if actor gradients consistently clipped
  if (actor_was_clipped) {
    drl.learning_rate_actor *= 0.98;  // Reduce by 2% when clipped
  }
  
  // Clamp actor LR to reasonable bounds
  double actor_lr_min = (region == "A") ? 0.0002 : 0.00005;
  double actor_lr_max = (region == "A") ? 0.0005 : 0.0001;
  drl.learning_rate_actor = std::max(actor_lr_min, std::min(actor_lr_max, drl.learning_rate_actor));
  
  // Track weight norm AFTER update
  drl.actor.UpdateWeights(drl.learning_rate_actor);
  
  // Note: actor_weight_norm_after removed (unused)
  
  // REMOVED: Verbose debug output - [GRAD_VERIFY]
  
  // REMOVED: WriteGradientMetrics() call - gradient metrics now written directly to training_metrics.csv
  // Gradient metrics are included in the consolidated training_metrics.csv output above
  
  // ============================================================================
  // STRUCTURED LOGGING: Log learning step with TD-error, losses, and buffer size
  // ============================================================================
  // Log learning metrics using StructuredLogger::LogLearning
  if (g_enableStructuredLogs && g_structuredLogger) {
    // Calculate TD-error: use mean absolute TD-error from this batch
    double tdError = td_abs_mean;  // Mean absolute TD-error from delta_hat
    
    // Critic and actor losses are already computed and averaged
    // critic_loss: averaged Huber loss (line 417)
    // actor_loss: averaged negative advantage (line 589)
    
    // Replay buffer size
    size_t bufferSize = drl.replayBuffer.Size();
    
    // Log learning step
    g_structuredLogger->LogLearning(simTime,
                                   region,
                                   tdError,      // TD-error (mean absolute from batch)
                                   critic_loss,  // Critic loss (averaged Huber loss)
                                   actor_loss);  // Actor loss (averaged negative advantage)
    
    // Also log buffer size via LogEvent (since LogLearning doesn't include buffer size)
    std::ostringstream details;
    details << "Learning update | td_error=" << std::fixed << std::setprecision(4) << tdError
            << " critic_loss=" << critic_loss
            << " actor_loss=" << actor_loss
            << " buffer_size=" << bufferSize
            << " training_step=" << drl.trainingStep;
    g_structuredLogger->LogEvent(simTime, "LEARNING_UPDATE", details.str());
  }
  
  // FIX 4: Fail-safe for zero/NaN losses
  if (std::isnan(actor_loss) || std::isnan(critic_loss)) {
    std::cerr << "[ERROR] Region=" << region << " | NaN loss detected! actor=" 
              << actor_loss << " critic=" << critic_loss << std::endl;
  }
  if (std::abs(actor_loss) < 1e-10 && std::abs(critic_loss) < 1e-10) {
    std::cerr << "[ERROR] Region=" << region << " | Losses always zero! actor=" 
              << actor_loss << " critic=" << critic_loss << std::endl;
  }
  
  // ABLATION 5: Skip target network soft updates if disabled
  if (!g_ablationConfig.disableTargetNetworks) {
    // Normal: soft update target networks
    const double tau = 0.005;
    drl.actor_target.SoftUpdate(drl.actor, tau);
    drl.critic_target.SoftUpdate(drl.critic, tau);
  } else {
    // Ablation: hard copy (target = main, no stabilization)
    // This makes target networks identical to main networks (no soft update)
    // Use Serialize/Deserialize for copy (networks don't have copy assignment)
    drl.actor_target.Deserialize(drl.actor.Serialize());
    drl.critic_target.Deserialize(drl.critic.Serialize());
  }
  
  // RESEARCH INSTRUMENTATION: Record learning step
  double rewardRaw = drl.reward;
  double baselineReward = 0.0;  // Could use moving average of rewards
  if (!drl.rewardHistory.empty() && drl.rewardHistory.size() > 10) {
    // Use mean of last 10 rewards as baseline
    double sum = 0.0;
    size_t count = std::min(size_t(10), drl.rewardHistory.size());
    for (size_t i = drl.rewardHistory.size() - count; i < drl.rewardHistory.size(); ++i) {
      sum += drl.rewardHistory[i];
    }
    baselineReward = sum / count;
  }
  double advantage = rewardRaw - baselineReward;
  ResearchInstrumentation::RecordLearningStep(drl.trainingStep, region, rewardRaw,
                                              baselineReward, advantage, critic_loss, actor_loss);
  
  // Store losses for status display
  extern std::map<std::string, double> g_regionActorLoss;
  extern std::map<std::string, double> g_regionCriticLoss;
  g_regionActorLoss[region] = actor_loss;
  g_regionCriticLoss[region] = critic_loss;
  
  
  // NOTE: Removed training_losses.csv logging (redundant with training_metrics.csv)
  
  // ============================================================================
  // PART 2: TRAINING METRICS DATASET (ENHANCED - MANDATORY)
  // ============================================================================
  // Calculate Q-value statistics from batch
  std::vector<double> q_values;
  for (const auto& trans : batch) {
    double q_val = drl.critic.Forward(trans.state, trans.action[0]);
    q_values.push_back(q_val);
  }
  double q_value_mean = CalculateMean(q_values);
  double q_value_std = std::sqrt(CalculateVariance(q_values));
  
  // REFACTORED: Use unified RegionTrainingStats
  double reward_mean_window = 0.0;
  double reward_variance_window = 0.0;
  if (g_regionStats.find(region) != g_regionStats.end() && 
      !g_regionStats[region].rewardWindow.empty()) {
    reward_mean_window = CalculateMean(g_regionStats[region].rewardWindow);
    reward_variance_window = CalculateVariance(g_regionStats[region].rewardWindow);
  }
  
  // Learning rates are already declared above (learning_rate_actor, learning_rate_critic)
  // Reuse them here for logging
  
  // Total loss
  double total_loss = actor_loss + critic_loss;
  
  // STEP 4: Subsampling support - log every N training steps based on config
  static std::map<std::string, uint32_t> trainingLogCount;  // Per-region counter
  if (trainingLogCount.find(region) == trainingLogCount.end()) {
    trainingLogCount[region] = 0;
  }
  trainingLogCount[region]++;
  
  // Only log if interval matches (or if interval is 1, log every step)
  bool shouldLog = (g_fdrlccConfig.trainingLogInterval == 1) || 
                   (trainingLogCount[region] % g_fdrlccConfig.trainingLogInterval == 0);
  
  // PART 2: Log consolidated training metrics (includes rewards, gradients, TD errors)
  if (g_trainingMetricsCsv.is_open() && shouldLog) {
    // Get reward data from RegionTrainingStats
    double reward = g_regionStats[region].lastReward;
    double episodeReward = g_regionStats[region].episodeReward;
    double rewardMA = g_regionStats[region].avgReward;
    
    // STEP 4: Calculate reward_delta and smoothed_reward for learning evidence
    double rewardDelta = 0.0;
    double smoothedReward = rewardMA;  // Use rewardMA as smoothed_reward (windowed mean)
    
    // Calculate reward_delta = reward(t) - reward(t-1)
    if (!g_regionStats[region].rewardHistory.empty()) {
      // Get previous reward from history
      double prevReward = g_regionStats[region].rewardHistory.back();
      rewardDelta = reward - prevReward;
    }
    
    // If rewardHistory is empty, use rewardWindow for previous value
    if (g_regionStats[region].rewardHistory.empty() && !g_regionStats[region].rewardWindow.empty()) {
      double prevReward = g_regionStats[region].rewardWindow.back();
      rewardDelta = reward - prevReward;
    }
    
    // Get gradient metrics (already calculated above)
    double critic_norm_preclip = critic_grad_norm_preclip;
    double critic_norm_postclip = critic_grad_norm_postclip;
    double actor_norm_preclip = actor_grad_norm_preclip;
    double actor_norm_postclip = actor_grad_norm_postclip;
    double clipping_frequency = drl.critic_clip_frequency;
    uint32_t clip_count = drl.critic_clip_count;
    uint32_t update_count = drl.critic_update_count;
    
    // Get TD error metrics (already calculated above)
    double td_error_mean = drl.td_error_mean;
    double td_error_std = drl.td_error_std;
    uint32_t td_error_count = drl.td_error_count;
    // Calculate min/max from mean/std (approximate, since we don't store actual min/max)
    double td_error_min = drl.td_error_mean - drl.td_error_std;
    double td_error_max = drl.td_error_mean + drl.td_error_std;
    
    g_trainingMetricsCsv << std::fixed << std::setprecision(6)
                        << simTime << ","                                    // timestamp
                        << g_ablationConfig.ablationLabel << ","             // ablation_config
                        << drl.trainingStep << ","                           // global_step
                        << region << ","                                    // region_id
                        << drl.trainingStep << ","                          // training_step_id
                        << actor_loss << ","                               // actor_loss
                        << critic_loss << ","                              // critic_loss
                        << total_loss << ","                               // total_loss
                        << q_value_mean << ","                             // q_value_mean
                        << q_value_std << ","                              // q_value_std
                        << reward_mean_window << ","                        // reward_mean_window
                        << reward_variance_window << ","                    // reward_variance_window
                        << drl.learning_rate_actor << ","                  // learning_rate_actor
                        << drl.learning_rate_critic << ","                  // learning_rate_critic
                        << reward << ","                                   // reward (from training_rewards.csv)
                        << episodeReward << ","                             // episode_reward (from training_rewards.csv)
                        << rewardMA << ","                                  // reward_ma (from training_rewards.csv)
                        << rewardDelta << ","                               // reward_delta (STEP 4: reward(t) - reward(t-1))
                        << smoothedReward << ","                            // smoothed_reward (STEP 4: EMA/windowed mean)
                        << critic_norm_preclip << ","                       // critic_norm_preclip (from gradient_metrics.csv)
                        << critic_norm_postclip << ","                      // critic_norm_postclip (from gradient_metrics.csv)
                        << actor_norm_preclip << ","                        // actor_norm_preclip (from gradient_metrics.csv)
                        << actor_norm_postclip << ","                       // actor_norm_postclip (from gradient_metrics.csv)
                        << clipping_frequency << ","                        // clipping_frequency (from gradient_metrics.csv)
                        << clip_count << ","                                // clip_count (from gradient_metrics.csv)
                        << update_count << ","                              // update_count (from gradient_metrics.csv)
                        << td_error_mean << ","                             // td_error_mean (from td_error_metrics.csv)
                        << td_error_std << ","                              // td_error_std (from td_error_metrics.csv)
                        << td_error_count << ","                            // td_error_count (from td_error_metrics.csv)
                        << td_error_min << ","                              // td_error_min (from td_error_metrics.csv)
                        << td_error_max << std::endl;                       // td_error_max (from td_error_metrics.csv)
    FlushCsvFiles();
  }
  
  // OPTION 3.5: Structured logging - Log DRL learning metrics
  if (g_enableStructuredLogs && g_structuredLogger) {
    // Get throughput from MetricSnapshot
    double totalThroughput = 0.0;
    if (g_metricEngine && g_metricEngine->IsRegionInitialized(region)) {
      const MetricSnapshot& snapshot = g_metricEngine->GetLatestSnapshot(region);
      totalThroughput = snapshot.throughputMbps;
    }
    
    // Calculate penalties (would need to extract from reward calculation - simplified for now)
    // These are typically: queue_penalty, delay_penalty, loss_penalty
    // For now, use placeholders - would need access to reward components
    double queuePenalty = 0.0;  // TODO: Extract from reward calculation
    double delayPenalty = 0.0;  // TODO: Extract from reward calculation
    double lossPenalty = 0.0;   // TODO: Extract from reward calculation
    
    // Use TD error mean from drl state
    double tdError = drl.td_error_mean;
    
    // Get current reward from drl state
    double currentReward = drl.reward;
    
    g_structuredLogger->LogDRLLearning(region,
                                       currentReward,  // current reward
                                       totalThroughput,
                                       queuePenalty,
                                       delayPenalty,
                                       lossPenalty,
                                       actor_loss,
                                       critic_loss,
                                       tdError,
                                       simTime);
  }
  
  // PART 2: Validate losses (non-zero, non-NaN, non-Inf)
  if (std::isnan(actor_loss) || std::isnan(critic_loss) || 
      std::isinf(actor_loss) || std::isinf(critic_loss)) {
    std::cerr << "[ERROR][" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] NaN/Inf loss detected! actor=" 
              << actor_loss << " critic=" << critic_loss 
              << " | Training step aborted!" << std::endl;
    return;  // Abort this training step
  }
  
  // PART 2: Training step must increment strictly
  // REFACTORED: Removed g_globalTrainingStep (not needed)
  uint32_t prev_training_step = drl.trainingStep;
  drl.trainingStep++;
  if (drl.trainingStep != prev_training_step + 1) {
    std::cerr << "[ERROR][" << std::fixed << std::setprecision(2) << simTime 
              << "s][" << region << "] Training step increment violation! prev=" 
              << prev_training_step << " current=" << drl.trainingStep << std::endl;
  }
  
  // REFACTORED: Use drl.trainingStep instead of g_globalTrainingStep
  g_trainingStatus.lastTrainingStep = drl.trainingStep;
  g_trainingStatus.lastBufferSize = bufferSize;
  
  // Track loss history for variance calculation
  g_trainingStatus.lossHistory[region].push_back(total_loss);
  if (g_trainingStatus.lossHistory[region].size() > 100) {
    g_trainingStatus.lossHistory[region].erase(g_trainingStatus.lossHistory[region].begin());
  }
  
  // REFACTORED: Use unified RegionTrainingStats
  if (g_regionStats.find(region) != g_regionStats.end() && 
      !g_regionStats[region].rewardWindow.empty()) {
    g_trainingStatus.rewardHistory[region] = g_regionStats[region].rewardWindow;
  }
  
  // Track weight changes
  std::vector<double> current_weights = drl.actor.Serialize();
  g_trainingStatus.lastWeights[region] = current_weights;
  
  // Update buffer growth rate
  g_trainingStatus.bufferGrowthRate[region] = bufferSize;
  
  g_trainingSummary.totalTrainingSteps++;
  g_trainingSummary.regionTrainingSteps[region]++;
  
  // Reset no-training counter
  g_regionNoTrainingCount[region] = 0;
}


} // namespace fdrl
} // namespace ndn
} // namespace ns3

