#ifndef FDRLCC_CONTROLLER_FDRL_FEDERATION_COORDINATOR_HPP_
#define FDRLCC_CONTROLLER_FDRL_FEDERATION_COORDINATOR_HPP_

#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/object.h"
#include <functional>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

using WeightBuffer = std::vector<uint8_t>;

/**
 * \brief Handles local training cadence and federated aggregation rounds.
 */
class FederationCoordinator : public Object
{
public:
  static TypeId GetTypeId();

  FederationCoordinator();

  void SetRoundInterval(Time interval);
  Time GetRoundInterval() const;

  void SetOnRequestWeights(std::function<WeightBuffer()> callback);
  void SetOnReceiveGlobal(std::function<void(const WeightBuffer&)> callback);
  void SetOnDispatchGlobal(std::function<void(const WeightBuffer&)> callback);

  void TriggerLocalUpdate();
  void TriggerFederatedRound();
  void Start();
  void Stop();

  // Check if federation should be triggered
  bool ShouldTriggerFederation() const;
  
  // Get current federation round number
  int GetFederationRound() const;

protected:
  void DoDispose() override;

private:
  void ScheduleNextRound();
  void DispatchGlobalWeights(const WeightBuffer& weights);

private:
  Time m_roundInterval;
  EventId m_roundEvent;
  bool m_running;
  WeightBuffer m_latestGlobalWeights;
  std::function<WeightBuffer()> m_requestWeights;
  std::function<void(const WeightBuffer&)> m_receiveGlobal;
  std::function<void(const WeightBuffer&)> m_dispatchGlobal;
  
  // Federation round tracking
  int m_federationRound;
  Time m_lastFederationTime;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CONTROLLER_FDRL_FEDERATION_COORDINATOR_HPP_

