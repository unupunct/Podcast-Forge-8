#pragma once
// Live enumeration of every Windows audio endpoint (all states) via MMDevice, IDeviceTopology and
// SetupAPI. The calling thread must have initialised COM. Never throws; missing properties stay empty.
#include <vector>

#include "devices/DeviceInfo.h"

namespace pf8 {

std::vector<EndpointRaw> enumerateEndpoints(bool probeExclusiveRates = true);

} // namespace pf8
