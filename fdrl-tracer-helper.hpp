#ifndef FDRLCC_HELPERS_FDRL_TRACER_HELPER_HPP_
#define FDRLCC_HELPERS_FDRL_TRACER_HELPER_HPP_

#include "ns3/ptr.h"
#include "ns3/nstime.h"
#include "ns3/node-container.h"
#include <string>
#include <vector>

namespace ns3 {
class Node;

namespace ndn {
class AppDelayTracer;
class L3RateTracer;
class CsTracer;

namespace fdrl {
class MetricEngine;  // REFACTORED: MetricStore deleted, using MetricEngine

/**
 * \brief Helper class for setting up ndnSIM tracers.
 * REFACTORED: MetricStore deleted - tracers are now standalone (MetricEngine handles delays internally).
 */
class FdrlTracerHelper
{
public:
  FdrlTracerHelper();
  ~FdrlTracerHelper();

  // AppDelayTracer setup
  void InstallAppDelayTracer(const NodeContainer& nodes, const std::string& filename);
  void InstallAppDelayTracer(Ptr<Node> node, const std::string& filename);
  // REFACTORED: MetricStore deleted - MetricEngine handles delays internally, no manual connection needed
  // void ConnectAppDelayTracerToMetricStore(Ptr<MetricStore> metricStore);  // DEPRECATED

  // L3RateTracer setup
  void InstallL3RateTracer(const NodeContainer& nodes, const std::string& filename);
  void InstallL3RateTracer(Ptr<Node> node, const std::string& filename);

  // CsTracer setup
  void InstallCsTracer(const NodeContainer& nodes, const std::string& filename);
  void InstallCsTracer(Ptr<Node> node, const std::string& filename);

  // REFACTORED: MetricStore deleted - MetricEngine handles RTT tracking internally
  // void EnableRttTracking(Ptr<MetricStore> metricStore);  // DEPRECATED

private:
  // REFACTORED: No longer needed - MetricEngine handles delays internally
  // void OnAppDelay(Ptr<MetricStore> metricStore, Time delay);  // DEPRECATED
  // Note: Tracers are managed by ndnSIM's Install() methods, we don't need to store them
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_HELPERS_FDRL_TRACER_HELPER_HPP_

