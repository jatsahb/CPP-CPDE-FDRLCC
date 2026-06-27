/**
 * fdrl-ddpg-networks.hpp
 * 
 * Deep Deterministic Policy Gradient (DDPG) Neural Networks
 * 
 * Implements Actor and Critic networks for DDPG-based congestion control.
 * 
 * Actor Network: State (6-dim) → [256] → [128] → Action (1-dim) [PHASE 1: Extended from 5D to 6D with RTT gradient]
 * Critic Network: [State (6) + Action (1)] → [256] → [128] → Q-value (1-dim) [PHASE 1: Extended from 5D to 6D with RTT gradient]
 */

#ifndef FDRL_DDPG_NETWORKS_HPP
#define FDRL_DDPG_NETWORKS_HPP

#include <vector>
#include <random>
#include <cmath>
#include <algorithm>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Actor Network (Policy Network)
 * 
 * Architecture:
 *   Input: 6-dimensional state vector (PHASE 1: [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio, rttGradientNorm])
 *   Layer 1: 6 → 256 (with LayerNorm + ReLU)
 *   Layer 2: 256 → 128 (with LayerNorm + ReLU)
 *   Output: 128 → 1 (with Tanh, scaled to [0.5, 3.0])
 *   FIX: Increased max from 2.0 to 3.0 to allow more aggressive rates
 * 
 * Total Parameters: ~36,000 (extended from ~35,000 due to 6D input)
 */
class ActorNetwork {
public:
  /**
   * Constructor with configurable state dimension
   * @param stateDim State vector dimension (default: 6 for backward compatibility)
   */
  explicit ActorNetwork(size_t stateDim = 6);
  ~ActorNetwork() = default;
  
  /**
   * Initialize network with random weights (Xavier initialization)
   */
  void Initialize();
  
  /**
   * Forward pass: Compute action from state
   * @param state 6-dimensional state vector [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio, rttGradientNorm]
   * @return Action (rate factor in [0.5, 3.0])
   */
  double Forward(const std::vector<double>& state);
  
  /**
   * Backward pass: Compute gradients for policy gradient update
   * @param state Current state
   * @param q_gradient Gradient of Q-value w.r.t. action
   */
  void Backward(const std::vector<double>& state, double q_gradient);
  
  /**
   * Update network weights using accumulated gradients
   * @param learning_rate Learning rate for weight update
   */
  void UpdateWeights(double learning_rate);
  
  /**
   * Soft update target network: θ' ← τ·θ + (1-τ)·θ'
   * @param source Source network to copy from
   * @param tau Soft update coefficient (typically 0.005)
   */
  void SoftUpdate(const ActorNetwork& source, double tau);
  
  /**
   * Serialize network to vector (for FL aggregation)
   * @return Flattened weight vector
   */
  std::vector<double> Serialize() const;
  
  /**
   * Deserialize network from vector
   * @param weights Flattened weight vector
   */
  void Deserialize(const std::vector<double>& weights);
  
  /**
   * Get number of parameters
   */
  size_t GetParameterCount() const;
  
  /**
   * Clip gradients to prevent explosion
   */
  void ClipGradients(double max_norm = 1.0);
  
  /**
   * Reset gradients to zero (call before batch accumulation)
   */
  void ResetGradients();
  
  /**
   * Get L2 norm of gradients (for verification)
   */
  double GetGradientNorm() const;
  
  /**
   * Get L2 norm of weights (for tracking weight growth)
   */
  double GetWeightNorm() const;
  
  /**
   * Average accumulated gradients by batch size
   * Call this after accumulating gradients across batch, before clipping/updating
   */
  void AverageGradients(size_t batch_size);

private:
  size_t state_dim_;  // State vector dimension (configurable for ablation)
  
  // Layer 1: state_dim → 256
  std::vector<std::vector<double>> layer1_weights_;  // state_dim×256
  std::vector<double> layer1_bias_;                  // 256
  std::vector<double> layer1_output_;                // 256 (cached for backward)
  
  // Layer 2: 256 → 128
  std::vector<std::vector<double>> layer2_weights_;  // 256×128
  std::vector<double> layer2_bias_;                  // 128
  std::vector<double> layer2_output_;                // 128 (cached for backward)
  
  // Output: 128 → 1
  std::vector<double> output_weights_;               // 128×1
  double output_bias_;                                // 1
  
  // Gradients (accumulated during backward pass)
  std::vector<std::vector<double>> layer1_weight_grads_;
  std::vector<double> layer1_bias_grads_;
  std::vector<std::vector<double>> layer2_weight_grads_;
  std::vector<double> layer2_bias_grads_;
  std::vector<double> output_weight_grads_;
  double output_bias_grad_;
  
  // Layer normalization parameters
  std::vector<double> layer1_mean_;
  std::vector<double> layer1_std_;
  std::vector<double> layer2_mean_;
  std::vector<double> layer2_std_;
  
