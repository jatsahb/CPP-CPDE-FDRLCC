#ifndef FDRLCC_CONTROLLER_FDRL_CONTROLLER_HPP_
#define FDRLCC_CONTROLLER_FDRL_CONTROLLER_HPP_

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "fdrl-action-policy.hpp"  // For ActionVector definition
#include <memory>
#include <functional>
#include <vector>

namespace ns3 {
namespace ndn {
namespace fdrl {

// REFACTORED: MetricStore deleted - using MetricEngine instead
class MetricEngine;
class StateFeatures;
class ActionPolicy;
class FederationCoordinator;
// REMOVED: ResultsLogger forward declaration - ResultsLogger replaced by StructuredLogger

struct MetricSnapshot;

// Forward declaration for WeightBuffer
using WeightBuffer = std::vector<uint8_t>;

/**
 * \brief Central manager orchestrating the FDRLCC loop.
 *
 * Responsibilities:
 *  - schedule periodic metric collection and state extraction
 *  - coordinate with the Python agent for action selection
 *  - apply actions via ActionPolicy
 *  - manage rewards, logging, and federation cadence
 */
class Controller : public Object
{
public:
  static TypeId GetTypeId();

  Controller();
  ~Controller() override;

  void Initialize();
  void Shutdown();

  void SetUpdateInterval(Time interval);
  Time GetUpdateInterval() const;

  // REFACTORED: MetricStore deleted - using MetricEngine instead (legacy method names kept for compatibility)
  void SetMetricStore(const Ptr<MetricEngine>& engine);  // Changed from MetricStore to MetricEngine
  void SetStateFeatures(const std::shared_ptr<StateFeatures>& features);
  void SetActionPolicy(const std::shared_ptr<ActionPolicy>& policy);
  void SetFederationCoordinator(const std::shared_ptr<FederationCoordinator>& coordinator);

  Ptr<MetricEngine> GetMetricStore() const;  // Changed from MetricStore to MetricEngine (legacy name kept)
  std::shared_ptr<StateFeatures> GetStateFeatures() const;
  std::shared_ptr<ActionPolicy> GetActionPolicy() const;
  std::shared_ptr<FederationCoordinator> GetFederationCoordinator() const;

  // Python callback registration for action selection and reward computation
  void SetPythonActionCallback(std::function<ActionVector(const std::vector<double>&)> callback);
  void SetPythonRewardCallback(std::function<double(const MetricSnapshot&)> callback);
  bool HasPythonActionCallback() const;
  bool HasPythonRewardCallback() const;

  // Convenience method for applying actions directly (used by Python bindings)
  void ApplyAction(const ActionVector& action);

  // Get current reward (computed via reward callback if available)
  double GetCurrentReward() const;

  // Get last action applied (for monitoring)
  ActionVector GetLastAction() const;

  // Federation weight exchange callbacks
  void SetPythonWeightRequestCallback(std::function<WeightBuffer()> callback);
  void SetPythonWeightReceiveCallback(std::function<void(const WeightBuffer&)> callback);

  // REMOVED: ResultsLogger methods - ResultsLogger replaced by StructuredLogger
  // StructuredLogger is accessed via global g_structuredLogger

protected:
  void DoInitialize() override;
  void DoDispose() override;

private:
  void ScheduleNextUpdate();
  void PerformControlStep();

private:
  Time m_updateInterval;
  EventId m_scheduledEvent;
  Ptr<MetricEngine> m_metricStore;  // REFACTORED: Renamed to MetricEngine but kept variable name for compatibility
  std::shared_ptr<StateFeatures> m_stateFeatures;
  std::shared_ptr<ActionPolicy> m_actionPolicy;
  std::shared_ptr<FederationCoordinator> m_federationCoordinator;

  // Python callbacks for action selection and reward computation
  std::function<ActionVector(const std::vector<double>&)> m_pythonActionCallback;
  std::function<double(const MetricSnapshot&)> m_pythonRewardCallback;

  // Current reward value (updated in PerformControlStep)
  double m_currentReward;

  // Last action applied (for monitoring)
  ActionVector m_lastAction;

  // Federation weight exchange callbacks
  std::function<WeightBuffer()> m_pythonWeightRequestCallback;
  std::function<void(const WeightBuffer&)> m_pythonWeightReceiveCallback;

  // REMOVED: m_resultsLogger - ResultsLogger replaced by StructuredLogger

  // Reward computation state
  double m_previousReward;
  int m_zeroRewardCount;
  
  // Congestion detection state machine
  enum class CongestionState {
    NOT_CONGESTED,
    CONGESTED
  };
  CongestionState m_congestionState;
  double m_congestionEnterTime;  // Time when congestion was detected
  double m_reactionLatency;      // Time from congestion enter to first rate reduction
  bool m_rateReducedAfterCongestion;  // Track if rate was reduced after congestion
  
  // Helper method for reward computation
  double CalculateReward(const MetricSnapshot& snapshot) const;
  
  // Congestion detection and state machine
  void CheckCongestionState(const MetricSnapshot& snapshot, const ActionVector& action);
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CONTROLLER_FDRL_CONTROLLER_HPP_

