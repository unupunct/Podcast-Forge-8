#pragma once
// Chooses which device's callback drives the engine tick (AUDIO_ENGINE.md "MasterClock").
#include <optional>
#include <string>
#include <vector>

#include "devices/DeviceInfo.h"

namespace pf8 {

struct MasterCandidate
{
    std::string endpointId;
    Flow flow = Flow::Render;
    bool online = false;
    bool userPreferred = false;
    bool isHeadphone = false; // assigned as a channel headphone output
    int order = 0;            // channel order (lower first)
};

// Returns the endpoint to use as master, or nullopt to fall back to the internal clock.
// Priority: user-preferred (online) > first online headphone > first online render > first online input.
std::optional<std::string> selectMaster(const std::vector<MasterCandidate>& candidates);

} // namespace pf8
