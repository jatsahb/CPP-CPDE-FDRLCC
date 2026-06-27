#include "fdrl-state-features.hpp"

#include <algorithm>

namespace ns3 {
namespace ndn {
namespace fdrl {

StateFeatures::StateFeatures()
  : m_config()
{
}

StateFeatures::StateFeatures(NormalizationConfig config)
  : m_config(config)
{
}

void
StateFeatures::SetNormalizationConfig(const NormalizationConfig& config)
{
  m_config = config;
}

const StateFeatures::NormalizationConfig&
StateFeatures::GetNormalizationConfig() const
{
  return m_config;
}

// REFACTORED: ExtractFeatures now returns 5D state vector matching new state space
// State: [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio]
StateFeatures::FeatureVector
StateFeatures::ExtractFeatures(const MetricSnapshot& snapshot) const
{
  FeatureVector features;
  features.reserve(5);

  // State[0] = queueOccupancy (already normalized 0-1)
  features.push_back(Clamp01(snapshot.queueOccupancy));
  
  // State[1] = pendingInterestsNorm (already normalized 0-1 in snapshot)
  features.push_back(Clamp01(snapshot.pendingInterestsNorm));
  
  // State[2] = throughputNorm (already normalized 0-1 in snapshot)
  features.push_back(Clamp01(snapshot.throughputNorm));
  
  // State[3] = avgDelayNorm (already normalized 0-1 in snapshot)
  features.push_back(Clamp01(snapshot.avgDelayNorm));
  
  // State[4] = cacheHitRatio (already normalized 0-1)
  features.push_back(Clamp01(snapshot.cacheHitRatio));

  return features;
}

double
StateFeatures::Normalize(double value, double maxValue) const
{
  if (maxValue <= 0.0) {
    return 0.0;
  }
  return std::clamp(value / maxValue, 0.0, 1.0);
}

double
StateFeatures::Clamp01(double value) const
{
  return std::clamp(value, 0.0, 1.0);
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

