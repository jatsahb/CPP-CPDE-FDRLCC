/**
 * fdrl-ddpg-networks.cpp
 * 
 * Implementation of Actor and Critic networks for DDPG
 */

#include "fdrl-ddpg-networks.hpp"
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace ns3 {
namespace ndn {
namespace fdrl {

// ============================================================================
// ActorNetwork Implementation
// ============================================================================

ActorNetwork::ActorNetwork(size_t stateDim)
  : state_dim_(stateDim), rng_(42), normal_dist_(0.0, 1.0)
{
  // Initialize layer sizes with configurable state dimension
  layer1_weights_.resize(state_dim_);
  for (auto& row : layer1_weights_) {
    row.resize(256);
  }
  layer1_bias_.resize(256);
  layer1_output_.resize(256);
  
  layer2_weights_.resize(256);
  for (auto& row : layer2_weights_) {
    row.resize(128);
  }
  layer2_bias_.resize(128);
  layer2_output_.resize(128);
  
  output_weights_.resize(128);
  output_bias_ = 0.0;
  
  // Initialize gradients
  layer1_weight_grads_ = layer1_weights_;
  layer1_bias_grads_.resize(256, 0.0);
  layer2_weight_grads_ = layer2_weights_;
  layer2_bias_grads_.resize(128, 0.0);
  output_weight_grads_.resize(128, 0.0);
  output_bias_grad_ = 0.0;
  
  // Initialize layer norm parameters
  layer1_mean_.resize(256, 0.0);
  layer1_std_.resize(256, 1.0);
  layer2_mean_.resize(128, 0.0);
  layer2_std_.resize(128, 1.0);
  
  Initialize();
}

void ActorNetwork::Initialize()
{
  // Xavier initialization for layer 1: state_dim → 256
  double limit1 = std::sqrt(6.0 / (static_cast<double>(state_dim_) + 256.0));
  for (size_t i = 0; i < state_dim_; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] = (normal_dist_(rng_) * 2.0 - 1.0) * limit1;
    }
  }
  std::fill(layer1_bias_.begin(), layer1_bias_.end(), 0.0);
  
  // Xavier initialization for layer 2: 256 → 128
  double limit2 = std::sqrt(6.0 / (256 + 128));
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] = (normal_dist_(rng_) * 2.0 - 1.0) * limit2;
    }
  }
  std::fill(layer2_bias_.begin(), layer2_bias_.end(), 0.0);
  
  // Xavier initialization for output: 128 → 1
  double limit3 = std::sqrt(6.0 / (128 + 1));
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] = (normal_dist_(rng_) * 2.0 - 1.0) * limit3;
  }
  output_bias_ = 0.0;
}

double ActorNetwork::Forward(const std::vector<double>& state)
{
  if (state.size() != state_dim_) {
    std::cerr << "ActorNetwork::Forward: Invalid state size: " << state.size() 
              << " (expected " << state_dim_ << ")" << std::endl;
    return 1.0;  // Default action
  }
  
  // Layer 1: state_dim → 256
  for (size_t j = 0; j < 256; j++) {
    double sum = layer1_bias_[j];
    for (size_t i = 0; i < state_dim_; i++) {
      sum += layer1_weights_[i][j] * state[i];
    }
    layer1_output_[j] = ReLU(sum);
  }
  
  // Layer normalization for layer 1 (simplified: just normalize)
  double mean1 = std::accumulate(layer1_output_.begin(), layer1_output_.end(), 0.0) / 256.0;
  double var1 = 0.0;
  for (double val : layer1_output_) {
    var1 += (val - mean1) * (val - mean1);
  }
  var1 /= 256.0;
  double std1 = std::sqrt(var1 + 1e-8);
  for (size_t j = 0; j < 256; j++) {
    layer1_output_[j] = (layer1_output_[j] - mean1) / std1;
  }
  
  // Layer 2: 256 → 128
  for (size_t j = 0; j < 128; j++) {
    double sum = layer2_bias_[j];
    for (size_t i = 0; i < 256; i++) {
      sum += layer2_weights_[i][j] * layer1_output_[i];
    }
    layer2_output_[j] = ReLU(sum);
  }
  
  // Layer normalization for layer 2
  double mean2 = std::accumulate(layer2_output_.begin(), layer2_output_.end(), 0.0) / 128.0;
  double var2 = 0.0;
  for (double val : layer2_output_) {
    var2 += (val - mean2) * (val - mean2);
  }
  var2 /= 128.0;
  double std2 = std::sqrt(var2 + 1e-8);
  for (size_t j = 0; j < 128; j++) {
    layer2_output_[j] = (layer2_output_[j] - mean2) / std2;
  }
  
  // Output: 128 → 1 (with Tanh, scaled to [0.5, 2.0])
  double output_sum = output_bias_;
  for (size_t i = 0; i < 128; i++) {
    output_sum += output_weights_[i] * layer2_output_[i];
  }
  double tanh_output = Tanh(output_sum);
  
  // Scale from [-1, 1] to [0.5, 2.0]: min + (tanh+1) * (max-min)/2
  return 0.5 + (tanh_output + 1.0) * 0.75;
}

