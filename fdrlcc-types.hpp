/**
 * fdrlcc-types.hpp
 * 
 * Common type definitions and data structures for FDRLCC simulation
 * Extracted from fdrlcc_unified.cpp for modular architecture
 */

#ifndef FDRLCC_TYPES_HPP
#define FDRLCC_TYPES_HPP

#include "src_cpp/controller/fdrl-ddpg-networks.hpp"
#include "src_cpp/controller/fdrl-replay-buffer.hpp"
#include "src_cpp/metrics/metric-engine.hpp"  // REFACTORED: Replaced MetricStore with MetricEngine
#include "src_cpp/apps/fdrl-consumer.hpp" // For FdrlConsumer complete type
#include "ns3/ptr.h" // For Ptr template complete type
#include <string>
#include <vector>
#include <map>
#include <limits>
#include <fstream>
#include <memory>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Forward declarations
class ResultsLogger;

/**
 * Congestion Control Algorithm Selection
 */
enum class CCAlgorithm {
  AIMD,
  BIC,
  CUBIC,
  FDRLCC
};

/**
 * Experiment Phase Enum
 * Explicit phases for experiment tracking and analysis
 */
enum class ExperimentPhase {
  WARMUP,      // Initial warmup period (0-20% of simTime)
  LEARNING,    // Active learning phase (20-50% of simTime)
  CONGESTION,  // Congestion testing phase (50-80% of simTime)
  STEADY       // Steady-state evaluation (80-100% of simTime)
};

/**
 * Convert ExperimentPhase to string
 */
inline std::string ExperimentPhaseToString(ExperimentPhase phase) {
  switch (phase) {
    case ExperimentPhase::WARMUP: return "WARMUP";
    case ExperimentPhase::LEARNING: return "LEARNING";
    case ExperimentPhase::CONGESTION: return "CONGESTION";
    case ExperimentPhase::STEADY: return "STEADY";
    default: return "UNKNOWN";
  }
}

/**
 * Region DRL State
 * Contains all state for a single region's DRL agent
 */
struct RegionDRLState {
  std::string regionId;
  std::vector<double> state;           // 5-dimensional state vector (REFACTORED: [queueOccupancy, pendingInterestsNorm, throughputNorm, avgDelayNorm, cacheHitRatio])
  std::vector<double> action;          // Action vector (rate factor, etc.)
  double reward = 0.0;                 // Current reward
  double cumulativeReward = 0.0;       // Cumulative reward
  double rateFactor = 1.0;             // Current rate adjustment factor
  double baseFrequency = 0.0;          // Base interest rate
  
  // DDPG Networks
  ActorNetwork actor;                  // Main policy network
  ActorNetwork actor_target;           // Target policy network
  CriticNetwork critic;                // Value network
  CriticNetwork critic_target;         // Target value network
  
  // Experience Replay Buffer
  ReplayBuffer replayBuffer;           // Stores past experiences (50K capacity)
  
  // Training state
  size_t trainingStep = 0;              // Number of training steps
  bool targetNetworksInitialized = false;  // Flag for target network initialization
  
  // Control step tracking for independent training
  uint32_t controlStep = 0;            // Control step counter (incremented every action step)
  std::vector<double> prevState;       // Previous state for transition collection
  std::vector<double> prevAction;      // Previous action for transition collection
  double prevReward = 0.0;             // Previous reward for transition collection
  bool hasPrevTransition = false;       // Flag to track if we have a valid previous transition
  
  // PHASE-1: Dynamic, NDN-aware timing
  double adaptiveActionInterval = 1.0;  // Adaptive action interval (seconds), based on RTT
  double measuredRTT = 0.0;             // Measured RTT from Interest-Data round trips (ms)
  double rttHistory[10];                // RTT history for smoothing (last 10 samples)
  size_t rttHistorySize = 0;            // Current size of RTT history
  size_t rttHistoryIdx = 0;             // Circular buffer index
  double minWarmupSize = 50.0;          // Minimum warmup size (dynamic, starts low)
  double targetWarmupSize = 100.0;      // Target warmup size (adapts based on diversity)
  std::set<std::string> uniqueStates;   // Track unique state signatures for diversity
  
  // Adaptive mixing
  double adaptiveMixingRatio = 0.7;    // Current adaptive mixing ratio
  
