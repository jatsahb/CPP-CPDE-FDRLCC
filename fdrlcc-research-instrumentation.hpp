/**
 * fdrlcc-research-instrumentation.hpp
 * 
 * Research-grade instrumentation for FDRLCC simulation
 * Generates structured datasets for causal analysis and publication-quality results
 * 
 * DO NOT modify core logic - only adds hooks, aggregators, and data writers
 */

#ifndef FDRLCC_RESEARCH_INSTRUMENTATION_HPP
#define FDRLCC_RESEARCH_INSTRUMENTATION_HPP

#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <memory>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Research Instrumentation System
 * 
 * Generates 6 research-grade CSV datasets:
 * 1. congestion_ground_truth.csv - Network reality (router queues, PIT, RTT)
 * 2. agent_state_decision.csv - DRL perception and decisions
 * 3. action_effect_trace.csv - Causal impact of actions
 * 4. reward_learning_trace.csv - Learning dynamics
 * 5. fl_contribution_trace.csv - Federated learning contributions
 * 6. stability_envelope.csv - System-level behavior
 */
class ResearchInstrumentation
{
public:
  /**
   * Initialize instrumentation system
   * @param outputDir Directory for CSV files
   * @param windowSize Time window for aggregation (default: 1.0s)
   */
  static void Initialize(const std::string& outputDir, Time windowSize = Seconds(1.0));
  
  /**
   * Start periodic data collection
   */
  static void Start();
  
  /**
   * Stop instrumentation and close files
   */
  static void Stop();
  
  // ========================================================================
  // Dataset 1: Congestion Ground Truth
  // ========================================================================
  
  /**
   * Record router congestion metrics
   * @param time Simulation time
   * @param routerId Router identifier
   * @param queueOccupancy Average queue occupancy
   * @param p95Rtt 95th percentile RTT
   * @param pitSize PIT size
   * @param linkUtilization Link utilization [0, 1]
   */
  static void RecordCongestionMetrics(double time, const std::string& routerId,
                                      double queueOccupancy, double p95Rtt,
                                      size_t pitSize, double linkUtilization);
  
  // ========================================================================
  // Dataset 2: Agent State and Decision
  // ========================================================================
  
  /**
   * Record DRL agent state and action
   * @param time Simulation time
   * @param agentId Agent/region identifier
   * @param stateVector Normalized state vector
   * @param actionTaken Action (rate scaling factor)
   * @param explorationNoise Exploration noise value
   * @param reward Current reward (raw, not rounded)
   */
  static void RecordAgentDecision(double time, const std::string& agentId,
                                  const std::vector<double>& stateVector,
                                  double actionTaken, double explorationNoise,
                                  double reward);
  
  // ========================================================================
  // Dataset 3: Action Effect Trace
  // ========================================================================
  
  /**
   * Record network state before action
   * @param time Simulation time
   * @param agentId Agent identifier
   * @param rttBefore RTT before action
   * @param queueBefore Queue occupancy before action
   * @param lossBefore Loss rate before action
   */
  static void RecordPreActionState(double time, const std::string& agentId,
                                   double rttBefore, double queueBefore, double lossBefore);
  
  /**
   * Record network state after action and compute deltas
   * @param time Simulation time
   * @param agentId Agent identifier
   * @param actionTaken Action that was applied
   * @param rttAfter RTT after action
   * @param queueAfter Queue occupancy after action
   * @param lossAfter Loss rate after action
   */
  static void RecordPostActionState(double time, const std::string& agentId,
                                    double actionTaken,
                                    double rttAfter, double queueAfter, double lossAfter);
  
  // ========================================================================
  // Dataset 4: Reward and Learning Trace
  // ========================================================================
  
  /**
   * Record training step metrics
   * @param episodeId Episode/training step identifier
   * @param agentId Agent identifier
   * @param rewardRaw Raw reward (not rounded)
   * @param baselineReward Baseline reward for advantage calculation
   * @param advantage Advantage (reward - baseline)
   * @param criticLoss Critic network loss
   * @param actorLoss Actor network loss
   */
  static void RecordLearningStep(uint32_t episodeId, const std::string& agentId,
                                  double rewardRaw, double baselineReward,
                                  double advantage, double criticLoss, double actorLoss);
  
  // ========================================================================
  // Dataset 5: FL Contribution Trace
  // ========================================================================
  
