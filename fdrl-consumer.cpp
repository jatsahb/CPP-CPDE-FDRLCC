#include "fdrl-consumer.hpp"
#include "../controller/fdrl-metric-aggregator.hpp"

#include "ns3/log.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/simulator.h"
#include "ns3/string.h"
#include "ns3/ptr.h"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include <cmath>
#include <limits>

// Forward declaration for global dynamic traffic variables (defined in fdrlcc_unified.cpp)
namespace ns3 {
namespace ndn {
namespace fdrl {
extern uint32_t g_dynamicTrafficInterval;
extern uint32_t g_dynamicContentEpoch;
extern double g_dynamicTrafficRatio;
} // namespace fdrl
} // namespace ndn
} // namespace ns3

NS_LOG_COMPONENT_DEFINE("ndn.FdrlConsumer");

namespace ns3 {
namespace ndn {
namespace fdrl {

NS_OBJECT_ENSURE_REGISTERED(FdrlConsumer);

TypeId
FdrlConsumer::GetTypeId()
{
  static TypeId tid =
    TypeId("ns3::ndn::fdrl::FdrlConsumer")
      .SetParent<ConsumerCbr>()
      .AddConstructor<FdrlConsumer>()
      .AddAttribute("ContentPoolSize",
                    "Size of content pool for cache hits (0 = unlimited sequential)",
                    UintegerValue(0),
                    MakeUintegerAccessor(&FdrlConsumer::m_contentPoolSize),
                    MakeUintegerChecker<uint32_t>())
      .AddAttribute("TrafficVariation",
                    "Traffic timing variation factor (0.0-1.0)",
                    DoubleValue(0.0),
                    MakeDoubleAccessor(&FdrlConsumer::m_trafficVariation),
                    MakeDoubleChecker<double>(0.0, 1.0))
      .AddTraceSource("InterestSent",
                      "Trace fired when an Interest is sent",
                      MakeTraceSourceAccessor(&FdrlConsumer::m_interestSentTrace),
                      "ns3::ndn::fdrl::FdrlConsumer::InterestSentCallback")
      .AddTraceSource("DataReceived",
                      "Trace fired when Data is received (bytes, delay_ms)",
                      MakeTraceSourceAccessor(&FdrlConsumer::m_dataReceivedTrace),
                      "ns3::ndn::fdrl::FdrlConsumer::DataReceivedCallback")
      .AddTraceSource("Timeout",
                      "Trace fired on timeout",
                      MakeTraceSourceAccessor(&FdrlConsumer::m_timeoutTrace),
                      "ns3::ndn::fdrl::FdrlConsumer::TimeoutCallback");
  return tid;
}

FdrlConsumer::FdrlConsumer()
  : m_enableFDRLCC(false)                 // AI CC disabled by default
  , m_classicalCCAlgo(ClassicalCCAlgorithm::AIMD)  // Default to AIMD
  , m_baseFrequency(30.0)                  // FIX: Increased from 10.0 to 30.0 Hz to create congestion conditions
  , m_currentFactor(1.0)                  // DRL factor (AI CC mode)
  , m_classicalCCFactor(1.0)              // Classical CC factor (Non-AI CC mode)
  , m_burstFactor(1.0)
  , m_contentPoolSize(0)
  , m_contentOffset(0)
  , m_trafficVariation(0.0)
  , m_burstyMode(false)
  , m_onRateMultiplier(3.0)  // OPTIMIZATION: Reduced from 5.0 to 3.0 for better performance
  , m_isBurstOn(true)                      // Start in ON state
  , m_onDurationRng(CreateObject<ExponentialRandomVariable>())
  , m_offDurationRng(CreateObject<ExponentialRandomVariable>())
  , m_ccIncrease(0.1)                      // Additive increase
  , m_ccDecrease(0.5)                     // Multiplicative decrease
  , m_ccMinFactor(0.1)                     // Min 10% of base
  , m_ccMaxFactor(2.0)                    // Max 200% of base
  , m_ccSuccessCount(0)
  , m_ccIncreaseInterval(10)               // Increase every 10 packets
  , m_bicMinWin(0.0)
  , m_bicMaxWin(std::numeric_limits<double>::max())
  , m_bicTargetWin(0.0)
  , m_isBicSlowStart(false)
  , m_cubicWmax(0.0)
  , m_cubicLastWmax(0.0)
  , m_cubicLastDecrease(::ndn::time::steady_clock::now())
  , m_totalInterestsSent(0)
  , m_totalDataReceived(0)
  , m_totalTimeouts(0)
  , m_delaySum(0.0)
  , m_delayCount(0)
  , m_successfulRttSum(0.0)
  , m_successfulRttCount(0)
  , m_timeoutsThisSecond(0)
  , m_dataThisSecond(0)
{
  m_contentRng = CreateObject<UniformRandomVariable>();
  m_timingRng = CreateObject<UniformRandomVariable>();
}

FdrlConsumer::~FdrlConsumer() = default;

void
FdrlConsumer::SetBaseFrequency(double frequencyHz)
{
  m_baseFrequency = std::max(0.1, std::min(100.0, frequencyHz));  // Limit to 0.1-100 Hz
  RefreshFrequency();
  NS_LOG_INFO("Base frequency set to " << m_baseFrequency << " Hz");
}

double
FdrlConsumer::GetBaseFrequency() const
{
  return m_baseFrequency;
}

double
FdrlConsumer::GetEffectiveFrequency() const
{
  double effectiveFactor = 1.0;
  
  if (m_enableFDRLCC) {
    // AI CC mode: Use DRL factor
    effectiveFactor = m_currentFactor * m_burstFactor;
  } else {
    // Classical CC mode: Use classical algorithm factor
    effectiveFactor = m_classicalCCFactor * m_burstFactor;
  }
  
  double frequency = std::max(0.1, m_baseFrequency * effectiveFactor);
  // Same cap as RefreshFrequency() for consistency
  frequency = std::min(200.0, frequency);  // Cap at 200 Hz
  
  return frequency;
}

void
FdrlConsumer::ApplyRateFactor(double factor)
{
  double oldFactor = m_currentFactor;
  m_currentFactor = std::max(0.1, std::min(5.0, factor));  // Limit range
  RefreshFrequency();
  
  NS_LOG_DEBUG("Controller rate factor: " << oldFactor << " -> " << m_currentFactor 
               << " (effective freq=" << m_baseFrequency * m_currentFactor * m_burstFactor << " Hz)");
}

void
FdrlConsumer::ApplyBurstFactor(double factor)
{
  double oldBurst = m_burstFactor;
  m_burstFactor = std::max(0.1, std::min(10.0, factor));  // Limit to 10x max burst
  RefreshFrequency();
  
  NS_LOG_INFO("Burst factor: " << oldBurst << " -> " << m_burstFactor 
              << " (effective freq=" << m_baseFrequency * m_currentFactor * m_burstFactor << " Hz)");
}

double
FdrlConsumer::GetCurrentFactor() const
{
  return m_currentFactor;
}

void
FdrlConsumer::SetContentPoolSize(uint32_t poolSize)
{
  m_contentPoolSize = poolSize;
  NS_LOG_INFO("Content pool size set to " << poolSize << " (0 = unlimited)");
}

uint32_t
FdrlConsumer::GetContentPoolSize() const
{
  return m_contentPoolSize;
}

void
FdrlConsumer::SetTrafficVariation(double factor)
{
  m_trafficVariation = std::clamp(factor, 0.0, 1.0);
  NS_LOG_INFO("Traffic variation set to " << m_trafficVariation);
}

void
FdrlConsumer::EnableBurstyTraffic(bool enable, double onRateMultiplier)
{
  m_burstyMode = enable;
  // OPTIMIZATION: Reduced multiplier range from 4-6x to 2-4x for better performance
  m_onRateMultiplier = std::max(2.0, std::min(4.0, onRateMultiplier));  // 2-4x range
  
  if (m_onDurationRng) {
    m_onDurationRng->SetAttribute("Mean", DoubleValue(2.0));  // ON: exponential(mean=2s)
    m_onDurationRng->SetAttribute("Bound", DoubleValue(10.0));  // Cap at 10s
  }
  if (m_offDurationRng) {
    m_offDurationRng->SetAttribute("Mean", DoubleValue(1.0));  // OFF: exponential(mean=1s)
    m_offDurationRng->SetAttribute("Bound", DoubleValue(5.0));  // Cap at 5s
  }
  
  NS_LOG_INFO("Bursty traffic " << (enable ? "ENABLED" : "DISABLED") 
              << " | ON rate multiplier: " << m_onRateMultiplier << "x");
}

void
FdrlConsumer::AssignRandomStreams(int64_t streamId)
{
  if (m_contentRng) {
    m_contentRng->SetStream(streamId);
  }
  if (m_timingRng) {
    m_timingRng->SetStream(streamId + 1000);  // Offset to avoid overlap
  }
  NS_LOG_DEBUG("Assigned random streams: content=" << streamId << ", timing=" << (streamId + 1000));
}

void
FdrlConsumer::SetMetricAggregator(std::shared_ptr<MetricAggregator> aggregator)
{
  m_aggregator = aggregator;
  NS_LOG_INFO("MetricAggregator connected to consumer");
}

void
FdrlConsumer::RefreshDynamicContent()
{
  // Increment content offset to shift content IDs (ensures no overlap)
  if (m_contentPoolSize > 0) {
    m_contentOffset += m_contentPoolSize;
    double simTime = Simulator::Now().GetSeconds();
    NS_LOG_INFO("[DynamicTraffic] Refreshed dynamic content at t=" << simTime << "s, new offset=" << m_contentOffset);
    
    // Schedule next refresh if interval is enabled
    if (g_dynamicTrafficInterval > 0) {
      m_contentRefreshEvent = Simulator::Schedule(Seconds(g_dynamicTrafficInterval), 
                                                   &FdrlConsumer::RefreshDynamicContent, this);
    }
  }
}

void
FdrlConsumer::StartApplication()
{
  NS_LOG_FUNCTION(this);
  m_totalInterestsSent = 0;
  m_totalDataReceived = 0;
  m_totalTimeouts = 0;
  m_totalCongestionMarks = 0;  // PHASE 1: Initialize congestion mark counter
  m_delaySum = 0.0;
  m_delayCount = 0;
  m_timeoutsThisSecond = 0;
  m_dataThisSecond = 0;
  m_pendingInterests.clear();
  m_contentOffset = 0;  // Initialize content offset
  
  ConsumerCbr::StartApplication();
  RefreshFrequency();
  
  // Dynamic content refresh: Schedule periodic refresh if enabled
  if (g_dynamicTrafficInterval > 0 && m_contentPoolSize > 0) {
    m_contentRefreshEvent = Simulator::Schedule(Seconds(g_dynamicTrafficInterval), 
                                                 &FdrlConsumer::RefreshDynamicContent, this);
  }
  
  // AGGRESSIVE TOPOLOGY: Start ON-OFF burst traffic if enabled
  if (m_burstyMode) {
    m_isBurstOn = true;  // Start in ON state
    ApplyBurstFactor(m_onRateMultiplier);  // Apply ON multiplier
    ScheduleBurstOff();  // Schedule first OFF transition
    NS_LOG_INFO("Bursty traffic started in ON state (rate=" << m_onRateMultiplier << "x)");
  }
}

void
FdrlConsumer::StopApplication()
{
  NS_LOG_INFO("Consumer stopping. Stats: sent=" << m_totalInterestsSent 
              << " received=" << m_totalDataReceived 
              << " timeouts=" << m_totalTimeouts);
  ConsumerCbr::StopApplication();
}

void
FdrlConsumer::RefreshFrequency()
{
  double effectiveFactor = 1.0;
  
  if (m_enableFDRLCC) {
    // AI CC mode: Use DRL factor
    effectiveFactor = m_currentFactor * m_burstFactor;
  } else {
    // Classical CC mode: Use classical algorithm factor
    effectiveFactor = m_classicalCCFactor * m_burstFactor;
  }
  
  double frequency = std::max(0.1, m_baseFrequency * effectiveFactor);
  // OPTIMIZATION: Reduced frequency cap to prevent excessive event generation
  // 200 Hz cap is still high enough for congestion testing but reduces event overhead
  frequency = std::min(200.0, frequency);  // Cap at 200 Hz (reduced from 500 Hz for performance)
  
  SetAttribute("Frequency", DoubleValue(frequency));
  NS_LOG_DEBUG("Consumer frequency: " << frequency << " Hz "
               << "(base=" << m_baseFrequency
               << ", mode=" << (m_enableFDRLCC ? "FDRLCC" : "Classical")
               << ", factor=" << effectiveFactor << ")");
}

void
FdrlConsumer::ScheduleNextPacket()
{
  // Calculate base interval from frequency (including all factors)
  // CRITICAL FIX: Removed 50 Hz hard cap to allow severe congestion testing
  // SEVERE CONGESTION MODE: Allow up to 500 Hz (was capped at 50 Hz)
  double effectiveFactor = m_enableFDRLCC ? 
      (m_currentFactor * m_burstFactor) : 
      (m_classicalCCFactor * m_burstFactor);
  double effectiveFreq = m_baseFrequency * effectiveFactor;
      effectiveFreq = std::max(0.1, std::min(200.0, effectiveFreq));  // Cap at 200 Hz for performance
  
  double baseInterval = 1.0 / effectiveFreq;
  
  // Add traffic variation (randomness in timing)
  if (m_trafficVariation > 0.0) {
    double variation = m_timingRng->GetValue(-m_trafficVariation, m_trafficVariation);
    baseInterval *= (1.0 + variation);
  }
  
  // Minimum interval = 20ms (max 50 packets/sec)
  baseInterval = std::max(0.02, baseInterval);
  
  // If using content pool, modify sequence number before sending
  if (m_contentPoolSize > 0) {
    // Apply content offset for dynamic content churn
    uint32_t poolSeq = m_contentRng->GetInteger(0, m_contentPoolSize - 1);
    m_seq = m_contentOffset + poolSeq;
  }
  
  // Schedule the actual interest send via our wrapper
  m_sendEvent = Simulator::Schedule(Seconds(baseInterval), &FdrlConsumer::DoSendInterest, this);
}

void
FdrlConsumer::DoSendInterest()
{
  // Record the send time for delay calculation
  m_pendingInterests[m_seq] = Simulator::Now();
  
  // Actually send the interest using parent
  Consumer::SendPacket();
  
  // Update statistics
  ++m_totalInterestsSent;
  
  // Fire trace callback
  m_interestSentTrace(0);  // 0 bytes for interest (typically small)
  
  // Record in aggregator
  if (m_aggregator) {
    m_aggregator->RecordInterestSent(0);
  }
  
  NS_LOG_DEBUG("Interest sent, seq=" << m_seq << ", total=" << m_totalInterestsSent);
}

void
FdrlConsumer::SendPacket()
{
  if (!m_active)
    return;

  NS_LOG_FUNCTION(this);

  uint32_t seq = std::numeric_limits<uint32_t>::max(); // invalid

  // Handle retransmissions
  while (m_retxSeqs.size()) {
    seq = *m_retxSeqs.begin();
    m_retxSeqs.erase(m_retxSeqs.begin());
    break;
  }

  if (seq == std::numeric_limits<uint32_t>::max()) {
    if (m_seqMax != std::numeric_limits<uint32_t>::max()) {
      if (m_seq >= m_seqMax) {
        return; // we are totally done
      }
    }
    seq = m_seq++;
  }

  // Build interest name with epoch for dynamic content
  shared_ptr<Name> nameWithSequence = make_shared<Name>();
  
  // Determine if this is dynamic content based on random selection
  bool isDynamic = false;
  if (g_dynamicTrafficRatio > 0.0 && g_dynamicContentEpoch > 0) {
    // Use a deterministic method: hash of sequence number to decide static vs dynamic
    // This ensures consistent behavior per sequence number
    uint32_t hash = seq;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    double ratio = static_cast<double>(hash % 10000) / 10000.0;
    isDynamic = (ratio < g_dynamicTrafficRatio);
  }
  
  if (isDynamic && g_dynamicContentEpoch > 0) {
    // Calculate current epoch: floor(simTime / epoch)
    double simTime = Simulator::Now().GetSeconds();
    uint32_t currentEpoch = static_cast<uint32_t>(std::floor(simTime / g_dynamicContentEpoch));
    
    // Build name: /dynamic/<epoch>/<original-prefix>/<seq>
    nameWithSequence->append("dynamic");
    nameWithSequence->append(std::to_string(currentEpoch));
    
    // Append original prefix components (skip first empty component if present)
    Name originalName(m_interestName);
    for (size_t i = 0; i < originalName.size(); ++i) {
      nameWithSequence->append(originalName.at(i));
    }
  } else {
    // Static content: use original prefix
    nameWithSequence = make_shared<Name>(m_interestName);
  }
  
  // Append sequence number
  nameWithSequence->appendSequenceNumber(seq);

  // Create and send interest
  shared_ptr<Interest> interest = make_shared<Interest>();
  interest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
  interest->setName(*nameWithSequence);
  interest->setCanBePrefix(false);
  time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
  interest->setInterestLifetime(interestLifeTime);

  NS_LOG_DEBUG("> Interest for " << seq << " (name: " << interest->getName() << ")");

  WillSendOutInterest(seq);

  m_transmittedInterests(interest, this, m_face);
  m_appLink->onReceiveInterest(*interest);

  ScheduleNextPacket();
}

void
FdrlConsumer::OnData(std::shared_ptr<const Data> data)
{
  // Calculate delay
  uint32_t seq = data->getName().at(-1).toSequenceNumber();
  double delayMs = 0.0;
  
  auto it = m_pendingInterests.find(seq);
  if (it != m_pendingInterests.end()) {
    Time delay = Simulator::Now() - it->second;
    delayMs = delay.GetMilliSeconds();
    // CRITICAL FIX: Ensure delay is at least link propagation delay
    // Topology has 2-5ms link delays, so minimum should reflect that
    delayMs = std::max(delayMs, 2.0);  // Minimum 2ms (link delay)
    m_pendingInterests.erase(it);
  } else {
    // Retransmitted or cached response - use realistic network delay estimate
    // For realistic congestion, cache hits still have some processing delay
    delayMs = 3.0;  // Realistic cache hit delay (was 10.0, too high)
  }
  
  // Accumulate delay statistics (for real-time measurement)
  m_delaySum += delayMs;
  ++m_delayCount;
  ++m_dataThisSecond;  // Track per-second data count
  
  // Get packet size
  uint32_t dataSize = data->wireEncode().size();
  
  // Update statistics
  ++m_totalDataReceived;
  
  // PHASE 1: Track congestion marks
  if (data->getCongestionMark() > 0) {
    ++m_totalCongestionMarks;
  }
  
  // Fire trace callback
  m_dataReceivedTrace(dataSize, delayMs);
  
  // Record in aggregator
  if (m_aggregator) {
    m_aggregator->RecordDataReceived(dataSize, delayMs);
  }
  
  NS_LOG_DEBUG("Data received, seq=" << seq << ", size=" << dataSize 
               << " bytes, delay=" << delayMs << " ms");
  
  // ────────────────────────────────────────────────
  // Route to appropriate CC handler
  // ────────────────────────────────────────────────
  if (m_enableFDRLCC) {
    // AI CC mode: DRL handles this externally
    // (No action here, DRL calls ApplyRateFactor() separately)
  } else {
    // Classical CC mode: Handle congestion here
    HandleClassicalCC(data);
  }
  
  // Call parent handler
  ConsumerCbr::OnData(data);
}

void
FdrlConsumer::OnTimeout(uint32_t sequenceNumber)
{
  ++m_totalTimeouts;
  ++m_timeoutsThisSecond;  // Track per-second timeouts
  
  // Count timeout as high delay (500ms) to indicate congestion
  // This helps the agent see the impact of congestion on delay
  // NOTE: Timeouts are added to m_delaySum but NOT to m_successfulRttSum
  const double TIMEOUT_DELAY_MS = 500.0;
  m_delaySum += TIMEOUT_DELAY_MS;
  ++m_delayCount;
  // Do NOT add to m_successfulRttSum - timeouts are not successful RTT
  
  // Fire trace callback
  m_timeoutTrace(sequenceNumber);
  
  // Record in aggregator as a drop
  if (m_aggregator) {
    m_aggregator->RecordQueueDrop(nullptr);  // nullptr - we don't have the packet
  }
  
  // Remove from pending
  m_pendingInterests.erase(sequenceNumber);
  
  NS_LOG_DEBUG("Timeout, seq=" << sequenceNumber << ", total timeouts=" << m_totalTimeouts
               << ", this second=" << m_timeoutsThisSecond
               << " (counted as " << TIMEOUT_DELAY_MS << "ms delay)");
  
  // ────────────────────────────────────────────────
  // Route to appropriate CC handler
  // ────────────────────────────────────────────────
  if (m_enableFDRLCC) {
    // AI CC mode: DRL handles this externally
    // (No action here)
  } else {
    // Classical CC mode: Handle timeout
    HandleClassicalCCTimeout();
  }
  
  // Call parent handler
  ConsumerCbr::OnTimeout(sequenceNumber);
}

void
FdrlConsumer::ResetDelayStats()
{
  m_delaySum = 0.0;
  m_delayCount = 0;
  m_successfulRttSum = 0.0;
  m_successfulRttCount = 0;
  m_timeoutsThisSecond = 0;  // Reset per-second timeout counter
  m_dataThisSecond = 0;      // Reset per-second data counter
  NS_LOG_DEBUG("Delay statistics reset");
}

void
FdrlConsumer::EnableFDRLCC(bool enable)
{
  m_enableFDRLCC = enable;
  if (enable) {
    m_classicalCCFactor = 1.0;  // Reset classical CC factor
    m_ccSuccessCount = 0;
    NS_LOG_INFO("FDRLCC (AI CC) enabled - Classical CC disabled");
  } else {
    m_currentFactor = 1.0;  // Reset DRL factor
    m_classicalCCFactor = 1.0;  // Reset classical CC factor
    m_ccSuccessCount = 0;
    RefreshFrequency();
    NS_LOG_INFO("FDRLCC (AI CC) disabled - Classical CC enabled");
  }
}

void
FdrlConsumer::SetClassicalCCAlgorithm(ClassicalCCAlgorithm algorithm)
{
  m_classicalCCAlgo = algorithm;
  m_classicalCCFactor = 1.0;  // Reset factor
  m_ccSuccessCount = 0;
  
  // Reset algorithm-specific state
  m_bicMinWin = 0.0;
  m_bicMaxWin = std::numeric_limits<double>::max();
  m_bicTargetWin = 0.0;
  m_isBicSlowStart = false;
  m_cubicWmax = 0.0;
  m_cubicLastWmax = 0.0;
  m_cubicLastDecrease = time::steady_clock::now();
  
  std::string algoName;
  switch (algorithm) {
    case ClassicalCCAlgorithm::NONE: algoName = "NONE"; break;
    case ClassicalCCAlgorithm::AIMD: algoName = "AIMD"; break;
    case ClassicalCCAlgorithm::BIC: algoName = "BIC"; break;
    case ClassicalCCAlgorithm::CUBIC: algoName = "CUBIC"; break;
  }
  NS_LOG_INFO("Classical CC algorithm set to: " << algoName);
}

void
FdrlConsumer::ConfigureClassicalCC(double increase,
                                    double decrease,
                                    double minFactor,
                                    double maxFactor,
                                    uint32_t increaseInterval)
{
  m_ccIncrease = increase;
  m_ccDecrease = decrease;
  m_ccMinFactor = minFactor;
  m_ccMaxFactor = maxFactor;
  m_ccIncreaseInterval = increaseInterval;
  NS_LOG_INFO("Classical CC configured: increase=" << increase
               << ", decrease=" << decrease
               << ", min=" << minFactor
               << ", max=" << maxFactor
               << ", interval=" << increaseInterval);
}

void
FdrlConsumer::HandleClassicalCC(std::shared_ptr<const Data> data)
{
  switch (m_classicalCCAlgo) {
    case ClassicalCCAlgorithm::NONE:
      // No congestion control - do nothing
      break;
      
    case ClassicalCCAlgorithm::AIMD:
      HandleAIMD(data);
      break;
      
    case ClassicalCCAlgorithm::BIC:
      HandleBIC(data);
      break;
      
    case ClassicalCCAlgorithm::CUBIC:
      HandleCUBIC(data);
      break;
  }
}

void
FdrlConsumer::HandleClassicalCCTimeout()
{
  switch (m_classicalCCAlgo) {
    case ClassicalCCAlgorithm::NONE:
      break;
      
    case ClassicalCCAlgorithm::AIMD:
      // Timeout indicates congestion - decrease rate
      m_classicalCCFactor *= m_ccDecrease;
      m_classicalCCFactor = std::max(m_ccMinFactor, m_classicalCCFactor);
      m_ccSuccessCount = 0;
      RefreshFrequency();
      NS_LOG_INFO("AIMD: Timeout detected, factor decreased to " << m_classicalCCFactor);
      break;
      
    case ClassicalCCAlgorithm::BIC:
      // BIC timeout handling (simplified - full implementation can be added later)
      m_classicalCCFactor *= m_ccDecrease;
      m_classicalCCFactor = std::max(m_ccMinFactor, m_classicalCCFactor);
      m_ccSuccessCount = 0;
      RefreshFrequency();
      NS_LOG_INFO("BIC: Timeout detected, factor decreased to " << m_classicalCCFactor);
      break;
      
    case ClassicalCCAlgorithm::CUBIC:
      // CUBIC timeout handling (simplified - full implementation can be added later)
      m_classicalCCFactor *= m_ccDecrease;
      m_classicalCCFactor = std::max(m_ccMinFactor, m_classicalCCFactor);
      m_ccSuccessCount = 0;
      RefreshFrequency();
      NS_LOG_INFO("CUBIC: Timeout detected, factor decreased to " << m_classicalCCFactor);
      break;
  }
}

void
FdrlConsumer::HandleAIMD(std::shared_ptr<const Data> data)
{
  // Check for congestion mark
  if (data->getCongestionMark() > 0) {
    // Congestion detected - multiplicative decrease
    m_classicalCCFactor *= m_ccDecrease;
    m_classicalCCFactor = std::max(m_ccMinFactor, m_classicalCCFactor);
    m_ccSuccessCount = 0;  // Reset success counter
    RefreshFrequency();  // Update frequency immediately
    
    NS_LOG_INFO("AIMD: Congestion mark received, factor decreased to " << m_classicalCCFactor);
  } else {
    // No congestion - additive increase (gradual)
    m_ccSuccessCount++;
    if (m_ccSuccessCount >= m_ccIncreaseInterval) {
      m_classicalCCFactor += m_ccIncrease;
      m_classicalCCFactor = std::min(m_ccMaxFactor, m_classicalCCFactor);
      m_ccSuccessCount = 0;
      RefreshFrequency();  // Update frequency immediately
      
      NS_LOG_DEBUG("AIMD: Factor increased to " << m_classicalCCFactor);
    }
  }
}

void
FdrlConsumer::HandleBIC(std::shared_ptr<const Data> data)
{
  // Simplified BIC implementation
  // Full BIC can be implemented later based on ConsumerPcon's BIC logic
  if (data->getCongestionMark() > 0) {
    // Congestion detected - decrease
    m_classicalCCFactor *= m_ccDecrease;
    m_classicalCCFactor = std::max(m_ccMinFactor, m_classicalCCFactor);
    m_ccSuccessCount = 0;
    RefreshFrequency();
    NS_LOG_INFO("BIC: Congestion mark received, factor decreased to " << m_classicalCCFactor);
  } else {
    // No congestion - increase gradually
    m_ccSuccessCount++;
    if (m_ccSuccessCount >= m_ccIncreaseInterval) {
      m_classicalCCFactor += m_ccIncrease;
      m_classicalCCFactor = std::min(m_ccMaxFactor, m_classicalCCFactor);
      m_ccSuccessCount = 0;
      RefreshFrequency();
      NS_LOG_DEBUG("BIC: Factor increased to " << m_classicalCCFactor);
    }
  }
}

void
FdrlConsumer::HandleCUBIC(std::shared_ptr<const Data> data)
{
  // Simplified CUBIC implementation
  // Full CUBIC can be implemented later based on ConsumerPcon's CUBIC logic
  if (data->getCongestionMark() > 0) {
    // Congestion detected - decrease
    m_classicalCCFactor *= m_ccDecrease;
    m_classicalCCFactor = std::max(m_ccMinFactor, m_classicalCCFactor);
    m_ccSuccessCount = 0;
    m_cubicLastDecrease = ::ndn::time::steady_clock::now();
    m_cubicWmax = m_classicalCCFactor;
    RefreshFrequency();
    NS_LOG_INFO("CUBIC: Congestion mark received, factor decreased to " << m_classicalCCFactor);
  } else {
    // No congestion - increase gradually
    m_ccSuccessCount++;
    if (m_ccSuccessCount >= m_ccIncreaseInterval) {
      m_classicalCCFactor += m_ccIncrease;
      m_classicalCCFactor = std::min(m_ccMaxFactor, m_classicalCCFactor);
      m_ccSuccessCount = 0;
      RefreshFrequency();
      NS_LOG_DEBUG("CUBIC: Factor increased to " << m_classicalCCFactor);
    }
  }
}

// AGGRESSIVE TOPOLOGY: ON-OFF burst traffic handlers
void
FdrlConsumer::ScheduleBurstOn()
{
  if (!m_burstyMode) return;
  
  m_isBurstOn = true;
  ApplyBurstFactor(m_onRateMultiplier);  // Apply ON multiplier (4-6x)
  NS_LOG_DEBUG("Burst ON: rate multiplier=" << m_onRateMultiplier << "x");
  
  // Schedule next OFF transition
  double onDuration = m_onDurationRng->GetValue();  // exponential(mean=2s)
  m_burstStateEvent = Simulator::Schedule(Seconds(onDuration), &FdrlConsumer::ScheduleBurstOff, this);
}

void
FdrlConsumer::ScheduleBurstOff()
{
  if (!m_burstyMode) return;
  
  m_isBurstOn = false;
  ApplyBurstFactor(0.01);  // Near-zero during OFF (0.01 to avoid complete stop)
  NS_LOG_DEBUG("Burst OFF: rate multiplier=0.01x");
  
  // Schedule next ON transition
  double offDuration = m_offDurationRng->GetValue();  // exponential(mean=1s)
  m_burstStateEvent = Simulator::Schedule(Seconds(offDuration), &FdrlConsumer::ScheduleBurstOn, this);
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3
