#include "fdrl-controller.hpp"

#include "fdrl-action-policy.hpp"
// REFACTORED: MetricStore deleted - using MetricEngine instead (legacy Controller may not be used in unified codebase)
#include "src_cpp/metrics/metric-engine.hpp"
#include "fdrl-state-features.hpp"
#include "fdrl-federation-coordinator.hpp"
// REMOVED: fdrl-results-logger.hpp - ResultsLogger replaced by StructuredLogger
#include "../simulation/fdrlcc-structured-logger.hpp"  // For congestion logging
#include "../simulation/fdrlcc-types.hpp"  // For g_structuredLogger, g_enableStructuredLogs

#include "ns3/log.h"
#include "ns3/simulator.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

NS_LOG_COMPONENT_DEFINE("ndn.FdrlController");

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_OBJECT_ENSURE_REGISTERED(Controller);

TypeId
Controller::GetTypeId()
{
  static TypeId tid = TypeId("ns3::ndn::fdrl::Controller")
                        .SetParent<Object>()
                        .AddConstructor<Controller>()
                        .AddAttribute("UpdateInterval",
                                      "Control-loop update interval.",
                                      TimeValue(Seconds(0.1)),
                                      MakeTimeAccessor(&Controller::SetUpdateInterval),
                                      MakeTimeChecker());
  return tid;
}

Controller::Controller()
  : m_updateInterval(Seconds(0.1))
  , m_currentReward(0.0)
  , m_lastAction{}
  , m_previousReward(0.0)
  , m_zeroRewardCount(0)
  , m_congestionState(CongestionState::NOT_CONGESTED)
  , m_congestionEnterTime(-1.0)
  , m_reactionLatency(-1.0)
  , m_rateReducedAfterCongestion(false)
{
}

Controller::~Controller()
{
  Shutdown();
}

void
Controller::Initialize()
{
  // REFACTORED: MetricStore deleted - using MetricEngine instead
  if (!m_metricStore) {
    NS_LOG_WARN("MetricEngine not set; controller will operate with default snapshot");
  }
  if (!m_stateFeatures) {
    NS_LOG_WARN("StateFeatures not set; controller will skip feature extraction");
  }
  if (!m_actionPolicy) {
    NS_LOG_WARN("ActionPolicy not set; controller will not apply actions");
  }

  // REFACTORED: MetricStore deleted - MetricEngine Start() handled differently
  if (m_metricStore) {
    // MetricEngine::Start() requires region initialization - handled externally
    NS_LOG_INFO("MetricEngine available (start handled externally)");
  }
  if (m_federationCoordinator) {
    m_federationCoordinator->Start();
    m_federationCoordinator->SetOnRequestWeights([this]() -> WeightBuffer {
      // Fetch weights from Python bridge if callback is set
      if (m_pythonWeightRequestCallback) {
        try {
          return m_pythonWeightRequestCallback();
        }
        catch (const std::exception& e) {
          NS_LOG_ERROR("Python weight request callback failed: " << e.what());
          return WeightBuffer();
        }
      }
      return WeightBuffer();
    });
    m_federationCoordinator->SetOnReceiveGlobal([this](const WeightBuffer& weights) {
      // Send weights to Python bridge if callback is set
      if (m_pythonWeightReceiveCallback) {
        try {
          m_pythonWeightReceiveCallback(weights);
          NS_LOG_DEBUG("Global weights received and forwarded to Python: size=" << weights.size());
        }
        catch (const std::exception& e) {
          NS_LOG_ERROR("Python weight receive callback failed: " << e.what());
        }
      }
      
      // REMOVED: ResultsLogger federated update logging - replaced by StructuredLogger
      // StructuredLogger handles FL logging via LogFL() and LogFederationRound()
      int roundNumber = m_federationCoordinator->GetFederationRound();
      NS_LOG_INFO("Federated Update Applied at t = " << Simulator::Now().GetSeconds() << " (Round " << roundNumber << ")");
    });
  }

  ScheduleNextUpdate();
}

void
Controller::Shutdown()
{
  if (m_scheduledEvent.IsRunning()) {
    m_scheduledEvent.Cancel();
  }
  // REFACTORED: MetricStore deleted - MetricEngine Stop() handled differently
  if (m_metricStore) {
    // MetricEngine::Stop() handled externally - just clear pointer
    NS_LOG_INFO("MetricEngine stopped (handled externally)");
  }
  if (m_federationCoordinator) {
    m_federationCoordinator->Stop();
  }
}

