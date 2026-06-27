#include "fdrl-controller-helper.hpp"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_LOG_COMPONENT_DEFINE("ndn.FdrlControllerHelper");

FdrlControllerHelper::FdrlControllerHelper()
  : m_updateInterval(Seconds(0.1))
  , m_samplingInterval(Seconds(0.05))
{
}

void
FdrlControllerHelper::SetUpdateInterval(Time interval)
{
  m_updateInterval = interval;
}

void
FdrlControllerHelper::SetSamplingInterval(Time interval)
{
  m_samplingInterval = interval;
}

void
FdrlControllerHelper::SetNormalizationConfig(const StateFeatures::NormalizationConfig& config)
{
  m_normConfig = config;
}

Ptr<Controller>
FdrlControllerHelper::Install(Ptr<Node> node) const
{
  Ptr<Controller> controller = CreateController();
  controller->SetUpdateInterval(m_updateInterval);

  // REFACTORED: MetricStore deleted - using MetricEngine instead
  // Note: MetricEngine requires InitializeRegion() with nodes/consumers, which isn't available here
  // For legacy Controller, create MetricEngine but initialization must be done externally
  Ptr<MetricEngine> metricEngine = CreateObject<MetricEngine>();
  // MetricEngine::Start() requires region initialization - handled externally
  controller->SetMetricStore(metricEngine);  // SetMetricStore now accepts MetricEngine

  auto stateFeatures = std::make_shared<StateFeatures>(m_normConfig);
  controller->SetStateFeatures(stateFeatures);

  auto actionPolicy = std::make_shared<ActionPolicy>();
  controller->SetActionPolicy(actionPolicy);

  auto federation = std::make_shared<FederationCoordinator>();
  controller->SetFederationCoordinator(federation);

  if (node != nullptr) {
    node->AggregateObject(controller);
  }

  controller->Initialize();
  return controller;
}

std::vector<Ptr<Controller>>
FdrlControllerHelper::Install(const NodeContainer& nodes) const
{
  std::vector<Ptr<Controller>> instances;
  instances.reserve(nodes.GetN());
  for (auto it = nodes.Begin(); it != nodes.End(); ++it) {
    instances.push_back(Install(*it));
  }
  return instances;
}

void
FdrlControllerHelper::AttachApplications(const Ptr<Controller>& controller,
                                         const Ptr<FdrlConsumer>& consumer,
                                         const Ptr<FdrlRouter>& router,
                                         const Ptr<FdrlProducer>& producer) const
{
  (void)producer;

  auto policy = controller ? controller->GetActionPolicy() : nullptr;
  if (!policy) {
    NS_LOG_WARN("Controller ActionPolicy not set; cannot attach applications.");
    return;
  }

  if (consumer) {
    policy->SetInterestRateHandler([consumer](double factor) {
      if (consumer) {
        consumer->ApplyRateFactor(factor);
      }
    });
  }

  if (router) {
    policy->SetQueueThresholdHandler([router](double factor) {
      if (router) {
        router->ApplyQueueFactor(factor);
      }
    });
    policy->SetForwardingWeightHandler([router](double delta) {
      if (router) {
        router->ApplyForwardingDelta(delta);
      }
    });
    policy->SetCacheAdjustmentHandler([router](double adj) {
      if (router) {
        router->ApplyCacheAdjustment(adj);
      }
    });
  }
}

Ptr<Controller>
FdrlControllerHelper::CreateController() const
{
  Ptr<Controller> controller = CreateObject<Controller>();
  return controller;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