  // Helper functions
  void LayerNorm(std::vector<double>& x, const std::vector<double>& mean, const std::vector<double>& std);
  double ReLU(double x);
  double ReLUDerivative(double x);
  double Tanh(double x);
  double TanhDerivative(double x);
  
  // Random number generator for initialization
  std::mt19937 rng_;
  std::normal_distribution<double> normal_dist_;
};

/**
 * Critic Network (Value Network)
 * 
 * Architecture:
 *   Input: 7-dimensional (6 state + 1 action) [PHASE 1: Extended from 5D to 6D]
 *   Layer 1: 7 → 256 (with LayerNorm + ReLU)
 *   Layer 2: 256 → 128 (with LayerNorm + ReLU)
 *   Output: 128 → 1 (Q-value)
 * 
 * Total Parameters: ~36,000
 */
class CriticNetwork {
public:
  /**
   * Constructor with configurable state dimension
   * @param stateDim State vector dimension (default: 6 for backward compatibility)
   */
  explicit CriticNetwork(size_t stateDim = 6);
  ~CriticNetwork() = default;
  
  /**
   * Initialize network with random weights (Xavier initialization)
   */
  void Initialize();
  
  /**
   * Forward pass: Compute Q-value from state and action
   * @param state 6-dimensional state vector [PHASE 1: Extended from 5D to 6D]
   * @param action 1-dimensional action (rate factor)
   * @return Q-value (estimated value of state-action pair)
   */
  double Forward(const std::vector<double>& state, double action);
  
  /**
   * Backward pass: Compute gradients for TD learning
   * @param state Current state
   * @param action Current action
   * @param td_error Temporal difference error (target_q - current_q)
   */
  void Backward(const std::vector<double>& state, double action, double td_error);
  
  /**
   * Get gradient of Q-value w.r.t. action (for actor update)
   * @param state Current state
   * @param action Current action
   * @return Gradient ∂Q/∂a
   */
  double GetActionGradient(const std::vector<double>& state, double action);
  
  /**
   * Update network weights using accumulated gradients
   * @param learning_rate Learning rate for weight update
   */
  void UpdateWeights(double learning_rate);
  
  /**
   * Soft update target network: θ' ← τ·θ + (1-τ)·θ'
   * @param source Source network to copy from
   * @param tau Soft update coefficient (typically 0.005)
   */
  void SoftUpdate(const CriticNetwork& source, double tau);
  
  /**
   * Serialize network to vector (for FL aggregation - optional, usually only actor is aggregated)
   * @return Flattened weight vector
   */
  std::vector<double> Serialize() const;
  
  /**
   * Deserialize network from vector
   * @param weights Flattened weight vector
   */
  void Deserialize(const std::vector<double>& weights);
  
  /**
   * Get number of parameters
   */
  size_t GetParameterCount() const;
  
  /**
   * Clip gradients to prevent explosion
   */
  void ClipGradients(double max_norm = 1.0);
  
  /**
   * Reset gradients to zero (call before batch accumulation)
   */
  void ResetGradients();
  
  /**
   * Get L2 norm of gradients (for verification)
   */
  double GetGradientNorm() const;
  
  /**
   * Get L2 norm of weights (for tracking weight growth)
   */
  double GetWeightNorm() const;
  
  /**
   * Average accumulated gradients by batch size
   * Call this after accumulating gradients across batch, before clipping/updating
   */
  void AverageGradients(size_t batch_size);

private:
  size_t state_dim_;  // State vector dimension (configurable for ablation)
  
  // Layer 1: (state_dim + 1) → 256 (state + action)
  std::vector<std::vector<double>> layer1_weights_;  // (state_dim+1)×256
  std::vector<double> layer1_bias_;                  // 256
  std::vector<double> layer1_output_;                // 256 (cached for backward)
  
  // Layer 2: 256 → 128
  std::vector<std::vector<double>> layer2_weights_;  // 256×128
  std::vector<double> layer2_bias_;                 // 128
  std::vector<double> layer2_output_;                // 128 (cached for backward)
  
  // Output: 128 → 1
  std::vector<double> output_weights_;               // 128×1
  double output_bias_;                                // 1
  
  // Gradients (accumulated during backward pass)
  std::vector<std::vector<double>> layer1_weight_grads_;
  std::vector<double> layer1_bias_grads_;
  std::vector<std::vector<double>> layer2_weight_grads_;
  std::vector<double> layer2_bias_grads_;
  std::vector<double> output_weight_grads_;
  double output_bias_grad_;
  
  // Layer normalization parameters
  std::vector<double> layer1_mean_;
  std::vector<double> layer1_std_;
  std::vector<double> layer2_mean_;
  std::vector<double> layer2_std_;
  
  // Helper functions
  void LayerNorm(std::vector<double>& x, const std::vector<double>& mean, const std::vector<double>& std);
  double ReLU(double x);
  double ReLUDerivative(double x);
  
  // Random number generator for initialization
  std::mt19937 rng_;
  std::normal_distribution<double> normal_dist_;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRL_DDPG_NETWORKS_HPP

