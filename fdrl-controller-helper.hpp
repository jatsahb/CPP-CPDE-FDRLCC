#ifndef FDRLCC_HELPERS_FDRL_CONTROLLER_HELPER_HPP_
#define FDRLCC_HELPERS_FDRL_CONTROLLER_HELPER_HPP_

#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/node-container.h"

#include "../controller/fdrl-controller.hpp"
// REFACTORED: MetricStore deleted - using MetricEngine instead
#include "src_cpp/metrics/metric-engine.hpp"
#include "../controller/fdrl-state-features.hpp"
#include "../controller/fdrl-action-policy.hpp"
#include "../controller/fdrl-federation-coordinator.hpp"
#include "../apps/fdrl-consumer.hpp"
#include "../apps/fdrl-router.hpp"
#include "../apps/fdrl-producer.hpp"

namespace ns3 {
namespace ndn {
namespace fdrl {

class FdrlControllerHelper
{
public:
  FdrlControllerHelper();

  void SetUpdateInterval(Time interval);
  void SetSamplingInterval(Time interval);
  void SetNormalizationConfig(const StateFeatures::NormalizationConfig& config);

  Ptr<Controller> Install(Ptr<Node> node) const;
  std::vector<Ptr<Controller>> Install(const NodeContainer& nodes) const;

  void AttachApplications(const Ptr<Controller>& controller,
                          const Ptr<FdrlConsumer>& consumer,
                          const Ptr<FdrlRouter>& router,
                          const Ptr<FdrlProducer>& producer) const;

private:
  Ptr<Controller> CreateController() const;

private:
  Time m_updateInterval;
  Time m_samplingInterval;
  StateFeatures::NormalizationConfig m_normConfig;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_HELPERS_FDRL_CONTROLLER_HELPER_HPP_