void
Controller::SetUpdateInterval(Time interval)
{
  m_updateInterval = interval;
}

Time
Controller::GetUpdateInterval() const
{
  return m_updateInterval;
}

void
// REFACTORED: MetricStore deleted - using MetricEngine instead
Controller::SetMetricStore(const Ptr<MetricEngine>& engine)
{
  m_metricStore = engine;  // Type compatibility: Ptr<MetricEngine> stored as Ptr<MetricStore> (requires header update)
  NS_LOG_INFO("MetricEngine set (legacy method - consider updating to SetMetricEngine)");
}

void
Controller::SetStateFeatures(const std::shared_ptr<StateFeatures>& features)
{
  m_stateFeatures = features;
}

void
Controller::SetActionPolicy(const std::shared_ptr<ActionPolicy>& policy)
{
  m_actionPolicy = policy;
}

void
Controller::SetFederationCoordinator(const std::shared_ptr<FederationCoordinator>& coordinator)
{
  m_federationCoordinator = coordinator;
}

// REFACTORED: MetricStore deleted - returns MetricEngine instead
Ptr<MetricEngine>
Controller::GetMetricStore() const
{
  // Type cast: Ptr<MetricStore> is actually Ptr<MetricEngine> (requires header update)
  return StaticCast<MetricEngine>(m_metricStore);
}

std::shared_ptr<StateFeatures>
Controller::GetStateFeatures() const
{
  return m_stateFeatures;
}

std::shared_ptr<ActionPolicy>
Controller::GetActionPolicy() const
{
  return m_actionPolicy;
}

std::shared_ptr<FederationCoordinator>
Controller::GetFederationCoordinator() const
{
  return m_federationCoordinator;
}

void
Controller::SetPythonActionCallback(std::function<ActionVector(const std::vector<double>&)> callback)
{
  m_pythonActionCallback = std::move(callback);
  NS_LOG_INFO("Python action callback registered");
}

void
Controller::SetPythonRewardCallback(std::function<double(const MetricSnapshot&)> callback)
{
  m_pythonRewardCallback = std::move(callback);
  NS_LOG_INFO("Python reward callback registered");
}

bool
Controller::HasPythonActionCallback() const
{
  return static_cast<bool>(m_pythonActionCallback);
}

bool
Controller::HasPythonRewardCallback() const
{
  return static_cast<bool>(m_pythonRewardCallback);
}

void
Controller::DoInitialize()
{
  Object::DoInitialize();
}

void
Controller::DoDispose()
{
  Shutdown();
  m_metricStore = nullptr;
  m_stateFeatures.reset();
  m_actionPolicy.reset();
  m_federationCoordinator.reset();
  Object::DoDispose();
}

void
Controller::ScheduleNextUpdate()
{
  m_scheduledEvent = Simulator::Schedule(m_updateInterval, &Controller::PerformControlStep, this);
}