void ActorNetwork::Backward(const std::vector<double>& state, double q_gradient)
{
  if (state.size() != state_dim_) {
    return;
  }
  
  // CRITICAL FIX: Do NOT reset gradients here - accumulate across batch
  // Gradients will be reset explicitly before batch processing via ResetGradients()
  
  // Backprop through output layer (Tanh)
  // Output gradient: q_gradient * tanh_derivative * scale_factor
  double tanh_input = 0.0;  // We need to recompute this
  for (size_t i = 0; i < 128; i++) {
    tanh_input += output_weights_[i] * layer2_output_[i];
  }
  tanh_input += output_bias_;
  
  double tanh_deriv = TanhDerivative(tanh_input);
  double output_grad = q_gradient * tanh_deriv * 0.75;  // Scale factor from Forward
  
  // Gradients for output layer
  for (size_t i = 0; i < 128; i++) {
    output_weight_grads_[i] += output_grad * layer2_output_[i];
  }
  output_bias_grad_ += output_grad;
  
  // Backprop through layer 2 (ReLU)
  std::vector<double> layer2_grad(128, 0.0);
  for (size_t i = 0; i < 128; i++) {
    layer2_grad[i] = output_grad * output_weights_[i] * ReLUDerivative(layer2_output_[i]);
  }
  
  // Gradients for layer 2
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weight_grads_[i][j] += layer2_grad[j] * layer1_output_[i];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_grads_[j] += layer2_grad[j];
  }
  
  // Backprop through layer 1 (ReLU)
  std::vector<double> layer1_grad(256, 0.0);
  for (size_t i = 0; i < 256; i++) {
    double sum = 0.0;
    for (size_t j = 0; j < 128; j++) {
      sum += layer2_grad[j] * layer2_weights_[i][j];
    }
    layer1_grad[i] = sum * ReLUDerivative(layer1_output_[i]);
  }
  
  // Gradients for layer 1
  for (size_t i = 0; i < state_dim_; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weight_grads_[i][j] += layer1_grad[j] * state[i];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_grads_[j] += layer1_grad[j];
  }
}

void ActorNetwork::UpdateWeights(double learning_rate)
{
  // Weight decay coefficient (L2 regularization)
  // TUNING: Increased from 1e-5 to 1e-4 (10x) for consistency with critic
  const double weight_decay = 1e-4;  // 0.0001 (was 0.00001)
  
  // Update output layer
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] += learning_rate * output_weight_grads_[i] - weight_decay * output_weights_[i];
  }
  output_bias_ += learning_rate * output_bias_grad_ - weight_decay * output_bias_;
  
  // Update layer 2
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] += learning_rate * layer2_weight_grads_[i][j] - weight_decay * layer2_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_[j] += learning_rate * layer2_bias_grads_[j] - weight_decay * layer2_bias_[j];
  }
  
  // Update layer 1
  for (size_t i = 0; i < state_dim_; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] += learning_rate * layer1_weight_grads_[i][j] - weight_decay * layer1_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_[j] += learning_rate * layer1_bias_grads_[j] - weight_decay * layer1_bias_[j];
  }
}

