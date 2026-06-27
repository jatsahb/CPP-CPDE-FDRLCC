#ifndef FDRLCC_APPLICATIONS_FDRL_CONSUMER_HPP_
#define FDRLCC_APPLICATIONS_FDRL_CONSUMER_HPP_

#include "ns3/ndnSIM/apps/ndn-consumer-cbr.hpp"
#include "ns3/random-variable-stream.h"
#include "ns3/traced-callback.h"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include <map>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Forward declaration
class MetricAggregator;

/**
 * \brief Classical congestion control algorithms
 */
enum class ClassicalCCAlgorithm {
  NONE,   // No congestion control (static rate)
  AIMD,   // Additive Increase Multiplicative Decrease
  BIC,    // Binary Increase Congestion
  CUBIC   // TCP CUBIC
};

/**
 * \brief Consumer that exposes runtime hooks for FDRLCC rate control.
 * 
 * Enhanced features for realistic training data:
 * - Limited content pool for cache hits
 * - Variable request timing
 * - Trace callbacks for real-time metric collection
 * - Realistic interest rates (configurable)
 */
class FdrlConsumer : public ConsumerCbr
{
public:
  static TypeId GetTypeId();

  FdrlConsumer();
  ~FdrlConsumer() override;

  void SetBaseFrequency(double frequencyHz);
  double GetBaseFrequency() const;
  double GetEffectiveFrequency() const;  // Returns actual frequency in use (base * factor * burst)

  void ApplyRateFactor(double factor);  // Called by controller (AI CC mode)
  void ApplyBurstFactor(double factor); // Called by traffic burst events
  double GetCurrentFactor() const;

  /**
   * Enable/disable FDRLCC (AI-based congestion control)
   * @param enable true = AI/DRL CC active, false = Classical CC active
   */
  void EnableFDRLCC(bool enable);
  
  /**
   * Set classical congestion control algorithm
   * (Only used when EnableFDRLCC(false))
   * @param algorithm AIMD, BIC, CUBIC, or NONE
   */
  void SetClassicalCCAlgorithm(ClassicalCCAlgorithm algorithm);
  
  /**
   * Configure classical CC parameters
   * @param increase Additive increase (for AIMD)
   * @param decrease Multiplicative decrease factor
   * @param minFactor Minimum rate factor
   * @param maxFactor Maximum rate factor
   * @param increaseInterval Increase every N successful packets
   */
  void ConfigureClassicalCC(double increase = 0.1,
                            double decrease = 0.5,
                            double minFactor = 0.1,
                            double maxFactor = 2.0,
                            uint32_t increaseInterval = 10);

  /**
   * Enable limited content pool for cache hits
   * @param poolSize Number of unique content items (0 = unlimited/sequential)
   */
  void SetContentPoolSize(uint32_t poolSize);
  uint32_t GetContentPoolSize() const;

  /**
   * Set traffic variation factor (randomness in timing)
   * @param factor 0.0 = constant rate, 1.0 = highly variable
   */
  void SetTrafficVariation(double factor);
  
  /**
   * Enable ON-OFF burst traffic pattern (AGGRESSIVE TOPOLOGY)
   * @param enable true = ON-OFF burst mode, false = smooth traffic
   * @param onRateMultiplier Rate multiplier during ON phase (4-6x recommended)
   */
  void EnableBurstyTraffic(bool enable, double onRateMultiplier = 5.0);
  
  /**
   * Assign random streams to ensure different seeds produce different results
   * @param streamId Base stream ID for this consumer
   */
  void AssignRandomStreams(int64_t streamId);

  /**
   * Set metric aggregator for direct event recording
   * This enables real-time metric collection without file tracing
   */
  void SetMetricAggregator(std::shared_ptr<MetricAggregator> aggregator);
  
  /**
   * Refresh dynamic content universe (for time-based content churn)
   * Increments content offset to ensure new IDs don't overlap with previous ones
   */
  void RefreshDynamicContent();
  
  /**
   * Override SendPacket to include epoch in dynamic content names
   * Note: Not marked override because base class doesn't declare it virtual
   */
  void SendPacket();

  /**
   * Get statistics
   */
  uint64_t GetTotalInterestsSent() const { return m_totalInterestsSent; }
  uint64_t GetTotalDataReceived() const { return m_totalDataReceived; }
  uint64_t GetTotalTimeouts() const { return m_totalTimeouts; }
  uint64_t GetTotalCongestionMarks() const { return m_totalCongestionMarks; }  // PHASE 1: Congestion mark tracking
  
