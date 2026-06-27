/**
 * fdrlcc-console-colors.cpp
 * 
 * Console color utilities implementation for FDRLCC
 */

#include "fdrlcc-console-colors.hpp"
#include <cstdlib>
#include <unistd.h>

namespace ns3 {
namespace ndn {
namespace fdrl {

// Static member initialization
bool ColorOutput::s_colorEnabled = true;
bool ColorOutput::s_initialized = false;

void
ColorOutput::Initialize()
{
  if (s_initialized) return;
  
  // Check if output is to terminal and supports colors
  // Check TERM environment variable and isatty
  const char* term = std::getenv("TERM");
  bool isTerminal = isatty(STDOUT_FILENO) != 0;
  bool termSupportsColor = (term != nullptr) && 
                           (std::string(term) != "dumb") &&
                           (std::string(term).find("color") != std::string::npos ||
                            std::string(term).find("xterm") != std::string::npos ||
                            std::string(term).find("screen") != std::string::npos ||
                            std::string(term).find("tmux") != std::string::npos);
  
  // Also check NO_COLOR environment variable (standard)
  const char* noColor = std::getenv("NO_COLOR");
  if (noColor != nullptr && noColor[0] != '\0') {
    s_colorEnabled = false;
  } else {
    s_colorEnabled = isTerminal && termSupportsColor;
  }
  
  s_initialized = true;
}

bool
ColorOutput::IsColorEnabled()
{
  Initialize();
  return s_colorEnabled;
}

void
ColorOutput::SetEnabled(bool enabled)
{
  Initialize();
  s_colorEnabled = enabled;
}

std::string
ColorOutput::GetColor(const std::string& colorCode)
{
  if (!IsColorEnabled()) {
    return "";
  }
  return colorCode;
}

std::string
ColorOutput::Reset()
{
  return GetColor(Colors::RESET);
}

std::string
ColorOutput::Success()
{
  return GetColor(Colors::BRIGHT_GREEN);
}

std::string
ColorOutput::Error()
{
  return GetColor(Colors::BRIGHT_RED);
}

std::string
ColorOutput::Warning()
{
  return GetColor(Colors::BRIGHT_YELLOW);
}

std::string
ColorOutput::Info()
{
  return GetColor(Colors::BRIGHT_CYAN);
}

std::string
ColorOutput::Normal()
{
  return GetColor(Colors::WHITE);
}

// Convenience functions
std::string
ColorSuccess(const std::string& text)
{
  return ColorOutput::Success() + text + ColorOutput::Reset();
}

std::string
ColorError(const std::string& text)
{
  return ColorOutput::Error() + text + ColorOutput::Reset();
}

std::string
ColorWarning(const std::string& text)
{
  return ColorOutput::Warning() + text + ColorOutput::Reset();
}

std::string
ColorInfo(const std::string& text)
{
  return ColorOutput::Info() + text + ColorOutput::Reset();
}

} // namespace fdrl
} // namespace ndn
} // namespace ns3