void ActorNetwork::SoftUpdate(const ActorNetwork& source, double tau)
{
  // Soft update: θ' ← τ·θ + (1-τ)·θ'
  for (size_t i = 0; i < state_dim_; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] = tau * source.layer1_weights_[i][j] + (1.0 - tau) * layer1_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_[j] = tau * source.layer1_bias_[j] + (1.0 - tau) * layer1_bias_[j];
  }
  
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] = tau * source.layer2_weights_[i][j] + (1.0 - tau) * layer2_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_[j] = tau * source.layer2_bias_[j] + (1.0 - tau) * layer2_bias_[j];
  }
  
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] = tau * source.output_weights_[i] + (1.0 - tau) * output_weights_[i];
  }
  output_bias_ = tau * source.output_bias_ + (1.0 - tau) * output_bias_;
}

std::vector<double> ActorNetwork::Serialize() const
{
  std::vector<double> weights;
  weights.reserve(GetParameterCount());
  
  // Serialize layer 1
  for (const auto& row : layer1_weights_) {
    weights.insert(weights.end(), row.begin(), row.end());
  }
  weights.insert(weights.end(), layer1_bias_.begin(), layer1_bias_.end());
  
  // Serialize layer 2
  for (const auto& row : layer2_weights_) {
    weights.insert(weights.end(), row.begin(), row.end());
  }
  weights.insert(weights.end(), layer2_bias_.begin(), layer2_bias_.end());
  
  // Serialize output
  weights.insert(weights.end(), output_weights_.begin(), output_weights_.end());
  weights.push_back(output_bias_);
  
  return weights;
}

void ActorNetwork::Deserialize(const std::vector<double>& weights)
{
  size_t expected_size = GetParameterCount();
  if (weights.size() != expected_size) {
    std::cerr << "ActorNetwork::Deserialize: Size mismatch. Expected " 
              << expected_size << ", got " << weights.size() << std::endl;
    return;
  }
  
  size_t idx = 0;
  
  // Deserialize layer 1
  for (size_t i = 0; i < state_dim_; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] = weights[idx++];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_[j] = weights[idx++];
  }
  
  // Deserialize layer 2
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] = weights[idx++];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_[j] = weights[idx++];
  }
  
  // Deserialize output
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] = weights[idx++];
  }
  output_bias_ = weights[idx++];
}

size_t ActorNetwork::GetParameterCount() const
{
  return (state_dim_ * 256 + 256) + (256 * 128 + 128) + (128 * 1 + 1);
}

void ActorNetwork::ResetGradients()
{
  // Reset gradients to zero (call before batch accumulation)
  for (auto& row : layer1_weight_grads_) {
    std::fill(row.begin(), row.end(), 0.0);
  }
  std::fill(layer1_bias_grads_.begin(), layer1_bias_grads_.end(), 0.0);
  for (auto& row : layer2_weight_grads_) {
    std::fill(row.begin(), row.end(), 0.0);
  }
  std::fill(layer2_bias_grads_.begin(), layer2_bias_grads_.end(), 0.0);
  std::fill(output_weight_grads_.begin(), output_weight_grads_.end(), 0.0);
  output_bias_grad_ = 0.0;
}

double ActorNetwork::GetGradientNorm() const
{
  // Compute L2 norm of gradients
  double norm_sq = 0.0;
  
  for (const auto& row : layer1_weight_grads_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer1_bias_grads_) {
    norm_sq += val * val;
  }
  for (const auto& row : layer2_weight_grads_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer2_bias_grads_) {
    norm_sq += val * val;
  }
  for (double val : output_weight_grads_) {
    norm_sq += val * val;
  }
  norm_sq += output_bias_grad_ * output_bias_grad_;
  
  return std::sqrt(norm_sq);
}