  /**
   * Record FL round contribution
   * @param roundId FL round identifier
   * @param agentId Agent identifier
   * @param localRewardMean Mean local reward
   * @param modelDivergence L2 norm of (θ_local - θ_global)
   * @param aggregationWeight Weight assigned in aggregation
   * @param globalRewardAfter Global reward after FL update
   */
  static void RecordFLContribution(uint32_t roundId, const std::string& agentId,
                                   double localRewardMean, double modelDivergence,
                                   double aggregationWeight, double globalRewardAfter);
  
  // ========================================================================
  // Dataset 6: Stability Envelope
  // ========================================================================
  
  /**
   * Record system-wide stability metrics
   * @param time Simulation time
   * @param systemThroughput Total system throughput (Mbps)
   * @param avgRtt Average RTT across all agents
   * @param packetLoss Packet loss rate [0, 1]
   * @param rateVariance Variance of rate factors across agents
   */
  static void RecordStabilityMetrics(double time, double systemThroughput,
                                     double avgRtt, double packetLoss,
                                     double rateVariance);
  
  // ========================================================================
  // Required Research-Grade Datasets
  // ========================================================================
  
  /**
   * Record network metrics (network_metrics.csv)
   * @param time Simulation time
   * @param avgRtt Average RTT (ms)
   * @param rttVariance RTT variance (ms²)
   * @param queueOccupancy Queue occupancy [0, 1]
   * @param packetLoss Packet loss rate [0, 1]
   * @param throughput Throughput (Mbps)
   */
  static void RecordNetworkMetrics(double time, double avgRtt, double rttVariance,
                                   double queueOccupancy, double packetLoss, double throughput);
  
  /**
   * Record DRL agent metrics (drl_agent_metrics.csv)
   * @param time Simulation time
   * @param agentId Agent identifier
   * @param stateSummary Encoded state summary (comma-separated values)
   * @param actionValue Action value (rate factor)
   * @param reward Current reward
   * @param policyEntropy Policy entropy (exploration noise)
   */
  static void RecordDRLAgentMetrics(double time, const std::string& agentId,
                                    const std::string& stateSummary, double actionValue,
                                    double reward, double policyEntropy);
  
  /**
   * Record FL aggregation metrics (fl_aggregation_metrics.csv)
   * @param time Simulation time
   * @param numClients Number of participating clients
   * @param aggregationWeights Comma-separated weights string
   * @param modelDivergence Model divergence metric
   * @param globalReward Global reward after aggregation
   */
  static void RecordFLAggregationMetrics(double time, size_t numClients,
                                        const std::string& aggregationWeights,
                                        double modelDivergence, double globalReward);
  
  /**
   * Record forwarding metrics (forwarding_metrics.csv)
   * @param time Simulation time
   * @param faceId Face identifier
   * @param interestForwarded Number of interests forwarded
   * @param interestSatisfied Number of interests satisfied
   * @param pathRtt Path RTT (ms)
   */
  static void RecordForwardingMetrics(double time, uint32_t faceId,
                                     uint64_t interestForwarded, uint64_t interestSatisfied,
                                     double pathRtt);

private:
  /**
   * Periodic collection callback
   */
  static void PeriodicCollection();
  
  /**
   * Write buffered data to CSV files
   */
  static void FlushBuffers();
  
  // CSV file streams (original research datasets)
  static std::ofstream m_congestionFile;
  static std::ofstream m_agentStateFile;
  static std::ofstream m_actionEffectFile;
  static std::ofstream m_rewardLearningFile;
  static std::ofstream m_flContributionFile;
  static std::ofstream m_stabilityFile;
  
  // CSV file streams (required research-grade datasets)
  static std::ofstream m_networkMetricsFile;
  static std::ofstream m_drlAgentMetricsFile;
  static std::ofstream m_flAggregationMetricsFile;
  static std::ofstream m_forwardingMetricsFile;
  
  // Configuration
  static std::string m_outputDir;
  static Time m_windowSize;
  static EventId m_collectionEvent;
  static bool m_initialized;
  static bool m_running;
  
  // Buffers for time-window aggregation
  struct PreActionState {
    double rtt;
    double queue;
    double loss;
  };
  static std::map<std::string, PreActionState> m_preActionStates;
  
  // Statistics for aggregation
  static double m_lastFlushTime;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_RESEARCH_INSTRUMENTATION_HPP