void
Controller::PerformControlStep()
{
  MetricSnapshot snapshot;
  // REFACTORED: MetricStore deleted - MetricEngine requires region parameter
  if (m_metricStore) {
    // MetricEngine::GetLatestSnapshot() requires region - use default or get from state
    // For now, create empty snapshot (Controller may not be used in unified codebase)
    // snapshot = m_metricStore->GetLatestSnapshot("default");  // TODO: Get actual region
    NS_LOG_WARN("Controller::GetLatestSnapshot() - MetricEngine requires region, using empty snapshot");
    snapshot = MetricSnapshot{};  // Empty snapshot
  }

  StateFeatures::FeatureVector features;
  if (m_stateFeatures) {
    features = m_stateFeatures->ExtractFeatures(snapshot);
  }

  ActionVector action{};

  // Use Python callback if available, otherwise fall back to heuristic
  if (m_pythonActionCallback && m_stateFeatures && !features.empty()) {
    // Convert feature vector to std::vector<double> for callback
    std::vector<double> featureVec(features.begin(), features.end());
    try {
      action = m_pythonActionCallback(featureVec);
      NS_LOG_DEBUG("Action selected via Python callback: rate=" << action.interestRateFactor
                                                                 << ", queue=" << action.queueThresholdFactor);
    }
    catch (const std::exception& e) {
      NS_LOG_ERROR("Python action callback failed: " << e.what() << ", falling back to heuristic");
      // Fall through to heuristic
    }
  }

  // Heuristic fallback if Python callback not available or failed
  // Make this MORE DYNAMIC and responsive to actual metrics
  if (!m_pythonActionCallback || action.interestRateFactor == 0.0) {
    // REFACTORED: Use MetricEngine snapshot fields (congestionLevel/queueDropRatio removed, rttMeanMs -> avgDelayMs)
    // Dynamic action based on real metrics - make it vary continuously
    double congestion = snapshot.queueOccupancy;  // REFACTORED: Use queueOccupancy as congestion indicator
    double throughput = snapshot.throughputMbps;
    double latency = snapshot.avgDelayMs;  // REFACTORED: avgDelayMs instead of rttMeanMs
    double queueOcc = snapshot.queueOccupancy;
    // REFACTORED: Calculate drop ratio from totalPacketsDropped/totalInterestsSent
    double dropRatio = 0.0;
    if (snapshot.totalInterestsSent > 0) {
      dropRatio = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
    }
    
    // More granular and dynamic response
    if (congestion > 0.8 || dropRatio > 0.15) {
      // High congestion: reduce rate significantly
      action.interestRateFactor = 0.7 + (congestion - 0.8) * 0.5;  // 0.7 to 0.9 range
      action.queueThresholdFactor = 0.85 + (congestion - 0.8) * 0.3;  // 0.85 to 1.0 range
    }
    else if (congestion > 0.5 || dropRatio > 0.05) {
      // Moderate congestion: slight reduction
      action.interestRateFactor = 0.85 + (congestion - 0.5) * 0.5;  // 0.85 to 1.0 range
      action.queueThresholdFactor = 0.9 + (congestion - 0.5) * 0.2;  // 0.9 to 1.0 range
    }
    else if (throughput < 0.05 && latency < 50.0) {
      // Low throughput but low latency: can increase rate
      action.interestRateFactor = 1.1 + std::min(0.2, (0.05 - throughput) * 4.0);  // 1.1 to 1.3 range
      action.queueThresholdFactor = 1.05 + std::min(0.15, (0.05 - throughput) * 3.0);  // 1.05 to 1.2 range
    }
    else if (latency > 100.0 || queueOcc > 0.8) {
      // High latency or queue: reduce rate
      double latencyFactor = std::min(1.0, latency / 200.0);  // Normalize to 0-1
      action.interestRateFactor = 0.8 + (1.0 - latencyFactor) * 0.3;  // 0.8 to 1.1 range
      action.queueThresholdFactor = 0.9 + (1.0 - latencyFactor) * 0.2;  // 0.9 to 1.1 range
    }
    else {
      // Normal operation: slight variation based on metrics
      double metricVariation = (congestion * 0.3) + (queueOcc * 0.2) + (dropRatio * 0.5);
      action.interestRateFactor = 0.95 + metricVariation * 0.1;  // 0.95 to 1.05 range
      action.queueThresholdFactor = 0.98 + metricVariation * 0.04;  // 0.98 to 1.02 range
    }
    
    // Clamp values to reasonable ranges
    action.interestRateFactor = std::max(0.5, std::min(1.5, action.interestRateFactor));
    action.queueThresholdFactor = std::max(0.7, std::min(1.3, action.queueThresholdFactor));
    
    // Dynamic forwarding weight based on congestion
    action.forwardingWeightDelta = (congestion > 0.6) ? -0.05 : ((throughput < 0.03) ? 0.05 : 0.0);
    action.forwardingWeightDelta = std::max(-0.1, std::min(0.1, action.forwardingWeightDelta));
    
    // Cache adjustment based on hit ratio
    action.cacheAdjustment = (snapshot.cacheHitRatio < 0.3) ? 0.1 : ((snapshot.cacheHitRatio > 0.7) ? -0.05 : 0.0);
    action.cacheAdjustment = std::max(-0.1, std::min(0.2, action.cacheAdjustment));
    
    NS_LOG_DEBUG("Heuristic action: rate=" << action.interestRateFactor 
                << ", queue=" << action.queueThresholdFactor
                << " (congestion=" << congestion << ", throughput=" << throughput << ")");
  }

  // Compute reward
  if (m_pythonRewardCallback) {
    try {
      m_currentReward = m_pythonRewardCallback(snapshot);
      NS_LOG_DEBUG("Reward computed via Python callback: " << m_currentReward);
    }
    catch (const std::exception& e) {
      NS_LOG_ERROR("Python reward callback failed: " << e.what());
      m_currentReward = CalculateReward(snapshot);
    }
  }
  else {
    // Use C++ reward computation
    m_currentReward = CalculateReward(snapshot);
  }

  // Validate reward (check for stuck zero values)
  if (std::abs(m_currentReward) < 1e-6) {
    m_zeroRewardCount++;
    if (m_zeroRewardCount > 3) {
      NS_LOG_WARN("WARNING: Reward stuck at zero for " << m_zeroRewardCount << " intervals");
    }
  } else {
    m_zeroRewardCount = 0;
  }

  // REMOVED: ResultsLogger reward logging - replaced by StructuredLogger
  // StructuredLogger handles reward logging via LogDRLLearning() and LogLearning()

  // Log inference information with detailed metrics
  std::vector<double> featureVec(features.begin(), features.end());
  NS_LOG_INFO("Inference: model received state [" << featureVec.size() << " dims] and returned action [rate=" 
            << action.interestRateFactor << ", queue=" << action.queueThresholdFactor 
            << ", fwd=" << action.forwardingWeightDelta << ", cache=" << action.cacheAdjustment << "]");
  
  // Log state push to Python (if callback exists)
  if (m_pythonActionCallback) {
    std::cout << "[AI] Sending state to Python: [" << featureVec.size() << " dims]" << std::endl;
    std::cout << "[AI] State values: ";
    for (size_t i = 0; i < std::min(featureVec.size(), size_t(5)); ++i) {
      std::cout << featureVec[i] << " ";
    }
    std::cout << "..." << std::endl;
  }
  
  // Log action received from Python or heuristic (disabled for clean display)
  // std::cout << "[AI] Received action from " << (m_pythonActionCallback ? "Python" : "Heuristic") 
  //           << ": rate=" << action.interestRateFactor 
  //           << ", queue=" << action.queueThresholdFactor << std::endl;

  // REMOVED: ResultsLogger action/state logging - replaced by StructuredLogger
  // StructuredLogger handles action/state logging via LogDRLAction() and LogDRLState()

  // Store last action for monitoring
  m_lastAction = action;

  // Check congestion state and log events
  CheckCongestionState(snapshot, action);

  // Log DRL decision using StructuredLogger
  if (g_enableStructuredLogs && g_structuredLogger) {
    // Check if action will be saturated by bounds (validate to see if clamping occurs)
    ActionVector rawAction = action;
    ActionVector validatedAction = action;
    if (m_actionPolicy) {
      validatedAction = m_actionPolicy->Validate(action);
    }
    
    // Determine if action is saturated (clamped to bounds)
    bool isSaturated = false;
    if (m_actionPolicy) {
      // Check if interestRateFactor was clamped (main action we care about)
      double rawRate = rawAction.interestRateFactor;
      double validatedRate = validatedAction.interestRateFactor;
      // Get bounds from action policy
      double rateMin = m_actionPolicy->GetInterestRateMin();
      double rateMax = m_actionPolicy->GetInterestRateMax();
      isSaturated = (rawRate != validatedRate) || 
                    (validatedRate <= rateMin + 1e-6) || 
                    (validatedRate >= rateMax - 1e-6);
    }
    
    // Get congestion state tag
    bool isCongested = (m_congestionState == CongestionState::CONGESTED);
    
    // Convert state vector
    std::vector<double> stateVec(features.begin(), features.end());
    
    // Log DRL action decision
    // Note: LogDRLAction takes single action value, we use interestRateFactor as primary action
    g_structuredLogger->LogDRLAction(Simulator::Now().GetSeconds(),
                                    "default",  // TODO: Get actual region
                                    stateVec,
                                    validatedAction.interestRateFactor,  // Selected action (after validation)
                                    m_currentReward,
                                    isSaturated);
    
    // Also log congestion state tag via LogEvent
    if (isCongested) {
      std::ostringstream details;
      details << "DRL decision in CONGESTED state | action=" << validatedAction.interestRateFactor
              << " reward=" << m_currentReward << " saturated=" << (isSaturated ? "yes" : "no");
      g_structuredLogger->LogEvent(Simulator::Now().GetSeconds(), "DRL_DECISION_CONGESTED", details.str());
    }
  }

  // Apply action
  if (m_actionPolicy) {
    m_actionPolicy->Apply(action);
  }

  if (m_federationCoordinator) {
    m_federationCoordinator->TriggerLocalUpdate();
    // Federation rounds are scheduled automatically by FederationCoordinator
    // The OnReceiveGlobal callback will handle logging when weights are received
  }

  m_previousReward = m_currentReward;
  ScheduleNextUpdate();
}