double ActorNetwork::GetWeightNorm() const
{
  // Compute L2 norm of weights
  double norm_sq = 0.0;
  
  for (const auto& row : layer1_weights_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer1_bias_) {
    norm_sq += val * val;
  }
  for (const auto& row : layer2_weights_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer2_bias_) {
    norm_sq += val * val;
  }
  for (double val : output_weights_) {
    norm_sq += val * val;
  }
  norm_sq += output_bias_ * output_bias_;
  
  return std::sqrt(norm_sq);
}

void ActorNetwork::AverageGradients(size_t batch_size)
{
  if (batch_size == 0) return;
  double inv_batch_size = 1.0 / static_cast<double>(batch_size);
  
  for (auto& row : layer1_weight_grads_) {
    for (double& val : row) {
      val *= inv_batch_size;
    }
  }
  for (double& val : layer1_bias_grads_) {
    val *= inv_batch_size;
  }
  for (auto& row : layer2_weight_grads_) {
    for (double& val : row) {
      val *= inv_batch_size;
    }
  }
  for (double& val : layer2_bias_grads_) {
    val *= inv_batch_size;
  }
  for (double& val : output_weight_grads_) {
    val *= inv_batch_size;
  }
  output_bias_grad_ *= inv_batch_size;
}

void ActorNetwork::ClipGradients(double max_norm)
{
  double norm = GetGradientNorm();
  
  if (norm > max_norm) {
    double scale = max_norm / norm;
    
    for (auto& row : layer1_weight_grads_) {
      for (double& val : row) {
        val *= scale;
      }
    }
    for (double& val : layer1_bias_grads_) {
      val *= scale;
    }
    for (auto& row : layer2_weight_grads_) {
      for (double& val : row) {
        val *= scale;
      }
    }
    for (double& val : layer2_bias_grads_) {
      val *= scale;
    }
    for (double& val : output_weight_grads_) {
      val *= scale;
    }
    output_bias_grad_ *= scale;
  }
}

// Helper functions
// PHASE 1 FIX: Replace ReLU with Leaky ReLU to prevent dead neurons
double ActorNetwork::ReLU(double x)
{
  // Leaky ReLU: f(x) = max(0.01x, x) = x if x > 0, else 0.01x
  return (x > 0.0) ? x : 0.01 * x;
}

double ActorNetwork::ReLUDerivative(double x)
{
  // Leaky ReLU derivative: 1.0 if x > 0, else 0.01
  return (x > 0.0) ? 1.0 : 0.01;
}

double ActorNetwork::Tanh(double x)
{
  return std::tanh(x);
}

double ActorNetwork::TanhDerivative(double x)
{
  double tanh_x = std::tanh(x);
  return 1.0 - tanh_x * tanh_x;
}

// ============================================================================
// CriticNetwork Implementation
// ============================================================================

CriticNetwork::CriticNetwork(size_t stateDim)
  : state_dim_(stateDim), rng_(42), normal_dist_(0.0, 1.0)
{
  // Initialize layer sizes (state + action = (state_dim + 1)D input)
  layer1_weights_.resize(state_dim_ + 1);
  for (auto& row : layer1_weights_) {
    row.resize(256);
  }
  layer1_bias_.resize(256);
  layer1_output_.resize(256);
  
  layer2_weights_.resize(256);
  for (auto& row : layer2_weights_) {
    row.resize(128);
  }
  layer2_bias_.resize(128);
  layer2_output_.resize(128);
  
  output_weights_.resize(128);
  output_bias_ = 0.0;
  
  // Initialize gradients
  layer1_weight_grads_ = layer1_weights_;
  layer1_bias_grads_.resize(256, 0.0);
  layer2_weight_grads_ = layer2_weights_;
  layer2_bias_grads_.resize(128, 0.0);
  output_weight_grads_.resize(128, 0.0);
  output_bias_grad_ = 0.0;
  
  // Initialize layer norm parameters
  layer1_mean_.resize(256, 0.0);
  layer1_std_.resize(256, 1.0);
  layer2_mean_.resize(128, 0.0);
  layer2_std_.resize(128, 1.0);
  
  Initialize();
}

