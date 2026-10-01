#pragma once
// The wiring the tick runs over: which bridges exist and which channel uses which bridge.
// Built on the control thread, handed to the engine through a lock-free queue, and retired back
// to the control thread for destruction. The tick only reads it (no refcount traffic on the RT
// thread: it uses .get() on the shared_ptrs).
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/Bridges.h"
#include "engine/StreamTypes.h"
#include "routing/RoutingTypes.h"

namespace pf8 {

struct ChannelRoute
{
    int inputBridge = -1;  // index into EngineGraph::inputs, -1 = unassigned
    int inputChannel = -1; // device channel, -1 = average of all device channels
    int outputBridge = -1; // headphones: index into EngineGraph::outputs
    int outputPair = 0;
};

// A bus sent to an output bridge other than a channel's headphones (monitor speakers/headphones,
// stream outputs for OBS/Discord such as Main, Clean Feed, Music).
struct BusOutput
{
    int bridge = -1; // index into EngineGraph::outputs
    int pair = 0;
};

struct EngineGraph
{
    static constexpr int kMaxInputBridges = 16;
    static constexpr int kMaxOutputBridges = 16;
    static constexpr int kMaxDeviceChannels = 32;

    std::vector<std::shared_ptr<InputBridge>> inputs;
    std::vector<std::shared_ptr<OutputBridge>> outputs;
    std::array<ChannelRoute, kNumChannels> channels{};
    ChannelRoute talkback{}; // dedicated talkback mic (input side only)
    // Extra bus outputs, indexed by BusId. HP buses normally go through ChannelRoute::outputBridge.
    std::array<BusOutput, kBusCount> busOutputs{};
    uint64_t generation = 0;
};

} // namespace pf8
