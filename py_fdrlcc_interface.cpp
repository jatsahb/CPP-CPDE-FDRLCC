#include "py_fdrlcc_interface.hpp"

#include "../controller/fdrl-controller.hpp"
// REFACTORED: MetricStore deleted - using MetricEngine instead
#include "src_cpp/metrics/metric-engine.hpp"
#include "../controller/fdrl-state-features.hpp"
#include "../controller/fdrl-action-policy.hpp"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/node.h"
#include "ns3/ndnSIM/model/ndn-l3-protocol.hpp"

#include "NFD/daemon/fw/forwarder.hpp"
#include "NFD/daemon/table/pit.hpp"

NS_LOG_COMPONENT_DEFINE("ndn.PyBindingInterface");

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_OBJECT_ENSURE_REGISTERED(PyBindingInterface);

TypeId
PyBindingInterface::GetTypeId()
{
  static TypeId tid = TypeId("ns3::ndn::fdrl::PyBindingInterface")
                        .SetParent<Object>()
                        .AddConstructor<PyBindingInterface>();
  return tid;
}

PyBindingInterface::PyBindingInterface()
  : m_lastStateTime(Seconds(0))
{
}

PyBindingInterface::~PyBindingInterface() = default;

void
PyBindingInterface::SetController(Ptr<Controller> controller)
{
  m_controller = controller;
}

Ptr<Controller>
PyBindingInterface::GetController() const
{
  return m_controller;
}

std::vector<double>
PyBindingInterface::GetState() const
{
  return ExtractStateVector();
}

void
PyBindingInterface::SendAction(double a_rate, double a_qweight)
{
  if (!m_controller) {
    NS_LOG_WARN("Controller not set; cannot send action");
    return;
  }
  
  // Clamp actions to valid range [0.0, 2.0]
  a_rate = std::max(0.0, std::min(2.0, a_rate));
  a_qweight = std::max(0.0, std::min(2.0, a_qweight));
  
  // Create action vector
  ActionVector action;
  action.interestRateFactor = a_rate;
  action.queueThresholdFactor = a_qweight;
  action.forwardingWeightDelta = 0.0;  // Not used in 2D action space
  action.cacheAdjustment = 0.0;       // Not used in 2D action space
  
  // Apply action
  m_controller->ApplyAction(action);
  
  NS_LOG_DEBUG("Action sent: rate=" << a_rate << ", queue_weight=" << a_qweight);
}

void
PyBindingInterface::LogTransition(const std::vector<double>& state,
                                   const std::vector<double>& action,
                                   double reward,
                                   const std::vector<double>& next_state)
{
  // Log transition for debugging/analysis
  NS_LOG_DEBUG("Transition logged: state[" << state.size() << "], "
            << "action[" << action.size() << "], "
            << "reward=" << reward << ", "
            << "next_state[" << next_state.size() << "]");
  
  // Store last state for reference
  m_lastState = next_state;
  m_lastStateTime = Simulator::Now();
}

bool
PyBindingInterface::IsSimulationRunning() const
{
  // Check if simulation is still running
  // Simulation is running if Simulator::Now() < Simulator::GetMaximumSimulationTime()
  Time now = Simulator::Now();
  Time maxTime = Simulator::GetMaximumSimulationTime();
  
  // Also check if controller is active
  if (!m_controller) {
    return false;
  }
  
  return (now < maxTime);
}

double
PyBindingInterface::GetThroughput() const
{
  if (!m_controller) {
    return 0.0;
  }
  // REFACTORED: MetricStore deleted - MetricEngine::GetLatestSnapshot() requires region parameter
  // For legacy Controller, use empty snapshot or get from region (if available)
  auto engine = m_controller->GetMetricStore();  // Returns Ptr<MetricEngine> now
  if (!engine) {
    return 0.0;
  }
  // TODO: Get actual region from controller/context - for now use default
  auto snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
  return snapshot.throughputMbps;
}

double
PyBindingInterface::GetRtt() const
{
  if (!m_controller) {
    return 0.0;
  }
  // REFACTORED: Use MetricEngine snapshot (rttMeanMs -> avgDelayMs)
  auto engine = m_controller->GetMetricStore();  // Returns Ptr<MetricEngine> now
  if (!engine) {
    return 0.0;
  }
  auto snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
  return snapshot.avgDelayMs;  // REFACTORED: avgDelayMs instead of rttMeanMs
}

double
PyBindingInterface::GetQueueSize() const
{
  if (!m_controller) {
    return 0.0;
  }
  // REFACTORED: MetricEngine::GetLatestSnapshot() requires region parameter
  auto engine = m_controller->GetMetricStore();  // Returns Ptr<MetricEngine> now
  if (!engine) {
    return 0.0;
  }
  auto snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
  return snapshot.queueOccupancy;
}

double
PyBindingInterface::GetSatisfactionRatio() const
{
  if (!m_controller) {
    return 0.0;
  }
  // REFACTORED: Use MetricEngine snapshot (packetLossRate calculated from drops/interests)
  auto engine = m_controller->GetMetricStore();  // Returns Ptr<MetricEngine> now
  if (!engine) {
    return 0.0;
  }
  auto snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
  
  // Satisfaction ratio = satisfied / (satisfied + unsatisfied)
  // REFACTORED: Calculate packet loss rate from totalPacketsDropped/totalInterestsSent
  double lossRate = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    lossRate = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
  }
  double satisfaction = 1.0 - lossRate;
  return std::max(0.0, std::min(1.0, satisfaction));
}

