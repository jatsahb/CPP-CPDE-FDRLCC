/**
 * action-executor.cpp
 * 
 * Action executor implementation for FDRLCC
 */

#include "action-executor.hpp"
#include "../apps/fdrl-consumer.hpp"
#include "../simulation/fdrlcc-types.hpp"
#include "ns3/log.h"
#include <cmath>
#include <algorithm>

NS_LOG_COMPONENT_DEFINE("FdrlccActionExecutor");

namespace ns3 {
namespace ndn {
namespace fdrl {

bool
ActionExecutor::ValidateAction(double rateFactor)
{
  // Check for NaN or Inf
  if (!std::isfinite(rateFactor)) {
    NS_LOG_WARN("ActionExecutor: Invalid action (NaN/Inf): " << rateFactor);
    return false;
  }
  
  // Check bounds
  if (rateFactor < MIN_ACTION || rateFactor > MAX_ACTION) {
    NS_LOG_WARN("ActionExecutor: Action out of bounds: " << rateFactor
                << " (valid range: [" << MIN_ACTION << ", " << MAX_ACTION << "])");
    return false;
  }
  
  return true;
}

std::vector<ActionExecutionReport>
ActionExecutor::ApplyActionToRegion(const std::string& region, double rateFactor)
{
  std::vector<ActionExecutionReport> reports;
  
  // Find all consumers in this region
  for (auto& consumer : g_consumers) {
    if (!consumer) continue;
    
    // Check if consumer belongs to this region
    if (g_consumerRegions.find(consumer) != g_consumerRegions.end() &&
        g_consumerRegions[consumer] == region) {
      ActionExecutionReport report = ApplyAction(consumer, rateFactor);
      reports.push_back(report);
    }
  }
  
  if (reports.empty()) {
    NS_LOG_WARN("ActionExecutor: No consumers found for region: " << region);
    ActionExecutionReport emptyReport;
    emptyReport.success = false;
    emptyReport.errorMessage = "No consumers found in region";
    emptyReport.requestedRateFactor = rateFactor;
    reports.push_back(emptyReport);
  }
  
  return reports;
}

ActionExecutionReport
ActionExecutor::ApplyAction(Ptr<FdrlConsumer> consumer, double rateFactor)
{
  ActionExecutionReport report;
  report.requestedRateFactor = rateFactor;
  
  if (!consumer) {
    report.success = false;
    report.errorMessage = "Consumer is null";
    return report;
  }
  
  // Get current state before applying
  report.baseFrequency = consumer->GetBaseFrequency();
  report.effectiveFrequencyBefore = consumer->GetEffectiveFrequency();
  
  // Validate action
  if (!ValidateAction(rateFactor)) {
    report.success = false;
    report.errorMessage = "Action validation failed";
    report.appliedRateFactor = consumer->GetCurrentFactor(); // Keep current
    report.effectiveFrequencyAfter = report.effectiveFrequencyBefore;
    return report;
  }
  
  // Apply action
  consumer->ApplyRateFactor(rateFactor);
  
  // Get state after applying
  report.appliedRateFactor = consumer->GetCurrentFactor();
  report.effectiveFrequencyAfter = consumer->GetEffectiveFrequency();
  report.success = true;
  
  NS_LOG_DEBUG("ActionExecutor: Applied rateFactor=" << rateFactor
              << " to consumer (baseFreq=" << report.baseFrequency
              << " Hz, effectiveFreq=" << report.effectiveFrequencyAfter << " Hz)");
  
  return report;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