void CriticNetwork::Initialize()
{
  // Xavier initialization for layer 1: (state_dim + 1) → 256 (state + action)
  double limit1 = std::sqrt(6.0 / (static_cast<double>(state_dim_ + 1) + 256.0));
  for (size_t i = 0; i < state_dim_ + 1; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] = (normal_dist_(rng_) * 2.0 - 1.0) * limit1;
    }
  }
  std::fill(layer1_bias_.begin(), layer1_bias_.end(), 0.0);
  
  // Xavier initialization for layer 2: 256 → 128
  double limit2 = std::sqrt(6.0 / (256 + 128));
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] = (normal_dist_(rng_) * 2.0 - 1.0) * limit2;
    }
  }
  std::fill(layer2_bias_.begin(), layer2_bias_.end(), 0.0);
  
  // Xavier initialization for output: 128 → 1
  double limit3 = std::sqrt(6.0 / (128 + 1));
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] = (normal_dist_(rng_) * 2.0 - 1.0) * limit3;
  }
  output_bias_ = 0.0;
}

double CriticNetwork::Forward(const std::vector<double>& state, double action)
{
  if (state.size() != state_dim_) {
    std::cerr << "CriticNetwork::Forward: Invalid state size: " << state.size() 
              << " (expected " << state_dim_ << ")" << std::endl;
    return 0.0;
  }
  
  // Concatenate state and action: [state + action] = (state_dim + 1)D
  std::vector<double> input(state_dim_ + 1);
  for (size_t i = 0; i < state_dim_; i++) {
    input[i] = state[i];
  }
  input[state_dim_] = action;
  
  // Layer 1: (state_dim + 1) → 256
  for (size_t j = 0; j < 256; j++) {
    double sum = layer1_bias_[j];
    for (size_t i = 0; i < state_dim_ + 1; i++) {
      sum += layer1_weights_[i][j] * input[i];
    }
    layer1_output_[j] = ReLU(sum);
  }
  
  // Layer normalization for layer 1
  double mean1 = std::accumulate(layer1_output_.begin(), layer1_output_.end(), 0.0) / 256.0;
  double var1 = 0.0;
  for (double val : layer1_output_) {
    var1 += (val - mean1) * (val - mean1);
  }
  var1 /= 256.0;
  double std1 = std::sqrt(var1 + 1e-8);
  for (size_t j = 0; j < 256; j++) {
    layer1_output_[j] = (layer1_output_[j] - mean1) / std1;
  }
  
  // Layer 2: 256 → 128
  for (size_t j = 0; j < 128; j++) {
    double sum = layer2_bias_[j];
    for (size_t i = 0; i < 256; i++) {
      sum += layer2_weights_[i][j] * layer1_output_[i];
    }
    layer2_output_[j] = ReLU(sum);
  }
  
  // Layer normalization for layer 2
  double mean2 = std::accumulate(layer2_output_.begin(), layer2_output_.end(), 0.0) / 128.0;
  double var2 = 0.0;
  for (double val : layer2_output_) {
    var2 += (val - mean2) * (val - mean2);
  }
  var2 /= 128.0;
  double std2 = std::sqrt(var2 + 1e-8);
  for (size_t j = 0; j < 128; j++) {
    layer2_output_[j] = (layer2_output_[j] - mean2) / std2;
  }
  
  // Output: 128 → 1 (Q-value)
  double output_sum = output_bias_;
  for (size_t i = 0; i < 128; i++) {
    output_sum += output_weights_[i] * layer2_output_[i];
  }
  
  return output_sum;  // Linear output (no activation for Q-value)
}

