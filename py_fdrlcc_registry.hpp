#ifndef PY_FDRLCC_REGISTRY_HPP
#define PY_FDRLCC_REGISTRY_HPP

#include "controller/fdrl-controller.hpp"
#include "ns3/ptr.h"
#include <cstdint>

namespace ns3 {
namespace ndn {
namespace fdrl {

/**
 * Register a controller instance for Python access.
 * This allows Python bindings to access controllers even when
 * NodeList access fails (e.g., in separate processes).
 */
void RegisterControllerForPython(uint32_t nodeId, Ptr<Controller> controller);

/**
 * Get controller from registry or NodeList.
 */
Ptr<Controller> GetControllerForPython(uint32_t nodeId);

} // namespace fdrl
} // namespace ndn
} // namespace ns3

#endif // PY_FDRLCC_REGISTRY_HPP

