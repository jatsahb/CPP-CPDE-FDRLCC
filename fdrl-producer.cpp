#include "fdrl-producer.hpp"

#include "ns3/log.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE("ndn.FdrlProducer");

namespace ns3 {
namespace ndn {
namespace fdrl {

// AGGRESSIVE TOPOLOGY: Global flag for delay spike (declared in fdrlcc_unified.cpp)
// Forward declaration - will be defined in fdrlcc_unified.cpp
extern bool g_producerDelaySpike;

NS_OBJECT_ENSURE_REGISTERED(FdrlProducer);

TypeId
FdrlProducer::GetTypeId()
{
  static TypeId tid =
    TypeId("ns3::ndn::fdrl::FdrlProducer").SetParent<Producer>().AddConstructor<FdrlProducer>();
  return tid;
}

FdrlProducer::FdrlProducer()
  : m_contentFreshness(Seconds(1.0))
{
  // Don't set attribute in constructor - object not fully initialized yet
  // Set it via SetContentFreshness() after object creation if needed
}

FdrlProducer::~FdrlProducer() = default;

void
FdrlProducer::SetContentFreshness(Time freshness)
{
  m_contentFreshness = freshness;
  SetAttribute("Freshness", TimeValue(freshness));
}

Time
FdrlProducer::GetContentFreshness() const
{
  return m_contentFreshness;
}

void
FdrlProducer::OnInterest(shared_ptr<const Interest> interest)
{
  NS_LOG_DEBUG("Serving Interest " << interest->getName());
  
  // AGGRESSIVE TOPOLOGY: Add delay spike if enabled
  if (g_producerDelaySpike) {
    // Schedule response with +50ms delay
    Simulator::Schedule(Seconds(0.05), [this, interest]() {
      Producer::OnInterest(interest);
    });
  } else {
    Producer::OnInterest(interest);
  }
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

