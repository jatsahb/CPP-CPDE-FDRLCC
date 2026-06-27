/**
 * fdrlcc-console-colors.hpp
 * 
 * Console color utilities for FDRLCC
 * Provides color-coded output for better readability
 */

#ifndef FDRLCC_CONSOLE_COLORS_HPP
#define FDRLCC_CONSOLE_COLORS_HPP

#include <string>
#include <iostream>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * ANSI color codes
 */
namespace Colors {
  // Reset
  const std::string RESET = "\033[0m";
  
  // Text colors
  const std::string BLACK = "\033[30m";
  const std::string RED = "\033[31m";
  const std::string GREEN = "\033[32m";
  const std::string YELLOW = "\033[33m";
  const std::string BLUE = "\033[34m";
  const std::string MAGENTA = "\033[35m";
  const std::string CYAN = "\033[36m";
  const std::string WHITE = "\033[37m";
  
  // Bright colors
  const std::string BRIGHT_RED = "\033[91m";
  const std::string BRIGHT_GREEN = "\033[92m";
  const std::string BRIGHT_YELLOW = "\033[93m";
  const std::string BRIGHT_BLUE = "\033[94m";
  const std::string BRIGHT_MAGENTA = "\033[95m";
  const std::string BRIGHT_CYAN = "\033[96m";
  
  // Background colors
  const std::string BG_RED = "\033[41m";
  const std::string BG_GREEN = "\033[42m";
  const std::string BG_YELLOW = "\033[43m";
}

/**
 * Color-enabled output control
 */
class ColorOutput {
public:
  /**
   * Check if terminal supports colors
   */
  static bool IsColorEnabled();
  
  /**
   * Enable/disable color output
   */
  static void SetEnabled(bool enabled);
  
  /**
   * Get color code (returns empty string if disabled)
   */
  static std::string GetColor(const std::string& colorCode);
  
  /**
   * Reset color
   */
  static std::string Reset();
  
  // Predefined color schemes
  static std::string Success();    // Green
  static std::string Error();      // Red
  static std::string Warning();    // Yellow/Orange
  static std::string Info();       // Cyan
  static std::string Normal();     // White (default)

private:
  static bool s_colorEnabled;
  static bool s_initialized;
  static void Initialize();
};

/**
 * Convenience functions for colored output
 */
std::string ColorSuccess(const std::string& text);
std::string ColorError(const std::string& text);
std::string ColorWarning(const std::string& text);
std::string ColorInfo(const std::string& text);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // FDRLCC_CONSOLE_COLORS_HPP