  // Stability calculation
  std::vector<double> rewardHistory;   // Last 10 rewards for stability
  double rewardVariance = 0.0;         // Current reward variance
  
  // Performance tracking
  double recentAvgReward = 0.0;        // Average of last 5 rewards
  double smoothedRewardForAggregation = 0.0;  // EMA reward for PW-FedAvg weights
  bool smoothedRewardInitialized = false;
  double lastAppliedRateFactor = 1.0;  // Last successfully applied rate factor
  uint64_t prevTotalInterestsSent = 0; // For Δinterest penalty in reward
  
  // Per-region adaptive learning rates
  double learning_rate_critic = 0.01;   // Critic LR (will decay based on stability)
  double learning_rate_actor = 0.0001;  // Actor LR (base, Region A will override)
  
  // TD error statistics for normalization
  double td_error_mean = 0.0;           // Running mean of TD errors
  double td_error_std = 1.0;            // Running std of TD errors
  size_t td_error_count = 0;            // Count for running statistics
  
  // Q-value normalization statistics (for value function stabilization)
  double q_value_mean = 0.0;            // Running mean of Q-values
  double q_value_std = 1.0;             // Running std of Q-values
  size_t q_value_count = 0;             // Count for running statistics
  
  // Gradient clipping frequency tracking (for conditional clipping)
  uint32_t critic_clip_count = 0;       // Number of times critic gradients were clipped
  uint32_t critic_update_count = 0;     // Total number of critic updates
  double critic_clip_frequency = 0.0;   // Clipping frequency (clip_count / update_count)
  
  // Stability tracking for LR decay
  uint32_t stable_loss_steps = 0;       // Consecutive steps with loss < 50
  uint32_t stable_td_steps = 0;         // Consecutive steps with TD abs_mean < 5
  uint32_t unstable_loss_steps = 0;     // Consecutive steps with loss > 200
};

/**
 * Region Training Statistics
 * Unified structure for training and reward tracking
 * Replaces scattered global variables: g_regionLastReward, g_regionRewardHistory,
 * g_regionRewardWindow, g_regionEpisodeReward, g_globalTrainingStep
 */
struct RegionTrainingStats {
  double lastReward = 0.0;
  double avgReward = 0.0;
  uint32_t trainingSteps = 0;
  uint32_t idleSteps = 0;
  
  // Reward history for moving average and variance calculation
  std::vector<double> rewardHistory;  // Last 100 rewards
  std::vector<double> rewardWindow;  // Last 200 rewards for variance
  
  // Episode tracking
  double episodeReward = 0.0;
  
  // Initialize with default values
  RegionTrainingStats() : lastReward(0.0), avgReward(0.0), 
                          trainingSteps(0), idleSteps(0), episodeReward(0.0) {}
  
  // Update reward history and calculate moving average
  void UpdateReward(double reward) {
    lastReward = reward;
    episodeReward += reward;
    
    rewardHistory.push_back(reward);
    rewardWindow.push_back(reward);
    
    // Keep only last 100 in history
    if (rewardHistory.size() > 100) {
      rewardHistory.erase(rewardHistory.begin());
    }
    
    // Keep only last 200 in window
    if (rewardWindow.size() > 200) {
      rewardWindow.erase(rewardWindow.begin());
    }
    
    // Calculate moving average
    if (!rewardHistory.empty()) {
      double sum = 0.0;
      for (double r : rewardHistory) {
        sum += r;
      }
      avgReward = sum / rewardHistory.size();
    }
  }
  
  // Reset episode reward
  void ResetEpisode() {
    episodeReward = 0.0;
  }
};

/**
 * Topology Information
 * Auto-detected topology structure
 */
struct TopologyInfo {
  std::vector<std::string> regions;  // Auto-detected: sorted unique regions (A, B, C or N1, N2, N3, etc.)
  std::map<std::string, std::vector<std::string>> consumers;  // region -> [node names]
  std::map<std::string, std::vector<std::string>> producers;  // region -> [node names]
  std::map<std::string, std::vector<std::string>> routers;    // region -> [node names] (all types: ER, ERC, ERP, SR, CR, etc.)
};

/**
 * Router Metrics
 * CS/PIT/FIB metrics from a router node
 */
struct RouterMetrics {
  size_t csSize = 0;
  size_t csLimit = 0;
  double csUtilization = 0.0;
  uint64_t csHits = 0;
  uint64_t csMisses = 0;
  double csHitRatio = 0.0;
  size_t pitSize = 0;
  double pitUtilization = 0.0;  // Assuming max PIT size of 1000
  size_t fibSize = 0;
};

