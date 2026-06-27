/**
 * metric-engine.hpp
 * 
 * Single authoritative metric collection engine for FDRLCC
 * 
 * Responsibilities:
 * - Own ALL raw metric collection
 * - Run on ONE periodic clock (single event loop)
 * - Compute derived metrics ONCE per tick
 * - Emit immutable MetricSnapshot per tick
 */

#ifndef FDRLCC_METRICS_METRIC_ENGINE_HPP
#define FDRLCC_METRICS_METRIC_ENGINE_HPP

#include "ns3/object.h"
#include "ns3/nstime.h"
#include "ns3/event-id.h"
#include "ns3/node-container.h"
#include "ns3/ptr.h"
#include <string>
#include <vector>
#include <cstdint>
#include <map>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Immutable metric snapshot - single source of truth
 * Contains both raw and normalized values
 * MUST be passed by const reference
 */
struct MetricSnapshot {
  // Raw metrics
  uint64_t totalInterestsSent = 0;
  uint64_t totalDataReceived = 0;
  uint64_t totalBytesReceived = 0;  // Actual bytes (not approximation)
  uint64_t totalPacketsDropped = 0;
  uint64_t pendingInterests = 0;
  uint64_t cacheHits = 0;
  uint64_t cacheMisses = 0;
  
  double queueOccupancy = 0.0;      // Normalized 0-1 (average across routers in region)
  double throughputMbps = 0.0;      // Actual throughput in Mbps (bytes * 8 / time / 1e6)
  double avgDelayMs = 0.0;          // Average delay in milliseconds
  double cacheHitRatio = 0.0;       // Normalized 0-1
  
  // RTT gradient (PHASE 1: Added for improved congestion detection)
  double rttGradient = 0.0;         // RTT gradient (ms/s) - rate of change
  double rttGradientNorm = 0.0;     // Normalized RTT gradient [0, 1]
  
  // PHASE 1: Congestion mark tracking
  uint64_t totalCongestionMarks = 0;  // Total congestion-marked packets
  double congestionMarkRate = 0.0;     // Rate of marked packets [0, 1]
  
  // Normalized values (computed ONCE)
  double pendingInterestsNorm = 0.0;  // Normalized 0-1
  double throughputNorm = 0.0;        // Normalized 0-1
  double avgDelayNorm = 0.0;          // Normalized 0-1
  
  Time timestamp;
  
  // Constructor ensures immutability after creation
  MetricSnapshot() = default;
  MetricSnapshot(const MetricSnapshot&) = default;
  MetricSnapshot& operator=(const MetricSnapshot&) = default;
};

/**
 * MetricEngine - Single authoritative source for all metrics
 * 
 * Rules:
 * - ONE periodic collection event
 * - Compute all metrics ONCE per tick
 * - Emit immutable snapshots
 * - NO metric duplication
 */
class MetricEngine : public Object
{
public:
  static TypeId GetTypeId();
  
  MetricEngine();
  ~MetricEngine() override;
  
  /**
   * Initialize engine for a specific region
   * @param region Region identifier
   * @param nodes Nodes in this region
   * @param consumers Consumers in this region
   */
  void InitializeRegion(const std::string& region, 
                       const NodeContainer& nodes,
                       const std::vector<Ptr<class FdrlConsumer>>& consumers);
  
  /**
   * Start metric collection
   * @param interval Collection interval (default: 1.0 seconds)
   */
  void Start(Time interval = Seconds(1.0));
  
  /**
   * Stop metric collection
   */
  void Stop();
  
  /**
   * Get latest snapshot for a region (immutable)
   * @param region Region identifier
   * @return Immutable MetricSnapshot (const reference)
   */
  const MetricSnapshot& GetLatestSnapshot(const std::string& region) const;
  
  /**
   * Check if region is initialized
   */
  bool IsRegionInitialized(const std::string& region) const;

protected:
  void DoInitialize() override;
  void DoDispose() override;

private:
  /**
   * Periodic collection callback - SINGLE event loop
   */
  void CollectMetrics();
  
  /**
   * Collect raw metrics for a specific region
   */
  MetricSnapshot CollectRegionMetrics(const std::string& region);
  
  /**
   * Normalize metrics in snapshot (called ONCE per collection)
   */
  void NormalizeSnapshot(MetricSnapshot& snapshot) const;
  
  /**
   * Schedule next collection
   */
  void ScheduleNextCollection();

private:
  Time m_collectionInterval;
  EventId m_collectionEvent;
  bool m_running;
  
  // Region-specific data
  struct RegionData {
    NodeContainer nodes;
    std::vector<Ptr<class FdrlConsumer>> consumers;
    MetricSnapshot latestSnapshot;
    uint64_t prevInterestsSent = 0;
    uint64_t prevDataReceived = 0;
    uint64_t prevBytesReceived = 0;
    uint64_t prevPacketsDropped = 0;
    Time prevTimestamp;
    
    // PHASE 1: RTT gradient tracking
    double prevRtt = 0.0;           // Previous RTT value (ms)
    Time prevRttTime;                // Previous RTT timestamp
    bool hasPrevRtt = false;         // Flag to indicate if we have previous RTT
  };
  
  std::map<std::string, RegionData> m_regions;
  
  // Normalization bounds (configurable)
  static constexpr double MAX_PENDING_INTERESTS = 1000.0;
  static constexpr double MAX_THROUGHPUT_MBPS = 10.0;
  static constexpr double MAX_DELAY_MS = 500.0;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_METRICS_METRIC_ENGINE_HPP