void CriticNetwork::Backward(const std::vector<double>& state, double action, double td_error)
{
  if (state.size() != state_dim_) {
    return;
  }
  
  // CRITICAL FIX: Do NOT reset gradients here - accumulate across batch
  // Gradients will be reset explicitly before batch processing via ResetGradients()
  
  // Concatenate state and action: [state + action] = (state_dim + 1)D
  std::vector<double> input(state_dim_ + 1);
  for (size_t i = 0; i < state_dim_; i++) {
    input[i] = state[i];
  }
  input[state_dim_] = action;
  
  // Output gradient = -td_error (negative because we minimize (target - current)^2)
  double output_grad = -td_error;
  
  // Gradients for output layer
  for (size_t i = 0; i < 128; i++) {
    output_weight_grads_[i] += output_grad * layer2_output_[i];
  }
  output_bias_grad_ += output_grad;
  
  // Backprop through layer 2 (ReLU)
  std::vector<double> layer2_grad(128, 0.0);
  for (size_t i = 0; i < 128; i++) {
    layer2_grad[i] = output_grad * output_weights_[i] * ReLUDerivative(layer2_output_[i]);
  }
  
  // Gradients for layer 2
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weight_grads_[i][j] += layer2_grad[j] * layer1_output_[i];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_grads_[j] += layer2_grad[j];
  }
  
  // Backprop through layer 1 (ReLU)
  std::vector<double> layer1_grad(256, 0.0);
  for (size_t i = 0; i < 256; i++) {
    double sum = 0.0;
    for (size_t j = 0; j < 128; j++) {
      sum += layer2_grad[j] * layer2_weights_[i][j];
    }
    layer1_grad[i] = sum * ReLUDerivative(layer1_output_[i]);
  }
  
  // Gradients for layer 1
  for (size_t i = 0; i < state_dim_ + 1; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weight_grads_[i][j] += layer1_grad[j] * input[i];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_grads_[j] += layer1_grad[j];
  }
}

double CriticNetwork::GetActionGradient(const std::vector<double>& state, double action)
{
  if (state.size() != state_dim_) {
    return 0.0;
  }
  
  // First, do a forward pass to cache intermediate outputs
  // (We need layer1_output_ and layer2_output_ for gradient computation)
    // Concatenate state and action: [state + action] = (state_dim + 1)D
    std::vector<double> input(state_dim_ + 1);
    for (size_t i = 0; i < state_dim_; i++) {
      input[i] = state[i];
    }
    // Action (rateFactor) is at index state_dim_
    input[state_dim_] = action;
  
  // Forward through layer 1
  for (size_t j = 0; j < 256; j++) {
    double sum = layer1_bias_[j];
    for (size_t i = 0; i < state_dim_ + 1; i++) {
      sum += layer1_weights_[i][j] * input[i];
    }
    layer1_output_[j] = ReLU(sum);
  }
  
  // Forward through layer 2
  for (size_t j = 0; j < 128; j++) {
    double sum = layer2_bias_[j];
    for (size_t i = 0; i < 256; i++) {
      sum += layer2_weights_[i][j] * layer1_output_[i];
    }
    layer2_output_[j] = ReLU(sum);
  }
  
  // Compute gradient of Q w.r.t. action
  // Chain rule: ∂Q/∂a = Σ_j (∂Q/∂layer1[j] * ∂layer1[j]/∂a)
  double grad = 0.0;
  for (size_t j = 0; j < 256; j++) {
    // ∂layer1[j]/∂a = layer1_weights_[state_dim_][j] * ReLU'(layer1_output_[j])
    double layer1_grad_wrt_action = layer1_weights_[state_dim_][j] * ReLUDerivative(layer1_output_[j]);
    
    // ∂Q/∂layer1[j] = Σ_k (∂Q/∂layer2[k] * ∂layer2[k]/∂layer1[j])
    double layer1_grad = 0.0;
    for (size_t k = 0; k < 128; k++) {
      double layer2_grad = output_weights_[k] * ReLUDerivative(layer2_output_[k]);
      layer1_grad += layer2_grad * layer2_weights_[j][k];
    }
    
    grad += layer1_grad_wrt_action * layer1_grad;
  }
  
  return grad;
}