/**
 * Training Status
 * Tracks training health and progress
 */
struct TrainingStatus {
  bool isActive = false;
  bool isStalled = false;
  uint32_t lastBufferSize = 0;
  uint32_t lastTrainingStep = 0;
  double lastLossVariance = 0.0;
  double lastRewardVariance = 0.0;
  std::map<std::string, std::vector<double>> lastWeights;  // For weight change detection
  std::map<std::string, uint32_t> bufferGrowthRate;  // Transitions per second
  std::map<std::string, std::vector<double>> lossHistory;  // For variance calculation
  std::map<std::string, std::vector<double>> rewardHistory;  // For variance calculation
};

/**
 * Training Summary
 * Stores training metrics for final summary
 */
struct TrainingSummary {
  uint32_t totalTrainingSteps = 0;
  double initialReward = 0.0;
  double finalReward = 0.0;
  double initialLoss = 0.0;
  double finalLoss = 0.0;
  double finalDivergence = 0.0;
  uint32_t flRounds = 0;
  std::map<std::string, uint32_t> regionTrainingSteps;
  std::map<std::string, double> regionFinalRewards;
};

/**
 * FDRLCC Configuration
 * Centralized configuration structure (replaces magic numbers)
 * Defined before #ifndef block so it's always available to all files
 */
struct FdrlccConfig {
  double flInterval = 5.0;              // FL aggregation interval (seconds)
  size_t replayWarmupSize = 100;       // Minimum replay buffer size before training
  uint32_t trainEveryNSteps = 1;       // Training frequency (every N control steps)
  size_t trainingBatchSize = 64;        // Batch size for training
  double minExplorationNoise = 0.01;    // Minimum exploration noise
  double gamma = 0.95;                  // Discount factor
  double metricCollectionInterval = 1.0; // MetricEngine collection interval (seconds)
  double actionInterval = 5.0;          // DRL action application interval (seconds)
  
  // Action bounds
  double minAction = 0.5;               // Minimum rate factor
  double maxAction = 2.0;               // Maximum rate factor (aligned with actor/executor)
  
  // STEP 3 & 4: Storage efficiency flags
  uint32_t experienceLogInterval = 1;   // Log every N transitions to experience_dataset.csv (default: 1 = every transition)
  uint32_t trainingLogInterval = 1;     // Log every N training steps to training_metrics.csv (default: 1 = every step)
  bool keepIntermediateCheckpoints = false;  // STEP 5: Keep intermediate weights_round_*.txt (default: false = only keep weights_latest.txt)
};

/**
 * Ablation Configuration
 * Controls which system components are disabled for ablation studies
 * Each flag disables exactly one component
 */
struct AblationConfig {
  // Component Ablations (one flag per component)
  bool disableDRL = false;              // Ablation 1: Use heuristic instead of DRL
  bool disableFL = false;                // Ablation 2: Local-only learning (no aggregation)
  bool disableCongestionState = false;   // Ablation 3: Remove congestion features from state
  bool disableCongestionReward = false;  // Ablation 4: Remove congestion terms from reward
  bool disableTargetNetworks = false;    // Ablation 5: Disable target network soft updates
  
  // Auto-generated metadata (set by GenerateAblationLabel)
  std::string ablationLabel;              // Auto-generated label for CSV headers (e.g., "baseline", "no_drl")
  std::string ablationDescription;        // Human-readable description
};

// ============================================================================
// Global Variable Forward Declarations
// ============================================================================
// These are defined in fdrlcc_unified.cpp and accessed via extern in modules
// Note: Extern declarations are excluded when compiling fdrlcc_unified.cpp itself
// to avoid ambiguity with actual definitions

#ifndef FDRLCC_UNIFIED_CPP
// Algorithm selection
extern CCAlgorithm g_selectedAlgorithm;

// Consumers and regions
extern std::vector<Ptr<FdrlConsumer>> g_consumers;
extern std::map<Ptr<FdrlConsumer>, std::string> g_consumerRegions;

// DRL state
extern std::map<std::string, RegionDRLState> g_regionDRL;
extern std::vector<double> g_globalWeights;