void
Controller::ApplyAction(const ActionVector& action)
{
  if (m_actionPolicy) {
    m_actionPolicy->Apply(action);
    NS_LOG_DEBUG("Action applied directly: rate=" << action.interestRateFactor
                                                   << ", queue=" << action.queueThresholdFactor);
  }
  else {
    NS_LOG_WARN("ActionPolicy not set; cannot apply action");
  }
}

double
Controller::GetCurrentReward() const
{
  return m_currentReward;
}

ActionVector
Controller::GetLastAction() const
{
  return m_lastAction;
}

void
Controller::SetPythonWeightRequestCallback(std::function<WeightBuffer()> callback)
{
  m_pythonWeightRequestCallback = std::move(callback);
  NS_LOG_INFO("Python weight request callback registered");
}

void
Controller::SetPythonWeightReceiveCallback(std::function<void(const WeightBuffer&)> callback)
{
  m_pythonWeightReceiveCallback = std::move(callback);
  NS_LOG_INFO("Python weight receive callback registered");
}

// REMOVED: SetResultsLogger/GetResultsLogger - ResultsLogger replaced by StructuredLogger
// StructuredLogger is accessed via global g_structuredLogger

double
Controller::CalculateReward(const MetricSnapshot& snapshot) const
{
  // Reward formula: r = w1*T - w2*L - w3*P + w4*J + w5*S - w6*C
  // Weights tuned for realistic network performance
  
  // REFACTORED: Use MetricEngine snapshot fields (rttMeanMs -> avgDelayMs, packetLossRate/congestionLevel removed)
  // Get REAL metrics from snapshot (these should be changing)
  double T = snapshot.throughputMbps;
  double L = snapshot.avgDelayMs;  // REFACTORED: avgDelayMs instead of rttMeanMs
  // REFACTORED: Calculate packet loss rate from totalPacketsDropped/totalInterestsSent
  double P = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    P = static_cast<double>(snapshot.totalPacketsDropped) / static_cast<double>(snapshot.totalInterestsSent);
  }
  double S = snapshot.cacheHitRatio;
  double C = snapshot.queueOccupancy;  // REFACTORED: Use queueOccupancy as congestion indicator (congestionLevel removed)
  double Q = snapshot.queueOccupancy;  // Queue occupancy
  
  // Normalize and scale metrics for reward calculation
  // Throughput: reward higher values (0-1 Mbps range, scale to 0-10)
  double throughputReward = std::min(10.0, T * 10.0);
  
  // Latency: penalize high latency (0-200ms range, scale penalty)
  double latencyPenalty = std::min(10.0, L / 20.0);
  
  // Packet Loss: heavily penalize (0-1 range, scale penalty)
  double lossPenalty = P * 20.0;
  
  // Cache Hit Ratio: reward high hit rates (0-1 range, scale reward)
  double cacheReward = S * 5.0;
  
  // Congestion: penalize high congestion (0-1 range, scale penalty)
  double congestionPenalty = C * 8.0;
  
  // Queue Occupancy: additional penalty for high queue
  double queuePenalty = Q * 3.0;
  
  // Calculate final reward
  double reward = throughputReward - latencyPenalty - lossPenalty + cacheReward - congestionPenalty - queuePenalty;
  
  // Log reward components for debugging (only occasionally to avoid spam)
  static int logCounter = 0;
  if (++logCounter % 10 == 0) {  // Log every 10th calculation
    NS_LOG_INFO("Reward components: T=" << T << "(" << throughputReward 
                << ") L=" << L << "(" << latencyPenalty 
                << ") P=" << P << "(" << lossPenalty
                << ") S=" << S << "(" << cacheReward
                << ") C=" << C << "(" << congestionPenalty
                << ") Q=" << Q << "(" << queuePenalty
                << ") => R=" << reward);
    // [Reward] debug output disabled for clean display
    // std::cout << "[Reward] T=" << T << " L=" << L << " P=" << P 
    //           << " S=" << S << " C=" << C << " Q=" << Q 
    //           << " => R=" << reward << std::endl;
  }
  
  return reward;
}

