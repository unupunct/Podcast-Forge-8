#pragma once
// Per-channel DSP parameters (DSP.md). Written by the UI, read once per block by the tick.
// Stage 4 binds the strip controls; Stage 5 implements the processing and the full parameter set.
#include <atomic>

#include "core/AtomicParam.h"

namespace pf8 {

struct ChannelDspParams
{
    AtomicParam inputTrimDb{0.0f}; // −24 … +24 dB, before everything else
    std::atomic<bool> polarityInvert{false};

    std::atomic<bool> hpfOn{true};
    std::atomic<bool> gateOn{false};
    std::atomic<bool> eqOn{false};
    std::atomic<bool> deesserOn{false};
    std::atomic<bool> compOn{false};
    std::atomic<bool> limiterOn{true};
};

struct MasterDspParams
{
    std::atomic<bool> limiterOn{true};
    AtomicParam limiterCeilingDb{-1.0f};
};

} // namespace pf8
