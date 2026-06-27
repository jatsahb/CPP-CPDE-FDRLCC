/**
 * fdrl-replay-buffer.cpp
 * 
 * Implementation of Experience Replay Buffer for DDPG
 */

#include "fdrl-replay-buffer.hpp"
#include <algorithm>
#include <random>

namespace ns3 {
namespace ndn {
namespace fdrl {

ReplayBuffer::ReplayBuffer(size_t capacity)
  : capacity_(capacity)
  , position_(0)
  , size_(0)
  , rng_(42)
{
  buffer_.reserve(capacity_);
  uniform_dist_ = std::uniform_int_distribution<size_t>(0, capacity_ - 1);
}

void ReplayBuffer::Add(const Transition& transition)
{
  if (size_ < capacity_) {
    // Buffer not full yet, append
    buffer_.push_back(transition);
    size_++;
  } else {
    // Buffer full, overwrite oldest (circular buffer)
    buffer_[position_] = transition;
    position_ = (position_ + 1) % capacity_;
  }
}

std::vector<Transition> ReplayBuffer::Sample(size_t batch_size) const
{
  std::vector<Transition> batch;
  batch.reserve(batch_size);
  
  if (size_ == 0) {
    return batch;  // Empty buffer
  }
  
  // Sample random indices
  std::uniform_int_distribution<size_t> dist(0, size_ - 1);
  
  for (size_t i = 0; i < batch_size; i++) {
    size_t idx = dist(rng_);
    batch.push_back(buffer_[idx]);
  }
  
  return batch;
}

bool ReplayBuffer::IsReady(size_t batch_size) const
{
  return size_ >= batch_size;
}

size_t ReplayBuffer::Size() const
{
  return size_;
}

size_t ReplayBuffer::Capacity() const
{
  return capacity_;
}

void ReplayBuffer::Clear()
{
  buffer_.clear();
  position_ = 0;
  size_ = 0;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

