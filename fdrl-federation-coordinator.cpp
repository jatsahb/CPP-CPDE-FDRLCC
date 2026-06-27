#include "fdrl-federation-coordinator.hpp"

#include "ns3/log.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE("ndn.FdrlFederationCoordinator");

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_OBJECT_ENSURE_REGISTERED(FederationCoordinator);

TypeId
FederationCoordinator::GetTypeId()
{
  static TypeId tid =
    TypeId("ns3::ndn::fdrl::FederationCoordinator").SetParent<Object>().AddConstructor<FederationCoordinator>();
  return tid;
}

FederationCoordinator::FederationCoordinator()
  : m_roundInterval(Seconds(5.0))
  , m_running(false)
  , m_federationRound(0)
  , m_lastFederationTime(Seconds(0))
{
}

void
FederationCoordinator::SetRoundInterval(Time interval)
{
  m_roundInterval = interval;
  if (m_running) {
    ScheduleNextRound();
  }
}

Time
FederationCoordinator::GetRoundInterval() const
{
  return m_roundInterval;
}

void
FederationCoordinator::SetOnRequestWeights(std::function<WeightBuffer()> callback)
{
  m_requestWeights = std::move(callback);
}

void
FederationCoordinator::SetOnReceiveGlobal(std::function<void(const WeightBuffer&)> callback)
{
  m_receiveGlobal = std::move(callback);
}

void
FederationCoordinator::SetOnDispatchGlobal(std::function<void(const WeightBuffer&)> callback)
{
  m_dispatchGlobal = std::move(callback);
}

void
FederationCoordinator::TriggerLocalUpdate()
{
  NS_LOG_DEBUG("Local training step triggered");
  // Placeholder: this can signal the Python agent to train immediately if desired.
}

void
FederationCoordinator::TriggerFederatedRound()
{
  m_federationRound++;
  m_lastFederationTime = Simulator::Now();
  
  WeightBuffer weights;
  if (m_requestWeights) {
    weights = m_requestWeights();
  }
  
  // Even if no weights callback, we still log the federation round
  // This allows federation logging to work without Python bindings
  if (weights.empty()) {
    NS_LOG_DEBUG("Federated round " << m_federationRound << ": No weights provided (Python callback not set)");
    // Create empty weight buffer for logging purposes
    weights = WeightBuffer(100, 0);  // Placeholder: 100 bytes of zeros
  } else {
    NS_LOG_INFO("Federated round " << m_federationRound << ": Dispatching weight buffer of size " << weights.size());
  }
  
  DispatchGlobalWeights(weights);
  
  // Schedule next round
  ScheduleNextRound();
}

void
FederationCoordinator::Start()
{
  if (m_running) {
    return;
  }
  m_running = true;
  ScheduleNextRound();
}

void
FederationCoordinator::Stop()
{
  if (!m_running) {
    return;
  }
  m_running = false;
  if (m_roundEvent.IsRunning()) {
    m_roundEvent.Cancel();
  }
}

void
FederationCoordinator::DoDispose()
{
  Stop();
  Object::DoDispose();
}

void
FederationCoordinator::ScheduleNextRound()
{
  if (!m_running || m_roundInterval.IsZero()) {
    return;
  }
  m_roundEvent = Simulator::Schedule(m_roundInterval, &FederationCoordinator::TriggerFederatedRound, this);
}

void
FederationCoordinator::DispatchGlobalWeights(const WeightBuffer& weights)
{
  m_latestGlobalWeights = weights;
  if (m_dispatchGlobal) {
    m_dispatchGlobal(weights);
  }
  if (m_receiveGlobal) {
    m_receiveGlobal(weights);
  }
}

bool
FederationCoordinator::ShouldTriggerFederation() const
{
  if (!m_running) {
    return false;
  }
  Time now = Simulator::Now();
  Time timeSinceLastRound = now - m_lastFederationTime;
  return timeSinceLastRound >= m_roundInterval;
}

int
FederationCoordinator::GetFederationRound() const
{
  return m_federationRound;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

