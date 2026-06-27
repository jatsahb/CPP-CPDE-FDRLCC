#include "fdrl-router.hpp"

#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"

#include "NFD/daemon/fw/forwarder.hpp"
#include "NFD/daemon/table/cs.hpp"

NS_LOG_COMPONENT_DEFINE("ndn.FdrlRouter");

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_OBJECT_ENSURE_REGISTERED(FdrlRouter);

TypeId
FdrlRouter::GetTypeId()
{
  static TypeId tid = TypeId("ns3::ndn::fdrl::FdrlRouter").SetParent<App>().AddConstructor<FdrlRouter>();
  return tid;
}

FdrlRouter::FdrlRouter()
  : m_queueFactor(1.0)
  , m_forwardingDelta(0.0)
  , m_cacheAdjustment(0.0)
{
}

FdrlRouter::~FdrlRouter() = default;

void
FdrlRouter::ApplyQueueFactor(double factor)
{
  m_queueFactor = std::max(0.0, factor);
  NS_LOG_DEBUG("Queue factor updated to " << m_queueFactor);
  
  // Note: Direct PIT queue manipulation is not exposed in NFD API.
  // The queue factor is stored and can be used by:
  // 1. Custom forwarding strategies that respect this factor
  // 2. Metric collection that accounts for queue factor in calculations
  // 3. Future instrumentation when NFD exposes queue controls
  
  // For now, we log the factor for monitoring purposes
  Ptr<Node> node = GetNode();
  if (node) {
    NS_LOG_INFO("Node " << node->GetId() << " queue factor set to " << m_queueFactor);
  }
}

void
FdrlRouter::ApplyForwardingDelta(double delta)
{
  m_forwardingDelta = delta;
  NS_LOG_DEBUG("Forwarding delta updated to " << m_forwardingDelta);
  
  // Note: Forwarding strategy weight adjustment requires:
  // 1. Custom forwarding strategy implementation that respects this delta
  // 2. Or modification of existing strategy's routing cost calculations
  // 
  // The delta can be used to:
  // - Adjust routing costs in custom strategies
  // - Influence forwarding decisions in strategy callbacks
  // - Modify face selection probabilities
  
  // For now, we store the delta for future use or custom strategy integration
  Ptr<Node> node = GetNode();
  if (node) {
    NS_LOG_INFO("Node " << node->GetId() << " forwarding delta set to " << m_forwardingDelta);
    
    // Example: If using a custom forwarding strategy, this delta could modify
    // the routing cost calculation: new_cost = base_cost * (1.0 + delta)
  }
}

void
FdrlRouter::ApplyCacheAdjustment(double adjustment)
{
  m_cacheAdjustment = adjustment;
  UpdateCacheLimit();
}

double
FdrlRouter::GetQueueFactor() const
{
  return m_queueFactor;
}

double
FdrlRouter::GetForwardingDelta() const
{
  return m_forwardingDelta;
}

double
FdrlRouter::GetCacheAdjustment() const
{
  return m_cacheAdjustment;
}

void
FdrlRouter::StartApplication()
{
  App::StartApplication();
}

void
FdrlRouter::StopApplication()
{
  App::StopApplication();
}

void
FdrlRouter::UpdateCacheLimit()
{
  Ptr<Node> node = GetNode();
  if (!node) {
    return;
  }

  auto l3 = node->GetObject<ndn::L3Protocol>();
  if (!l3) {
    return;
  }

  auto forwarder = l3->getForwarder();
  if (!forwarder) {
    return;
  }

  nfd::cs::Cs& cs = forwarder->getCs();
  size_t originalLimit = cs.getLimit();
  if (originalLimit == 0) {
    return;
  }

  size_t newLimit =
    std::max<size_t>(1, static_cast<size_t>(static_cast<double>(originalLimit) *
                                            (1.0 + m_cacheAdjustment)));
  if (newLimit != originalLimit) {
    cs.setLimit(newLimit);
    NS_LOG_DEBUG("Adjusted CS limit for node " << node->GetId() << " to " << newLimit);
  }
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

