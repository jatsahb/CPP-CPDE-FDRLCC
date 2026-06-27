#ifndef FDRLCC_APPLICATIONS_FDRL_ROUTER_HPP_
#define FDRLCC_APPLICATIONS_FDRL_ROUTER_HPP_

#include "ns3/ndnSIM/apps/ndn-app.hpp"

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * \brief Router shim application that exposes knobs for congestion control.
 */
class FdrlRouter : public App
{
public:
  static TypeId GetTypeId();

  FdrlRouter();
  ~FdrlRouter() override;

  void ApplyQueueFactor(double factor);
  void ApplyForwardingDelta(double delta);
  void ApplyCacheAdjustment(double adjustment);

  double GetQueueFactor() const;
  double GetForwardingDelta() const;
  double GetCacheAdjustment() const;

protected:
  void StartApplication() override;
  void StopApplication() override;

private:
  void UpdateCacheLimit();

private:
  double m_queueFactor;
  double m_forwardingDelta;
  double m_cacheAdjustment;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_APPLICATIONS_FDRL_ROUTER_HPP_

