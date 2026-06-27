#ifndef FDRLCC_CONTROLLER_FDRL_ACTION_POLICY_HPP_
#define FDRLCC_CONTROLLER_FDRL_ACTION_POLICY_HPP_

#include "ns3/nstime.h"
#include <functional>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

struct ActionVector
{
  double interestRateFactor = 1.0;
  double queueThresholdFactor = 1.0;
  double forwardingWeightDelta = 0.0;
  double cacheAdjustment = 0.0;
};

/**
 * \brief Applies actions produced by the DRL agent to ndnSIM components.
 */
class ActionPolicy
{
public:
  ActionPolicy();

  void SetInterestRateBounds(double minFactor, double maxFactor);
  void SetQueueThresholdBounds(double minFactor, double maxFactor);
  void SetCacheBounds(double minAdjustment, double maxAdjustment);
  void SetForwardingWeightBounds(double minDelta, double maxDelta);

  void EnableFallback(bool enabled);
  void SetFallbackCooling(ns3::Time duration);
  void ReportFailure();

  using InterestRateHandler = std::function<void(double)>;
  using QueueThresholdHandler = std::function<void(double)>;
  using ForwardingWeightHandler = std::function<void(double)>;
  using CacheAdjustmentHandler = std::function<void(double)>;

  void SetInterestRateHandler(InterestRateHandler handler);
  void SetQueueThresholdHandler(QueueThresholdHandler handler);
  void SetForwardingWeightHandler(ForwardingWeightHandler handler);
  void SetCacheAdjustmentHandler(CacheAdjustmentHandler handler);

  ActionVector Validate(const ActionVector& rawAction) const;
  void Apply(const ActionVector& action);
  
  // Get bounds for saturation detection
  double GetInterestRateMin() const { return m_interestMin; }
  double GetInterestRateMax() const { return m_interestMax; }
  double GetQueueThresholdMin() const { return m_queueMin; }
  double GetQueueThresholdMax() const { return m_queueMax; }

private:
  double Clamp(double value, double minValue, double maxValue) const;
  bool CanApplyAction() const;

private:
  double m_interestMin;
  double m_interestMax;
  double m_queueMin;
  double m_queueMax;
  double m_forwardMin;
  double m_forwardMax;
  double m_cacheMin;
  double m_cacheMax;
  bool m_fallbackEnabled;
  ns3::Time m_fallbackCooldown;
  ns3::Time m_lastFailure;

  InterestRateHandler m_interestRateHandler;
  QueueThresholdHandler m_queueThresholdHandler;
  ForwardingWeightHandler m_forwardingWeightHandler;
  CacheAdjustmentHandler m_cacheAdjustmentHandler;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CONTROLLER_FDRL_ACTION_POLICY_HPP_