  /**
   * Get delay statistics (for real-time delay measurement)
   * ENHANCED: Separate successful RTT from timeouts
   */
  double GetTotalDelaySum() const { return m_delaySum; }
  uint64_t GetDelayCount() const { return m_delayCount; }
  double GetSuccessfulRttSum() const { return m_successfulRttSum; }  // NEW: Only successful RTT
  uint64_t GetSuccessfulRttCount() const { return m_successfulRttCount; }  // NEW: Count of successful RTT
  uint64_t GetTimeoutsThisSecond() const { return m_timeoutsThisSecond; }
  uint64_t GetDataThisSecond() const { return m_dataThisSecond; }
  double GetAverageDelay() const { 
    return (m_delayCount > 0) ? (m_delaySum / static_cast<double>(m_delayCount)) : 0.0; 
  }
  double GetAverageSuccessfulRtt() const {  // NEW: Average of only successful RTT
    return (m_successfulRttCount > 0) ? (m_successfulRttSum / static_cast<double>(m_successfulRttCount)) : 0.0;
  }
  
  /**
   * Reset delay statistics (for per-second metrics)
   */
  void ResetDelayStats();

protected:
  void StartApplication() override;
  void StopApplication() override;
  void ScheduleNextPacket() override;
  
  // Override to capture events
  void OnData(std::shared_ptr<const Data> data) override;
  void OnTimeout(uint32_t sequenceNumber) override;

private:
  void RefreshFrequency();
  void DoSendInterest();
  
  // ON-OFF burst traffic handlers (AGGRESSIVE TOPOLOGY)
  void ScheduleBurstOn();
  void ScheduleBurstOff();
  
  // Classical CC handlers
  void HandleClassicalCC(std::shared_ptr<const Data> data);
  void HandleClassicalCCTimeout();
  void HandleAIMD(std::shared_ptr<const Data> data);
  void HandleBIC(std::shared_ptr<const Data> data);
  void HandleCUBIC(std::shared_ptr<const Data> data);

private:
  // Control flags
  bool m_enableFDRLCC;                    // AI CC enabled? (default: false)
  ClassicalCCAlgorithm m_classicalCCAlgo;  // Which classical algorithm? (default: AIMD)
  
  // Rate control factors
  double m_baseFrequency;
  double m_currentFactor;      // DRL rate factor (AI CC mode)
  double m_classicalCCFactor;  // Classical CC rate factor (Non-AI CC mode)
  double m_burstFactor;        // External traffic burst factor
  
  // Content pool for cache hits
  uint32_t m_contentPoolSize;
  Ptr<UniformRandomVariable> m_contentRng;
  uint32_t m_contentOffset;  // Offset for dynamic content churn (increments periodically)
  EventId m_contentRefreshEvent;  // Event for periodic content refresh
  
  // Traffic variation
  double m_trafficVariation;
  Ptr<UniformRandomVariable> m_timingRng;
  
  // ON-OFF burst traffic (AGGRESSIVE TOPOLOGY)
  bool m_burstyMode;
  double m_onRateMultiplier;  // 4-6x multiplier during ON phase
  bool m_isBurstOn;           // Current state: ON or OFF
  Ptr<ExponentialRandomVariable> m_onDurationRng;  // ON duration: exponential(mean=2s)
  Ptr<ExponentialRandomVariable> m_offDurationRng;  // OFF duration: exponential(mean=1s)
  EventId m_burstStateEvent;  // Event for ON/OFF transitions
  
  // Classical CC state
  double m_ccIncrease;                    // Additive increase
  double m_ccDecrease;                    // Multiplicative decrease
  double m_ccMinFactor;                   // Minimum factor
  double m_ccMaxFactor;                   // Maximum factor
  uint32_t m_ccSuccessCount;              // Success counter for gradual increase
  uint32_t m_ccIncreaseInterval;          // Increase every N packets
  
  // BIC-specific state
  double m_bicMinWin;
  double m_bicMaxWin;
  double m_bicTargetWin;
  bool m_isBicSlowStart;
  
  // CUBIC-specific state
  double m_cubicWmax;
  double m_cubicLastWmax;
  ::ndn::time::steady_clock::TimePoint m_cubicLastDecrease;
  
  // Metric aggregator for direct recording
  std::shared_ptr<MetricAggregator> m_aggregator;
  
  // Statistics
  uint64_t m_totalInterestsSent;
  uint64_t m_totalDataReceived;
  uint64_t m_totalTimeouts;
  uint64_t m_totalCongestionMarks;  // PHASE 1: Count of congestion-marked packets
  
  // Delay statistics (for real-time measurement)
  double m_delaySum;      // Sum of all measured delays (ms) - includes timeouts as 500ms
  uint64_t m_delayCount;  // Number of delay measurements - includes timeouts
  double m_successfulRttSum;    // NEW: Sum of only successful RTT (ms) - excludes timeouts
  uint64_t m_successfulRttCount; // NEW: Count of successful RTT measurements
  uint64_t m_timeoutsThisSecond;  // Timeouts in current second (for verification)
  uint64_t m_dataThisSecond;      // Data received in current second (for verification)
  
  // Timing for delay calculation
  std::map<uint32_t, Time> m_pendingInterests; // seq -> send time
  
  // Traced callbacks
  TracedCallback<uint32_t> m_interestSentTrace;
  TracedCallback<uint32_t, double> m_dataReceivedTrace; // bytes, delay_ms
  TracedCallback<uint32_t> m_timeoutTrace;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_APPLICATIONS_FDRL_CONSUMER_HPP_
