#pragma once
// Everything a project remembers (brief §24): device assignments and channel names, DSP, routing,
// headphone mixes, monitor, master, talkback, recording options, pre-roll, soundboard carts,
// music playlist and ducking. Captured as JSON on the UI thread and applied back the same way.
//
// Each parameter group is described once by a `visit…` function used for both directions, so
// save and load can never disagree about a field.
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "core/Json.h"

namespace pf8 {

class EngineController;

namespace project {

constexpr int kStateVersion = 1;

json::Value captureState(EngineController& controller);

struct ApplyResult
{
    std::vector<std::pair<int, std::filesystem::path>> carts; // cart → file to load (caller loads them)
    std::vector<std::string> warnings;                        // missing files etc., for the user
};

// UI thread. Unknown or out-of-range values are ignored (the current value stays); missing groups
// keep their current state. Device assignments are re-resolved by identity.
ApplyResult applyState(const json::Value& state, EngineController& controller);

} // namespace project
} // namespace pf8
