#include "fdrl-action-policy.hpp"
#include "../simulation/fdrlcc-structured-logger.hpp"  // For DRL action logging
#include "../simulation/fdrlcc-types.hpp"  // For g_structuredLogger, g_enableStructuredLogs

#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

NS_LOG_COMPONENT_DEFINE("ndn.FdrlActionPolicy");

namespace ns3 {
namespace ndn {
namespace fdrl {

ActionPolicy::ActionPolicy()
  : m_interestMin(0.1)
  , m_interestMax(2.0)
  , m_queueMin(0.1)
  , m_queueMax(2.0)
  , m_forwardMin(-1.0)
  , m_forwardMax(1.0)
  , m_cacheMin(-1.0)
  , m_cacheMax(1.0)
{
}

void
ActionPolicy::SetInterestRateBounds(double minFactor, double maxFactor)
{
  m_interestMin = std::min(minFactor, maxFactor);
  m_interestMax = std::max(minFactor, maxFactor);
}

void
ActionPolicy::SetQueueThresholdBounds(double minFactor, double maxFactor)
{
  m_queueMin = std::min(minFactor, maxFactor);
  m_queueMax = std::max(minFactor, maxFactor);
}

void
ActionPolicy::SetCacheBounds(double minAdjustment, double maxAdjustment)
{
  m_cacheMin = std::min(minAdjustment, maxAdjustment);
  m_cacheMax = std::max(minAdjustment, maxAdjustment);
}

void
ActionPolicy::SetForwardingWeightBounds(double minDelta, double maxDelta)
{
  m_forwardMin = std::min(minDelta, maxDelta);
  m_forwardMax = std::max(minDelta, maxDelta);
}

void
ActionPolicy::SetInterestRateHandler(InterestRateHandler handler)
{
  m_interestRateHandler = std::move(handler);
}

void
ActionPolicy::SetQueueThresholdHandler(QueueThresholdHandler handler)
{
  m_queueThresholdHandler = std::move(handler);
}

void
ActionPolicy::SetForwardingWeightHandler(ForwardingWeightHandler handler)
{
  m_forwardingWeightHandler = std::move(handler);
}

void
ActionPolicy::SetCacheAdjustmentHandler(CacheAdjustmentHandler handler)
{
  m_cacheAdjustmentHandler = std::move(handler);
}

ActionVector
ActionPolicy::Validate(const ActionVector& rawAction) const
{
  ActionVector validated = rawAction;
  validated.interestRateFactor = Clamp(rawAction.interestRateFactor, m_interestMin, m_interestMax);
  validated.queueThresholdFactor = Clamp(rawAction.queueThresholdFactor, m_queueMin, m_queueMax);
  validated.forwardingWeightDelta =
    Clamp(rawAction.forwardingWeightDelta, m_forwardMin, m_forwardMax);
  validated.cacheAdjustment = Clamp(rawAction.cacheAdjustment, m_cacheMin, m_cacheMax);
  return validated;
}

void
ActionPolicy::Apply(const ActionVector& action)
{
  ActionVector validated = Validate(action);
  
  // Check if action was saturated (clamped to bounds)
  bool isSaturated = (action.interestRateFactor != validated.interestRateFactor) ||
                     (validated.interestRateFactor <= m_interestMin + 1e-6) ||
                     (validated.interestRateFactor >= m_interestMax - 1e-6);
  
  // Log DRL action application (if structured logging enabled)
  // Note: This logs the actual applied action, while controller logs the decision
  // We don't have state/reward here, so we only log saturation status
  if (g_enableStructuredLogs && g_structuredLogger && isSaturated) {
    std::ostringstream details;
    details << "Action saturated at bounds | raw=" << std::fixed << std::setprecision(4) 
            << action.interestRateFactor
            << " validated=" << validated.interestRateFactor
            << " bounds=[" << m_interestMin << "," << m_interestMax << "]";
    g_structuredLogger->LogEvent(Simulator::Now().GetSeconds(), "ACTION_SATURATED", details.str());
  }

  if (m_interestRateHandler) {
    m_interestRateHandler(validated.interestRateFactor);
  }
  if (m_queueThresholdHandler) {
    m_queueThresholdHandler(validated.queueThresholdFactor);
  }
  if (m_forwardingWeightHandler) {
    m_forwardingWeightHandler(validated.forwardingWeightDelta);
  }
  if (m_cacheAdjustmentHandler) {
    m_cacheAdjustmentHandler(validated.cacheAdjustment);
  }
}

double
ActionPolicy::Clamp(double value, double minValue, double maxValue) const
{
  return std::min(std::max(value, minValue), maxValue);
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

