#ifndef FDRLCC_HELPERS_FDRL_METRIC_TRACE_HELPER_HPP_
#define FDRLCC_HELPERS_FDRL_METRIC_TRACE_HELPER_HPP_

#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include <functional>

#include "../controller/fdrl-controller.hpp"

namespace ns3 {
namespace ndn {
namespace fdrl {

class FdrlMetricTraceHelper
{
public:
  FdrlMetricTraceHelper();

  void SetInterval(Time interval);
  void SetCallback(std::function<void(const MetricSnapshot&)> cb);

  void Attach(const Ptr<Controller>& controller);
  void Detach();

private:
  void ScheduleNext();
  void Emit();

private:
  Time m_interval;
  std::function<void(const MetricSnapshot&)> m_callback;
  Ptr<Controller> m_controller;
  EventId m_event;
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_HELPERS_FDRL_METRIC_TRACE_HELPER_HPP_

