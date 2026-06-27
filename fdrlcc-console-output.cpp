/**
 * fdrlcc-console-output.cpp
 */

#include "fdrlcc-console-output.hpp"
#include "fdrlcc-types.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace ns3 {
namespace ndn {
namespace fdrl {

ConsoleMode ConsoleOutput::s_mode = ConsoleMode::Compact;
bool ConsoleOutput::s_lineActive = false;
double ConsoleOutput::s_lastDivergence = -1.0;
double ConsoleOutput::s_lastGlobalReward = -1.0;
bool ConsoleOutput::s_haveFlBaseline = false;

namespace {

constexpr size_t kProgressLineWidth = 110;

std::string
PadLine(const std::string& line)
{
  if (line.size() >= kProgressLineWidth) {
    return line.substr(0, kProgressLineWidth);
  }
  return line + std::string(kProgressLineWidth - line.size(), ' ');
}

} // namespace

void
ConsoleOutput::SetMode(ConsoleMode mode)
{
  s_mode = mode;
}

void
ConsoleOutput::SetModeFromString(const std::string& mode)
{
  std::string lower = mode;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "verbose" || lower == "full") {
    SetMode(ConsoleMode::Verbose);
  } else if (lower == "quiet" || lower == "silent" || lower == "none") {
    SetMode(ConsoleMode::Quiet);
  } else {
    SetMode(ConsoleMode::Compact);
  }
}

ConsoleMode
ConsoleOutput::GetMode()
{
  return s_mode;
}

bool
ConsoleOutput::IsCompact()
{
  return s_mode == ConsoleMode::Compact;
}

bool
ConsoleOutput::IsVerbose()
{
  return s_mode == ConsoleMode::Verbose;
}

bool
ConsoleOutput::IsQuiet()
{
  return s_mode == ConsoleMode::Quiet;
}

void
ConsoleOutput::PrintStartupBanner(uint32_t scenario,
                                  double simTimeSec,
                                  uint32_t seed,
                                  uint32_t runNumber,
                                  const std::string& algorithm,
                                  const std::string& resultsDir,
                                  bool flDisabled)
{
  if (IsQuiet()) {
    return;
  }

  std::cout << "FDRLCC | scenario=" << scenario << " sim=" << std::fixed << std::setprecision(0)
            << simTimeSec << "s seed=" << seed << " run=" << runNumber << " algo=" << algorithm;
  if (flDisabled) {
    std::cout << " | FL=OFF (isolated DRL)";
  } else {
    std::cout << " | FL every " << g_flInterval << "s | PW-FedAvg+AFM";
  }
  std::cout << " | console=";
  switch (s_mode) {
  case ConsoleMode::Compact:
    std::cout << "compact";
    break;
  case ConsoleMode::Verbose:
    std::cout << "verbose";
    break;
  case ConsoleMode::Quiet:
    std::cout << "quiet";
    break;
  }
  std::cout << std::endl;
  std::cout << "log: " << resultsDir << "/console_output.log" << std::endl;
  if (IsCompact()) {
    std::cout << std::string(80, '-') << std::endl;
    EmitProgressLine(PadLine("FDRLCC | [   0s / " + std::to_string(static_cast<int>(simTimeSec)) +
                             "s   0% ] warming up ..."),
                     false);
    s_lineActive = true;
  }
}

// ASCII trend markers for terminal
static std::string
FormatTrend(double current, double previous, bool lowerIsBetter, bool haveBaseline)
{
  if (!haveBaseline) {
    return "~";
  }
  const double relEps = 1e-4 * std::max(1.0, std::abs(previous));
  if (std::abs(current - previous) <= relEps) {
    return "~";
  }
  if (lowerIsBetter) {
    return (current < previous) ? "v" : "^";
  }
  return (current > previous) ? "^" : "v";
}

void
ConsoleOutput::CollectSnapshot(double& minIsrPercent,
                               double& maxLossPercent,
                               double& meanRateFactor,
                               uint64_t& totalTrainingSteps)
{
  minIsrPercent = 100.0;
  maxLossPercent = 0.0;
  meanRateFactor = 0.0;
  totalTrainingSteps = 0;
  size_t n = 0;

  for (const auto& [region, drl] : g_regionDRL) {
    (void)region;
    meanRateFactor += drl.rateFactor;
    totalTrainingSteps += drl.trainingStep;
    ++n;

    if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(drl.regionId)) {
      continue;
    }
    const MetricSnapshot& snap = g_metricEngine->GetLatestSnapshot(drl.regionId);
    double isr = 100.0;
    if (snap.totalInterestsSent > 0) {
      isr = 100.0 * static_cast<double>(snap.totalDataReceived) /
            static_cast<double>(snap.totalInterestsSent);
    }
    minIsrPercent = std::min(minIsrPercent, isr);

    double loss = 0.0;
    if (snap.totalInterestsSent > 0) {
      loss = 100.0 * static_cast<double>(snap.totalPacketsDropped) /
             static_cast<double>(snap.totalInterestsSent);
    }
    maxLossPercent = std::max(maxLossPercent, loss);
  }

  if (n > 0) {
    meanRateFactor /= static_cast<double>(n);
  }
  if (g_regionDRL.empty()) {
    minIsrPercent = 0.0;
  }
}