// Metrics
// REMOVED: g_regionMetrics - MetricEngine is the single source of truth
extern Ptr<MetricEngine> g_metricEngine;  // Single authoritative MetricEngine
extern TopologyInfo g_topologyInfo;  // Global topology info (set after parsing)

// FL parameters
extern double g_flInterval;
extern uint32_t g_flRound;
extern double g_explorationNoise;
extern double g_noiseDecay;
extern double g_lastFLDivergence;
extern double g_lastFLFairness;
extern double g_lastFLAvgMixing;

// Weights
extern std::map<std::string, std::vector<double>> g_loadedLocalWeights;
extern bool g_weightsLoaded;

// Training tracking
// REFACTORED: Unified training statistics (replaces scattered globals)
extern std::map<std::string, RegionTrainingStats> g_regionStats;
extern std::map<std::string, uint32_t> g_regionNoTrainingCount;
extern std::map<std::string, std::vector<double>> g_regionLastWeights;
extern TrainingStatus g_trainingStatus;
extern TrainingSummary g_trainingSummary;

// FDRLCC Configuration (centralized)
extern FdrlccConfig g_fdrlccConfig;

// Ablation Configuration (centralized)
extern AblationConfig g_ablationConfig;

/**
 * Get state dimension based on ablation configuration
 * Returns 3 if congestion state is disabled, 6 otherwise (baseline)
 */
inline size_t GetStateDim() {
  return g_ablationConfig.disableCongestionState ? 3 : 6;
}

// CSV files
// NOTE: Removed extern declarations for merged CSV files:
// - g_trainingRewardsCsv (merged into training_metrics.csv)
// - g_gradientMetricsCsv (merged into training_metrics.csv)
// - g_tdErrorMetricsCsv (merged into training_metrics.csv)
// - g_fairnessMetricsCsv (merged into fl_metrics.csv)
// - g_trainingLossesCsv (redundant with training_metrics.csv)
// - g_actionsLogCsv (redundant with policy_actions.csv)
// - g_stateTransitionCsv (redundant with experience_dataset.csv)
extern std::ofstream g_flMetricsCsv;
extern std::ofstream g_experienceDatasetCsv;
extern std::ofstream g_trainingMetricsCsv;
// REMOVED: g_policyActionsCsv - replaced by StructuredLogger (logs/drl/action_*.csv)

// Enhanced metrics CSV files
// REMOVED: g_queueMetricsCsv - replaced by StructuredLogger (logs/congestion/congestion_*.csv)
// REMOVED: g_trainingStatusCsv - redundant with training_metrics.csv
extern std::ofstream g_statisticsSummaryCsv;

// PhD Defense Framework CSV files
extern std::ofstream g_network1sCsv;
extern std::ofstream g_learning1sCsv;
extern std::ofstream g_aggregate5sCsv;

// Other globals
// REMOVED: g_logger (ResultsLogger) - replaced by StructuredLogger
extern std::ofstream g_summaryCsv;
extern std::ofstream g_consoleLog;
extern std::string g_resultsDir;
extern double g_simulationTime;
extern uint32_t g_randomSeed;
extern uint32_t g_scenario;
extern uint32_t g_csvWriteCount;
extern uint32_t CSV_FLUSH_INTERVAL;
extern uint32_t g_currentEpisodeId;

// Structured logging (Option 3.5)
extern Ptr<class StructuredLogger> g_structuredLogger;
extern bool g_enableStructuredLogs;

// Experiment phase tracking
extern ExperimentPhase g_currentPhase;
extern double g_totalSimulationTime;  // Total simulation time for phase calculation

// Fail-safe tracking
extern std::map<std::string, double> g_lastReward;
extern std::map<std::string, uint32_t> g_constantRewardCount;
extern std::map<std::string, double> g_lastAction;
extern std::map<std::string, uint32_t> g_constantActionCount;
extern std::map<std::string, bool> g_replayBufferFilled;

// Training loss tracking for status display
extern std::map<std::string, double> g_regionActorLoss;
extern std::map<std::string, double> g_regionCriticLoss;

// File output control flags
extern bool g_saveTraces;
extern bool g_saveConsoleLog;
extern bool g_saveDetailedLogs;
extern bool g_saveCsPitFib;
extern bool g_saveCheckpoints;

// Random number generator for exploration noise
extern std::mt19937 g_rng;
extern std::normal_distribution<double> g_normalDist;

#endif // FDRLCC_UNIFIED_CPP

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_TYPES_HPP
