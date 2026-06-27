/**
 * FDRLCC Metric Aggregator - FIXED VERSION v2
 * 
 * Key improvements for realistic metrics:
 * 1. Periodic queue sampling (every 50ms) for transient queue detection
 * 2. Peak queue tracking per second
 * 3. Pending interest tracking as network load indicator
 * 4. Cache hit vs network response distinction
 * 5. Better congestion formula using multiple signals
 */

#ifndef FDRLCC_METRIC_AGGREGATOR_HPP
#define FDRLCC_METRIC_AGGREGATOR_HPP

#include "ns3/ptr.h"
#include "ns3/node-container.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/queue.h"
#include "ns3/packet.h"
#include "ns3/nstime.h"

#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <vector>
#include <atomic>

namespace ns3 {

// Forward declarations
class Node;
class NetDevice;

namespace ndn {

// Forward declarations
class App;

namespace fdrl {

/**
 * Structure to hold FDRL action for display
 */
struct AggregatorAction {
    double rateFactor = 1.0;
    double queueFactor = 1.0;
    double forwardingDelta = 0.0;
    double cacheAdjustment = 0.0;
};

/**
 * Configuration for MetricAggregator
 * REFACTORED: Removed CSV/debug/console configs - MetricAggregator only collects raw metrics
 */
struct AggregatorConfig {
    int maxQueueSize = 100;  // Maximum queue capacity per device
};

/**
 * MetricAggregator - Real-time metric aggregation v2
 * 
 * Key fixes:
 * - Periodic queue sampling (50ms) catches transient queue buildups
 * - Peak queue tracking for congestion detection
 * - Pending interest count as load indicator
 * - Cache hit vs network response tracking
 */
class MetricAggregator {
public:
    explicit MetricAggregator(const AggregatorConfig& config);
    ~MetricAggregator();
    
    bool Initialize(const std::string& resultsDir);
    void ConnectTraces(const NodeContainer& bottleneckNodes);
    void DisconnectTraces();
    
    // ========== Direct recording from trace callbacks ==========
    
    void RecordInterestSent(uint32_t bytes = 0);
    void RecordDataReceived(uint32_t bytes, double delayMs);
    void RecordQueueDrop(Ptr<const Packet> packet);
    void RecordEnqueue(Ptr<const Packet> packet);  // Deprecated - uses periodic sampling
    void RecordDequeue(Ptr<const Packet> packet);  // Deprecated - uses periodic sampling
    void OnActionApplied(const AggregatorAction& action);
    void SampleQueueLengths();
    
    void ResetCounters();  // REFACTORED: Renamed from FlushPerSecond - just resets counters
    void Shutdown();
    
    // ========== Accessors (raw metrics only) ==========
    // REFACTORED: Removed GetCongestion, GetThroughputMbps - metric computation moved to MetricEngine
    
    double GetQueueOccupancy() const;
    uint64_t GetPendingInterests() const { return m_pendingInterests.load(); }
    uint64_t GetQueuePeak() const { return m_queuePeak.load(); }
    uint64_t GetInterestsSent() const { return m_interestsSent.load(); }
    uint64_t GetDataReceived() const { return m_dataReceived.load(); }
    uint64_t GetPacketsDropped() const { return m_packetsDropped.load(); }
    uint64_t GetBytesReceived() const { return m_bytesReceived.load(); }
    uint64_t GetCacheHits() const { return m_cacheHits.load(); }
    uint64_t GetNetworkResponses() const { return m_networkResponses.load(); }
    double GetDelaySum() const { return m_delaySum; }
    uint64_t GetDelayCount() const { return m_delayCount; }

private:
    void StartPeriodicSampling();
    void PeriodicQueueSample();
    // REFACTORED: Removed PrintConsoleLine, WriteCsvRow, ComputeThroughputSign - output moved to MetricSink

private:
    AggregatorConfig m_config;
    std::string m_resultsDir;
    
    // Per-second counters
    std::atomic<uint64_t> m_interestsSent;
    std::atomic<uint64_t> m_dataReceived;
    std::atomic<uint64_t> m_packetsDropped;
    std::atomic<uint64_t> m_bytesReceived;
    std::atomic<uint64_t> m_packetsInQueue;  // Deprecated
    
    double m_delaySum;
    uint64_t m_delayCount;
    
    // Queue state (sampled periodically)
    std::atomic<uint64_t> m_queueCurrent;
    std::atomic<uint64_t> m_queueMax;
    std::atomic<uint64_t> m_queuePeak;  // Peak queue length this second
    
    // NEW: Network load tracking
    std::atomic<uint64_t> m_pendingInterests;  // Outstanding interests
    std::atomic<uint64_t> m_cacheHits;         // Fast responses (<10ms)
    std::atomic<uint64_t> m_networkResponses;  // Slow responses (>=10ms)
    
    // Monitored devices
    std::vector<Ptr<PointToPointNetDevice>> m_monitoredDevices;
    
    // Action tracking
    AggregatorAction m_lastAction;
    
    // REFACTORED: Removed CSV/debug file streams, console printing flags, throughput/congestion tracking
    // MetricAggregator only collects raw metrics - computation/output moved to MetricEngine/MetricSink
    
    // Periodic sampling
    bool m_samplingActive;
    
    // Thread safety
    mutable std::mutex m_mutex;
    mutable std::mutex m_delayMutex;
    
    // Congestion detection state machine
    enum class CongestionState {
        NOT_CONGESTED,
        CONGESTED
    };
    CongestionState m_congestionState;
    double m_congestionEnterTime;
    double m_reactionLatency;
    bool m_rateReducedAfterCongestion;
    
    // Congestion detection thresholds (using existing values from controller)
    static constexpr double QUEUE_THRESHOLD_HIGH = 0.8;
    static constexpr double QUEUE_THRESHOLD_MODERATE = 0.5;
    static constexpr double DELAY_THRESHOLD = 100.0;  // ms
    static constexpr double LOSS_THRESHOLD_HIGH = 0.15;
    static constexpr double LOSS_THRESHOLD_MODERATE = 0.05;
    
    // Congestion detection and state machine
    void CheckCongestionState(double queueOccupancy, double delayMs, double lossRate, 
                              const AggregatorAction& action);
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_METRIC_AGGREGATOR_HPP
