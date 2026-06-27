#ifndef FDRLCC_CONTROLLER_FDRL_STATE_FEATURES_HPP_
#define FDRLCC_CONTROLLER_FDRL_STATE_FEATURES_HPP_

#include "../metrics/metric-engine.hpp"  // REFACTORED: Use MetricEngine snapshot
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * \brief Transforms raw metric snapshots into normalized feature vectors.
 */
class StateFeatures
{
public:
  using FeatureVector = std::vector<double>;

  struct NormalizationConfig
  {
    // Realistic values for a 1-10 Mbps NDN network with typical consumer rates
    double maxQueueOccupancy = 1.0;      // Already normalized 0-1
    double maxQueueDrop = 0.5;           // 50% drop rate is severe congestion
    double maxInterestRate = 100.0;      // 100 interests/sec is high for single consumer
    double maxDataRate = 100.0;          // 100 data packets/sec
    double maxThroughputMbps = 2.0;      // 2 Mbps for 1 Mbps link (allows >1 for bursts)
    double maxRttMs = 200.0;             // 200ms is high latency for local network
    double maxLossRate = 0.3;            // 30% loss rate is severe
    double maxNackRate = 50.0;           // 50 NACKs/sec is high
    double maxCacheUtilization = 1.0;    // Already normalized 0-1
  };

  StateFeatures();
  explicit StateFeatures(NormalizationConfig config);

  void SetNormalizationConfig(const NormalizationConfig& config);
  const NormalizationConfig& GetNormalizationConfig() const;

  FeatureVector ExtractFeatures(const MetricSnapshot& snapshot) const;

private:
  double Normalize(double value, double maxValue) const;
  double Clamp01(double value) const;

private:
  NormalizationConfig m_config;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CONTROLLER_FDRL_STATE_FEATURES_HPP_

