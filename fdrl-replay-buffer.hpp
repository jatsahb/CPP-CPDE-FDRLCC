/**
 * fdrl-replay-buffer.hpp
 * 
 * Experience Replay Buffer for DDPG
 * 
 * Stores transitions (state, action, reward, next_state, done) and provides
 * random batch sampling for off-policy learning.
 */

#ifndef FDRL_REPLAY_BUFFER_HPP
#define FDRL_REPLAY_BUFFER_HPP

#include <vector>
#include <random>
#include <cstddef>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Transition structure for experience replay
 */
struct Transition {
  std::vector<double> state;       // 11-dimensional state
  std::vector<double> action;      // 1-dimensional action (rate factor)
  double reward;                    // Immediate reward
  std::vector<double> next_state;  // 11-dimensional next state
  bool done;                        // Episode termination flag
  
  Transition()
    : reward(0.0), done(false) {}
  
  Transition(const std::vector<double>& s,
             const std::vector<double>& a,
             double r,
             const std::vector<double>& ns,
             bool d)
    : state(s), action(a), reward(r), next_state(ns), done(d) {}
};

/**
 * Experience Replay Buffer
 * 
 * Implements a circular buffer for storing transitions.
 * Provides random batch sampling for DDPG training.
 */
class ReplayBuffer {
public:
  /**
   * Constructor
   * @param capacity Maximum number of transitions to store (default: 50,000)
   */
  explicit ReplayBuffer(size_t capacity = 50000);
  
  ~ReplayBuffer() = default;
  
  /**
   * Add a transition to the buffer
   * @param transition Transition to add
   */
  void Add(const Transition& transition);
  
  /**
   * Sample a random batch of transitions
   * @param batch_size Number of transitions to sample
   * @return Vector of sampled transitions
   */
  std::vector<Transition> Sample(size_t batch_size) const;
  
  /**
   * Check if buffer has enough samples for training
   * @param batch_size Required batch size
   * @return True if buffer has at least batch_size samples
   */
  bool IsReady(size_t batch_size) const;
  
  /**
   * Get current number of stored transitions
   * @return Current size
   */
  size_t Size() const;
  
  /**
   * Get buffer capacity
   * @return Maximum capacity
   */
  size_t Capacity() const;
  
  /**
   * Clear all transitions from buffer
   */
  void Clear();

private:
  std::vector<Transition> buffer_;  // Circular buffer
  size_t capacity_;                 // Maximum capacity
  size_t position_;                 // Current write position (circular)
  size_t size_;                     // Current number of stored transitions
  
  // Random number generator for sampling
  mutable std::mt19937 rng_;
  mutable std::uniform_int_distribution<size_t> uniform_dist_;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRL_REPLAY_BUFFER_HPP

