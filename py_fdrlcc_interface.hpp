#ifndef PY_FDRLCC_INTERFACE_HPP_
#define PY_FDRLCC_INTERFACE_HPP_

/**
 * \file py_fdrlcc_interface.hpp
 * \brief Stateful Python binding interface for real-time RL training
 *
 * This interface provides a stateful API for Python RL agents to interact
 * with the FDRLCC controller during active simulation.
 */

#include "ns3/object.h"
#include "ns3/nstime.h"
// REFACTORED: MetricStore deleted - using MetricEngine instead
#include "src_cpp/metrics/metric-engine.hpp"
#include <vector>
#include <memory>

namespace ns3 {
namespace ndn {
namespace fdrl {

class Controller;

/**
 * \brief Stateful Python binding interface for real-time RL training
 *
 * Provides methods for:
 * - Getting current state vector (9 dimensions)
 * - Sending actions (2 dimensions: rate, queue_weight)
 * - Checking simulation status
 * - Logging transitions
 */
class PyBindingInterface : public Object
{
public:
  static TypeId GetTypeId();
  
  PyBindingInterface();
  ~PyBindingInterface() override;
  
  // Initialize with controller
  void SetController(Ptr<Controller> controller);
  Ptr<Controller> GetController() const;
  
  // State retrieval (9-dimensional vector)
  std::vector<double> GetState() const;
  
  // Action submission (2 dimensions: rate, queue_weight)
  void SendAction(double a_rate, double a_qweight);
  
  // Transition logging
  void LogTransition(const std::vector<double>& state,
                     const std::vector<double>& action,
                     double reward,
                     const std::vector<double>& next_state);
  
  // Simulation status
  bool IsSimulationRunning() const;
  
  // Metrics access (for reward computation)
  double GetThroughput() const;
  double GetRtt() const;
  double GetQueueSize() const;
  double GetSatisfactionRatio() const;
  double GetNackRate() const;
  
  // Get full metric snapshot
  MetricSnapshot GetMetricSnapshot() const;

protected:
  void DoInitialize() override;
  void DoDispose() override;

private:
  // Extract 9-dimensional state vector from metrics
  std::vector<double> ExtractStateVector() const;
  
  // Get PIT occupancy (for state vector)
  double GetPitOccupancy() const;

private:
  Ptr<Controller> m_controller;
  mutable std::vector<double> m_lastState;
  mutable Time m_lastStateTime;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // PY_FDRLCC_INTERFACE_HPP_