double
PyBindingInterface::GetNackRate() const
{
  if (!m_controller) {
    return 0.0;
  }
  // REFACTORED: MetricEngine snapshot doesn't have nackRate - calculate from drops/interests
  auto engine = m_controller->GetMetricStore();  // Returns Ptr<MetricEngine> now
  if (!engine) {
    return 0.0;
  }
  auto snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
  // REFACTORED: nackRate removed - approximate from packet loss rate
  if (snapshot.totalInterestsSent > 0) {
    return static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
  }
  return 0.0;
}

MetricSnapshot
PyBindingInterface::GetMetricSnapshot() const
{
  if (!m_controller) {
    return MetricSnapshot{};
  }
  // REFACTORED: MetricEngine::GetLatestSnapshot() requires region parameter
  auto engine = m_controller->GetMetricStore();  // Returns Ptr<MetricEngine> now
  if (!engine) {
    return MetricSnapshot{};
  }
  // TODO: Get actual region from controller/context - for now use default
  return engine->GetLatestSnapshot("default");  // MetricEngine requires region
}

void
PyBindingInterface::DoInitialize()
{
  Object::DoInitialize();
}

void
PyBindingInterface::DoDispose()
{
  m_controller = nullptr;
  Object::DoDispose();
}

double
PyBindingInterface::GetPitOccupancy() const
{
  if (!m_controller) {
    return 0.0;
  }
  
  // Get the node where controller is installed
  Ptr<Node> node = m_controller->GetObject<Node>();
  if (!node) {
    return 0.0;
  }
  
  auto l3 = node->GetObject<ndn::L3Protocol>();
  if (!l3) {
    return 0.0;
  }
  
  std::shared_ptr<nfd::Forwarder> forwarder = l3->getForwarder();
  if (!forwarder) {
    return 0.0;
  }
  
  nfd::Pit& pit = forwarder->getPit();
  size_t pitSize = pit.size();
  
  // Normalize PIT occupancy (assuming max PIT size of 1000)
  // This is a reasonable assumption for small topologies
  const double maxPitSize = 1000.0;
  return std::min(1.0, static_cast<double>(pitSize) / maxPitSize);
}

std::vector<double>
PyBindingInterface::ExtractStateVector() const
{
  // Extract 9-dimensional state vector as per training plan:
  // 1. Interest arrival rate
  // 2. Data return rate
  // 3. PIT occupancy
  // 4. Queue occupancy
  // 5. RTT
  // 6. Packet drops
  // 7. Congestion indicator (0/1)
  // 8. Cache hit ratio
  // 9. Satisfaction ratio
  
  std::vector<double> state(9, 0.0);
  
  if (!m_controller) {
    return state;
  }
  
  // REFACTORED: Use MetricEngine snapshot (many fields removed, simplified to 5D state metrics)
  auto engine = m_controller->GetMetricStore();  // Returns Ptr<MetricEngine> now
  if (!engine) {
    return state;
  }
  
  auto snapshot = engine->GetLatestSnapshot("default");  // MetricEngine requires region
  
  // REFACTORED: Extract 9D state using available MetricEngine fields
  // 1. Interest arrival rate (normalized, 0-1) - approximate from throughput
  const double maxInterestRate = 100.0;  // pps
  // Use pendingInterests as proxy (normalized)
  state[0] = std::min(1.0, snapshot.pendingInterestsNorm);
  
  // 2. Data return rate (normalized, 0-1) - use throughput normalized
  state[1] = snapshot.throughputNorm;  // Already normalized 0-1
  
  // 3. PIT occupancy (normalized, 0-1)
  state[2] = GetPitOccupancy();
  
  // 4. Queue occupancy (normalized, 0-1)
  state[3] = std::min(1.0, snapshot.queueOccupancy);
  
  // 5. RTT (normalized, 0-1, assuming max 200ms) - REFACTORED: avgDelayMs instead of rttMeanMs
  const double maxRtt = 200.0;  // ms
  state[4] = std::min(1.0, snapshot.avgDelayMs / maxRtt);  // REFACTORED: avgDelayMs
  
  // 6. Packet drops (normalized, 0-1) - REFACTORED: Calculate from totalPacketsDropped/totalInterestsSent
  double lossRate = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    lossRate = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
  }
  state[5] = std::min(1.0, lossRate);
  
  // 7. Congestion indicator (0/1 binary) - REFACTORED: Use queueOccupancy as congestion indicator
  state[6] = (snapshot.queueOccupancy > 0.7) ? 1.0 : 0.0;  // REFACTORED: congestionLevel removed
  
  // 8. Cache hit ratio (0-1)
  state[7] = std::min(1.0, snapshot.cacheHitRatio);
  
  // 9. Satisfaction ratio (0-1)
  double satisfaction = 1.0 - lossRate;  // REFACTORED: Use calculated lossRate
  state[8] = std::max(0.0, std::min(1.0, satisfaction));
  
  // Store for reference
  m_lastState = state;
  m_lastStateTime = Simulator::Now();
  
  return state;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