void
Controller::CheckCongestionState(const MetricSnapshot& snapshot, const ActionVector& action)
{
  // Use existing congestion thresholds from PerformControlStep logic
  double congestion = snapshot.queueOccupancy;
  double latency = snapshot.avgDelayMs;
  double queueOcc = snapshot.queueOccupancy;
  double dropRatio = 0.0;
  if (snapshot.totalInterestsSent > 0) {
    dropRatio = static_cast<double>(snapshot.totalPacketsDropped) / 
                static_cast<double>(snapshot.totalInterestsSent);
  }
  
  // Congestion detection using existing thresholds (same as in PerformControlStep)
  bool isCongested = (congestion > 0.8 || dropRatio > 0.15) ||  // High congestion
                     (congestion > 0.5 || dropRatio > 0.05) ||  // Moderate congestion
                     (latency > 100.0 || queueOcc > 0.8);        // High latency/queue
  
  double currentTime = Simulator::Now().GetSeconds();
  std::string region = "default";  // TODO: Get actual region if available
  
  // State machine transitions
  if (isCongested && m_congestionState == CongestionState::NOT_CONGESTED) {
    // CONGESTION_ENTER
    m_congestionState = CongestionState::CONGESTED;
    m_congestionEnterTime = currentTime;
    m_rateReducedAfterCongestion = false;
    m_reactionLatency = -1.0;  // Reset
    
    // Log CONGESTION_ENTER event
    if (g_enableStructuredLogs && g_structuredLogger) {
      std::ostringstream details;
      details << "Congestion detected | queue=" << std::fixed << std::setprecision(3) << congestion
              << " delay=" << latency << "ms"
              << " loss=" << std::setprecision(4) << dropRatio
              << " queueOcc=" << queueOcc;
      g_structuredLogger->LogEvent(currentTime, "CONGESTION_ENTER", details.str());
      
      // Log congestion metrics at detection time
      g_structuredLogger->LogCongestion(currentTime, region, 
                                       congestion,  // queue
                                       latency,     // delay
                                       dropRatio,   // loss
                                       std::vector<double>());  // state (optional)
    }
    
    NS_LOG_INFO("[CONGESTION_ENTER] t=" << currentTime << "s queue=" << congestion 
                << " delay=" << latency << "ms loss=" << dropRatio);
  }
  else if (!isCongested && m_congestionState == CongestionState::CONGESTED) {
    // CONGESTION_EXIT
    m_congestionState = CongestionState::NOT_CONGESTED;
    
    // Log CONGESTION_EXIT event
    if (g_enableStructuredLogs && g_structuredLogger) {
      double congestionDuration = currentTime - m_congestionEnterTime;
      std::ostringstream details;
      details << "Congestion cleared | duration=" << std::fixed << std::setprecision(2) 
              << congestionDuration << "s"
              << " reaction_latency=";
      if (m_reactionLatency >= 0.0) {
        details << std::setprecision(3) << m_reactionLatency << "s";
      } else {
        details << "N/A";
      }
      g_structuredLogger->LogEvent(currentTime, "CONGESTION_EXIT", details.str());
    }
    
    NS_LOG_INFO("[CONGESTION_EXIT] t=" << currentTime << "s duration=" 
                << (currentTime - m_congestionEnterTime) << "s");
    
    // Reset tracking
    m_congestionEnterTime = -1.0;
    m_rateReducedAfterCongestion = false;
    m_reactionLatency = -1.0;
  }
  
  // Track reaction latency: time from congestion enter to first rate reduction
  if (m_congestionState == CongestionState::CONGESTED && 
      m_congestionEnterTime >= 0.0 && 
      !m_rateReducedAfterCongestion) {
    
    // Check if current action is a rate reduction (rateFactor < 1.0)
    if (action.interestRateFactor < 1.0) {
      m_rateReducedAfterCongestion = true;
      m_reactionLatency = currentTime - m_congestionEnterTime;
      
      // Log reaction latency
      if (g_enableStructuredLogs && g_structuredLogger) {
        std::ostringstream details;
        details << "Reaction latency: " << std::fixed << std::setprecision(3) 
                << m_reactionLatency << "s"
                << " | rate_reduced_to=" << action.interestRateFactor;
        g_structuredLogger->LogEvent(currentTime, "CONGESTION_REACTION", details.str());
      }
      
      NS_LOG_INFO("[CONGESTION_REACTION] t=" << currentTime << "s latency=" 
                  << m_reactionLatency << "s rate=" << action.interestRateFactor);
    }
  }
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