void
ConsoleOutput::CheckRegionImbalance(double simTimeSec)
{
  if (!g_metricEngine || simTimeSec < 10.0) {
    return;
  }

  uint64_t minInterests = std::numeric_limits<uint64_t>::max();
  uint64_t maxInterests = 0;
  std::string minRegion;
  std::string maxRegion;

  for (const auto& [region, drl] : g_regionDRL) {
    (void)drl;
    if (!g_metricEngine->IsRegionInitialized(region)) {
      continue;
    }
    const MetricSnapshot& snap = g_metricEngine->GetLatestSnapshot(region);
    if (snap.totalInterestsSent < minInterests) {
      minInterests = snap.totalInterestsSent;
      minRegion = region;
    }
    if (snap.totalInterestsSent > maxInterests) {
      maxInterests = snap.totalInterestsSent;
      maxRegion = region;
    }
  }

  if (minInterests == 0 || maxInterests == 0 || minRegion.empty() || maxRegion.empty()) {
    return;
  }

  const double ratio = static_cast<double>(maxInterests) / static_cast<double>(minInterests);
  if (ratio >= 3.0) {
    std::ostringstream warn;
    warn << "FDRLCC | t=" << std::fixed << std::setprecision(0) << simTimeSec
         << "s region imbalance: " << maxRegion << "=" << maxInterests << " vs " << minRegion
         << "=" << minInterests << " interests (ratio " << std::setprecision(1) << ratio
         << ") — check seed/checkpoint";
    PrintWarning(warn.str());
  }
}

void
ConsoleOutput::EmitProgressLine(const std::string& line, bool useCarriageReturn)
{
  if (IsQuiet()) {
    return;
  }
  if (useCarriageReturn) {
    std::cout << '\r' << PadLine(line) << std::flush;
  } else {
    std::cout << PadLine(line) << std::endl;
  }
}

void
ConsoleOutput::UpdateOnFLRound(double simTimeSec,
                               double simTotalSec,
                               uint32_t flRound,
                               double divergence,
                               double globalReward,
                               double fairnessIndex)
{
  (void)fairnessIndex;
  if (!IsCompact()) {
    return;
  }

  CheckRegionImbalance(simTimeSec);

  double minIsr = 0.0;
  double maxLoss = 0.0;
  double meanRate = 1.0;
  uint64_t trainSteps = 0;
  CollectSnapshot(minIsr, maxLoss, meanRate, trainSteps);

  int pct = 0;
  if (simTotalSec > 0.0) {
    pct = static_cast<int>(std::min(100.0, 100.0 * simTimeSec / simTotalSec));
  }

  std::ostringstream line;
  line << "FDRLCC | [" << std::setw(4) << static_cast<int>(simTimeSec) << "s / "
       << static_cast<int>(simTotalSec) << "s " << std::setw(3) << pct << "%] FL R"
       << std::setw(2) << flRound << " | div " << std::fixed << std::setprecision(4) << divergence
       << FormatTrend(divergence, s_lastDivergence, true, s_haveFlBaseline) << " | reward "
       << std::setprecision(3) << globalReward
       << FormatTrend(globalReward, s_lastGlobalReward, false, s_haveFlBaseline) << " | ISR min "
       << std::setprecision(2) << minIsr << "% | loss " << std::setprecision(2) << maxLoss
       << "% | rate x" << std::setprecision(2) << meanRate << " | steps " << trainSteps;

  EmitProgressLine(line.str(), true);
  s_lineActive = true;

  s_lastDivergence = divergence;
  s_lastGlobalReward = globalReward;
  s_haveFlBaseline = true;
}

void
ConsoleOutput::PrintWarning(const std::string& message)
{
  if (IsQuiet()) {
    return;
  }
  if (s_lineActive && IsCompact()) {
    std::cout << std::endl;
  }
  std::cout << "! " << message << std::endl;
  if (IsCompact()) {
    s_lineActive = true;
  }
}

void
ConsoleOutput::PrintSimulationComplete(double simTimeSec,
                                       double simTotalSec,
                                       uint32_t flRound,
                                       double divergence,
                                       double globalReward,
                                       const std::string& resultsDir)
{
  if (IsQuiet()) {
    std::cout << "FDRLCC complete. Results: " << resultsDir << std::endl;
    return;
  }

  if (IsCompact()) {
    if (s_lineActive) {
      std::cout << std::endl;
      s_lineActive = false;
    }

    double minIsr = 0.0;
    double maxLoss = 0.0;
    double meanRate = 1.0;
    uint64_t trainSteps = 0;
    CollectSnapshot(minIsr, maxLoss, meanRate, trainSteps);

    std::ostringstream isrDetail;
    isrDetail << std::fixed << std::setprecision(2);
    bool first = true;
    for (const auto& [region, drl] : g_regionDRL) {
      (void)drl;
      if (!g_metricEngine || !g_metricEngine->IsRegionInitialized(region)) {
        continue;
      }
      const MetricSnapshot& snap = g_metricEngine->GetLatestSnapshot(region);
      double isr = 100.0;
      if (snap.totalInterestsSent > 0) {
        isr = 100.0 * static_cast<double>(snap.totalDataReceived) /
              static_cast<double>(snap.totalInterestsSent);
      }
      if (!first) {
        isrDetail << " ";
      }
      isrDetail << region << "=" << isr;
      first = false;
    }

    std::cout << "FDRLCC | [" << static_cast<int>(simTimeSec) << "s / "
              << static_cast<int>(simTotalSec) << "s 100%] DONE | FL R" << flRound << " | div "
              << std::setprecision(4) << divergence << " | reward " << std::setprecision(3)
              << globalReward << " | ISR " << isrDetail.str() << " | loss " << std::setprecision(2)
              << maxLoss << "% | steps " << trainSteps << std::endl;
    std::cout << "Results: " << resultsDir << std::endl;
    return;
  }

  std::cout << "FDRLCC simulation complete. Results: " << resultsDir << std::endl;
}

void
ConsoleOutput::Cleanup()
{
  s_lineActive = false;
  s_lastDivergence = -1.0;
  s_lastGlobalReward = -1.0;
  s_haveFlBaseline = false;
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3
