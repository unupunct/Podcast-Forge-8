#pragma once
// Live enumeration of every Windows audio endpoint (all states) via MMDevice, IDeviceTopology and
// SetupAPI. The calling thread must have initialised COM. Never throws; missing properties stay empty.
#include <optional>
#include <string>
#include <vector>

#include "devices/DeviceInfo.h"

namespace pf8 {

std::vector<EndpointRaw> enumerateEndpoints(bool probeExclusiveRates = true);

} // namespace pf8

namespace pf8 {

// Endpoint id of the Windows default console device for `flow`, if any.
std::optional<std::string> defaultEndpointId(Flow flow);

} // namespace pf8