void CriticNetwork::UpdateWeights(double learning_rate)
{
  // Weight decay coefficient (L2 regularization)
  // TUNING: Increased from 1e-5 to 1e-4 (10x) to better prevent weight growth
  // With high LR (0.01) and TD error scaling, weights can grow rapidly
  // Stronger weight decay: w = w + lr*grad - decay*w = w*(1-decay) + lr*grad
  const double weight_decay = 1e-4;  // 0.0001 (was 0.00001)
  
  // Update output layer
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] += learning_rate * output_weight_grads_[i] - weight_decay * output_weights_[i];
  }
  output_bias_ += learning_rate * output_bias_grad_ - weight_decay * output_bias_;
  
  // Update layer 2
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] += learning_rate * layer2_weight_grads_[i][j] - weight_decay * layer2_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_[j] += learning_rate * layer2_bias_grads_[j] - weight_decay * layer2_bias_[j];
  }
  
  // Update layer 1
  for (size_t i = 0; i < state_dim_ + 1; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] += learning_rate * layer1_weight_grads_[i][j] - weight_decay * layer1_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_[j] += learning_rate * layer1_bias_grads_[j] - weight_decay * layer1_bias_[j];
  }
}

void CriticNetwork::SoftUpdate(const CriticNetwork& source, double tau)
{
  // Soft update: θ' ← τ·θ + (1-τ)·θ'
  for (size_t i = 0; i < state_dim_ + 1; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] = tau * source.layer1_weights_[i][j] + (1.0 - tau) * layer1_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_[j] = tau * source.layer1_bias_[j] + (1.0 - tau) * layer1_bias_[j];
  }
  
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] = tau * source.layer2_weights_[i][j] + (1.0 - tau) * layer2_weights_[i][j];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_[j] = tau * source.layer2_bias_[j] + (1.0 - tau) * layer2_bias_[j];
  }
  
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] = tau * source.output_weights_[i] + (1.0 - tau) * output_weights_[i];
  }
  output_bias_ = tau * source.output_bias_ + (1.0 - tau) * output_bias_;
}

std::vector<double> CriticNetwork::Serialize() const
{
  std::vector<double> weights;
  weights.reserve(GetParameterCount());
  
  // Serialize layer 1
  for (const auto& row : layer1_weights_) {
    weights.insert(weights.end(), row.begin(), row.end());
  }
  weights.insert(weights.end(), layer1_bias_.begin(), layer1_bias_.end());
  
  // Serialize layer 2
  for (const auto& row : layer2_weights_) {
    weights.insert(weights.end(), row.begin(), row.end());
  }
  weights.insert(weights.end(), layer2_bias_.begin(), layer2_bias_.end());
  
  // Serialize output
  weights.insert(weights.end(), output_weights_.begin(), output_weights_.end());
  weights.push_back(output_bias_);
  
  return weights;
}

void CriticNetwork::Deserialize(const std::vector<double>& weights)
{
  size_t expected_size = GetParameterCount();
  if (weights.size() != expected_size) {
    std::cerr << "CriticNetwork::Deserialize: Size mismatch. Expected " 
              << expected_size << ", got " << weights.size() << std::endl;
    return;
  }
  
  size_t idx = 0;
  
  // Deserialize layer 1
  for (size_t i = 0; i < state_dim_ + 1; i++) {
    for (size_t j = 0; j < 256; j++) {
      layer1_weights_[i][j] = weights[idx++];
    }
  }
  for (size_t j = 0; j < 256; j++) {
    layer1_bias_[j] = weights[idx++];
  }
  
  // Deserialize layer 2
  for (size_t i = 0; i < 256; i++) {
    for (size_t j = 0; j < 128; j++) {
      layer2_weights_[i][j] = weights[idx++];
    }
  }
  for (size_t j = 0; j < 128; j++) {
    layer2_bias_[j] = weights[idx++];
  }
  
  // Deserialize output
  for (size_t i = 0; i < 128; i++) {
    output_weights_[i] = weights[idx++];
  }
  output_bias_ = weights[idx++];
}

