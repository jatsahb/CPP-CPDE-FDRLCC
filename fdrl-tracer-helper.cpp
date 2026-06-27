#include "fdrl-tracer-helper.hpp"

// REFACTORED: MetricStore deleted - using MetricEngine instead
// This helper is for legacy Controller which may not be used in unified codebase
// If Controller is used, it should use MetricEngine, not MetricStore
#include "src_cpp/metrics/metric-engine.hpp"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/config.h"
#include "ns3/callback.h"
#include "ns3/simulator.h"
#include "ns3/node-list.h"
#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/ndnSIM/apps/ndn-consumer.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-l3-rate-tracer.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-cs-tracer.hpp"

NS_LOG_COMPONENT_DEFINE("ndn.FdrlTracerHelper");

namespace ns3 {
namespace ndn {
namespace fdrl {

// REFACTORED: MetricStore deleted - this helper needs to be updated for MetricEngine
// For now, stub it out since it's only used with legacy Controller
static Ptr<MetricEngine> g_metricEngine = nullptr;

FdrlTracerHelper::FdrlTracerHelper()
{
}

FdrlTracerHelper::~FdrlTracerHelper()
{
  // Tracers are managed by ndnSIM, no cleanup needed
  g_metricEngine = nullptr;
}

void
FdrlTracerHelper::InstallAppDelayTracer(const NodeContainer& nodes, const std::string& filename)
{
  NS_LOG_INFO("Installing AppDelayTracer on " << nodes.GetN() << " nodes, output: " << filename);
  AppDelayTracer::Install(nodes, filename);
  NS_LOG_INFO("AppDelayTracer installed successfully");
}

void
FdrlTracerHelper::InstallAppDelayTracer(Ptr<Node> node, const std::string& filename)
{
  NS_LOG_INFO("Installing AppDelayTracer on node " << node->GetId() << ", output: " << filename);
  AppDelayTracer::Install(node, filename);
  NS_LOG_INFO("AppDelayTracer installed successfully");
}

// Static callback for LastRetransmittedInterestDataDelay
// Signature: void(Ptr<App>, uint32_t seqno, Time delay, int32_t hopCount)
static void OnLastRetransmittedDelay(Ptr<ndn::App> app, uint32_t seqno, Time delay, int32_t hopCount)
{
  NS_UNUSED(app);
  NS_UNUSED(seqno);
  NS_UNUSED(hopCount);
  // REFACTORED: MetricStore deleted - MetricEngine handles delay collection internally
  // This callback is no longer needed as MetricEngine collects delays from consumers directly
  NS_LOG_DEBUG("RTT recorded (LastRetransmitted): " << delay.GetMilliSeconds() << " ms");
}

// Static callback for FirstInterestDataDelay
// Signature: void(Ptr<App>, uint32_t seqno, Time delay, uint32_t retxCount, int32_t hopCount)
static void OnFirstInterestDelay(Ptr<ndn::App> app, uint32_t seqno, Time delay, uint32_t retxCount, int32_t hopCount)
{
  NS_UNUSED(app);
  NS_UNUSED(seqno);
  NS_UNUSED(retxCount);
  NS_UNUSED(hopCount);
  // REFACTORED: MetricStore deleted - MetricEngine handles delay collection internally
  // This callback is no longer needed as MetricEngine collects delays from consumers directly
  NS_LOG_DEBUG("RTT recorded (FirstInterest): " << delay.GetMilliSeconds() << " ms");
}

// REFACTORED: MetricStore deleted - these functions are deprecated and removed
// MetricEngine handles delay collection internally, no manual connection needed
// DEPRECATED: Implementations removed - functions commented out in header

void
FdrlTracerHelper::InstallL3RateTracer(const NodeContainer& nodes, const std::string& filename)
{
  NS_LOG_INFO("Installing L3RateTracer on " << nodes.GetN() << " nodes, output: " << filename);
  L3RateTracer::Install(nodes, filename);
  NS_LOG_INFO("L3RateTracer installed successfully");
}

void
FdrlTracerHelper::InstallL3RateTracer(Ptr<Node> node, const std::string& filename)
{
  NS_LOG_INFO("Installing L3RateTracer on node " << node->GetId() << ", output: " << filename);
  L3RateTracer::Install(node, filename);
  NS_LOG_INFO("L3RateTracer installed successfully");
}

void
FdrlTracerHelper::InstallCsTracer(const NodeContainer& nodes, const std::string& filename)
{
  NS_LOG_INFO("Installing CsTracer on " << nodes.GetN() << " nodes, output: " << filename);
  CsTracer::Install(nodes, filename, Seconds(1.0));  // Default sampling interval
  NS_LOG_INFO("CsTracer installed successfully");
}

void
FdrlTracerHelper::InstallCsTracer(Ptr<Node> node, const std::string& filename)
{
  NS_LOG_INFO("Installing CsTracer on node " << node->GetId() << ", output: " << filename);
  CsTracer::Install(node, filename, Seconds(1.0));  // Default sampling interval
  NS_LOG_INFO("CsTracer installed successfully");
}

// REFACTORED: MetricStore deleted - this function is deprecated and removed
// MetricEngine handles RTT tracking internally
// DEPRECATED: Implementation removed - function commented out in header

} // namespace fdrl
} // namespace ndn
} // namespace ns3

