#include "fdrl-metric-trace-helper.hpp"
// REFACTORED: MetricStore deleted - this helper uses Controller which may not be used in unified codebase
// For legacy Controller, it should use MetricEngine, not MetricStore
#include "src_cpp/metrics/metric-engine.hpp"
#include "ns3/log.h"
#include "ns3/simulator.h"

NS_LOG_COMPONENT_DEFINE("ndn.FdrlMetricTraceHelper");

namespace ns3 {
namespace ndn {
namespace fdrl {

FdrlMetricTraceHelper::FdrlMetricTraceHelper()
  : m_interval(Seconds(0.5))
{
}

void
FdrlMetricTraceHelper::SetInterval(Time interval)
{
  m_interval = interval;
}

void
FdrlMetricTraceHelper::SetCallback(std::function<void(const MetricSnapshot&)> cb)
{
  m_callback = std::move(cb);
}

void
FdrlMetricTraceHelper::Attach(const Ptr<Controller>& controller)
{
  m_controller = controller;
  ScheduleNext();
}

void
FdrlMetricTraceHelper::Detach()
{
  if (m_event.IsRunning()) {
    m_event.Cancel();
  }
  m_controller = nullptr;
}

void
FdrlMetricTraceHelper::ScheduleNext()
{
  if (!m_controller || m_interval.IsZero()) {
    return;
  }
  m_event = Simulator::Schedule(m_interval, &FdrlMetricTraceHelper::Emit, this);
}

void
FdrlMetricTraceHelper::Emit()
{
  if (!m_controller) {
    return;
  }

  // REFACTORED: MetricStore deleted - Controller needs to be updated to use MetricEngine
  // For now, stub this out since Controller may not be used in unified codebase
  // If Controller is used, it should provide GetMetricEngine() instead of GetMetricStore()
  // auto metricEngine = m_controller->GetMetricEngine();  // TODO: Update Controller
  // if (m_callback && metricEngine) {
  //   MetricSnapshot snapshot = metricEngine->GetLatestSnapshot("default");  // Need region
  //   m_callback(snapshot);
  // }
  
  NS_LOG_WARN("FdrlMetricTraceHelper::Emit() - MetricStore deleted, needs Controller update for MetricEngine");
  // For now, just schedule next without calling callback (prevents crash)
  // TODO: Update Controller to use MetricEngine if this helper is needed

  ScheduleNext();
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