size_t CriticNetwork::GetParameterCount() const
{
  return ((state_dim_ + 1) * 256 + 256) + (256 * 128 + 128) + (128 * 1 + 1);
}

void CriticNetwork::ResetGradients()
{
  // Reset gradients to zero (call before batch accumulation)
  for (auto& row : layer1_weight_grads_) {
    std::fill(row.begin(), row.end(), 0.0);
  }
  std::fill(layer1_bias_grads_.begin(), layer1_bias_grads_.end(), 0.0);
  for (auto& row : layer2_weight_grads_) {
    std::fill(row.begin(), row.end(), 0.0);
  }
  std::fill(layer2_bias_grads_.begin(), layer2_bias_grads_.end(), 0.0);
  std::fill(output_weight_grads_.begin(), output_weight_grads_.end(), 0.0);
  output_bias_grad_ = 0.0;
}

double CriticNetwork::GetGradientNorm() const
{
  // Compute L2 norm of gradients
  double norm_sq = 0.0;
  
  for (const auto& row : layer1_weight_grads_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer1_bias_grads_) {
    norm_sq += val * val;
  }
  for (const auto& row : layer2_weight_grads_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer2_bias_grads_) {
    norm_sq += val * val;
  }
  for (double val : output_weight_grads_) {
    norm_sq += val * val;
  }
  norm_sq += output_bias_grad_ * output_bias_grad_;
  
  return std::sqrt(norm_sq);
}

double CriticNetwork::GetWeightNorm() const
{
  // Compute L2 norm of weights
  double norm_sq = 0.0;
  
  for (const auto& row : layer1_weights_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer1_bias_) {
    norm_sq += val * val;
  }
  for (const auto& row : layer2_weights_) {
    for (double val : row) {
      norm_sq += val * val;
    }
  }
  for (double val : layer2_bias_) {
    norm_sq += val * val;
  }
  for (double val : output_weights_) {
    norm_sq += val * val;
  }
  norm_sq += output_bias_ * output_bias_;
  
  return std::sqrt(norm_sq);
}

void CriticNetwork::AverageGradients(size_t batch_size)
{
  if (batch_size == 0) return;
  double inv_batch_size = 1.0 / static_cast<double>(batch_size);
  
  for (auto& row : layer1_weight_grads_) {
    for (double& val : row) {
      val *= inv_batch_size;
    }
  }
  for (double& val : layer1_bias_grads_) {
    val *= inv_batch_size;
  }
  for (auto& row : layer2_weight_grads_) {
    for (double& val : row) {
      val *= inv_batch_size;
    }
  }
  for (double& val : layer2_bias_grads_) {
    val *= inv_batch_size;
  }
  for (double& val : output_weight_grads_) {
    val *= inv_batch_size;
  }
  output_bias_grad_ *= inv_batch_size;
}

void CriticNetwork::ClipGradients(double max_norm)
{
  double norm = GetGradientNorm();
  
  if (norm > max_norm) {
    double scale = max_norm / norm;
    
    for (auto& row : layer1_weight_grads_) {
      for (double& val : row) {
        val *= scale;
      }
    }
    for (double& val : layer1_bias_grads_) {
      val *= scale;
    }
    for (auto& row : layer2_weight_grads_) {
      for (double& val : row) {
        val *= scale;
      }
    }
    for (double& val : layer2_bias_grads_) {
      val *= scale;
    }
    for (double& val : output_weight_grads_) {
      val *= scale;
    }
    output_bias_grad_ *= scale;
  }
}

// Helper functions
// PHASE 1 FIX: Replace ReLU with Leaky ReLU to prevent dead neurons
double CriticNetwork::ReLU(double x)
{
  // Leaky ReLU: f(x) = max(0.01x, x) = x if x > 0, else 0.01x
  return (x > 0.0) ? x : 0.01 * x;
}

double CriticNetwork::ReLUDerivative(double x)
{
  // Leaky ReLU derivative: 1.0 if x > 0, else 0.01
  return (x > 0.0) ? 1.0 : 0.01;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

