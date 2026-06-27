/**
 * fdrlcc-console-output.hpp
 *
 * Compact / verbose / quiet terminal progress for FDRLCC runs.
 */

#ifndef FDRLCC_CONSOLE_OUTPUT_HPP
#define FDRLCC_CONSOLE_OUTPUT_HPP

#include <cstdint>
#include <string>

namespace ns3 {
namespace ndn {
namespace fdrl {

enum class ConsoleMode {
  Compact,  // Single updating progress line (default)
  Verbose,  // Legacy boxes + event logger
  Quiet     // No stdout; details in console_output.log
};

class ConsoleOutput
{
public:
  static void SetMode(ConsoleMode mode);
  static void SetModeFromString(const std::string& mode);
  static ConsoleMode GetMode();

  static bool IsCompact();
  static bool IsVerbose();
  static bool IsQuiet();

  static void PrintStartupBanner(uint32_t scenario,
                                 double simTimeSec,
                                 uint32_t seed,
                                 uint32_t runNumber,
                                 const std::string& algorithm,
                                 const std::string& resultsDir,
                                 bool flDisabled);

  /** Call once per completed FL round (compact/quiet log only). */
  static void UpdateOnFLRound(double simTimeSec,
                              double simTotalSec,
                              uint32_t flRound,
                              double divergence,
                              double globalReward,
                              double fairnessIndex);

  static void PrintSimulationComplete(double simTimeSec,
                                      double simTotalSec,
                                      uint32_t flRound,
                                      double divergence,
                                      double globalReward,
                                      const std::string& resultsDir);

  /** Printed in compact mode (and verbose); suppressed in quiet. */
  static void PrintWarning(const std::string& message);

  static void Cleanup();

private:
  static ConsoleMode s_mode;
  static bool s_lineActive;
  static double s_lastDivergence;
  static double s_lastGlobalReward;
  static bool s_haveFlBaseline;

  static void EmitProgressLine(const std::string& line, bool useCarriageReturn);
  static void CollectSnapshot(double& minIsrPercent,
                              double& maxLossPercent,
                              double& meanRateFactor,
                              uint64_t& totalTrainingSteps);
  static void CheckRegionImbalance(double simTimeSec);
};

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CONSOLE_OUTPUT_HPP
