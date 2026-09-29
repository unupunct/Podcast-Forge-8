#pragma once
// Per-channel and master DSP parameters (DSP.md). Written by the UI, read once per block by the
// tick (relaxed atomics); processing blocks smooth or interpolate every change.
#include <array>
#include <atomic>

#include "core/AtomicParam.h"

namespace pf8 {

enum class EqBandType : uint8_t { Peak, LowShelf, HighShelf };
enum class DetectorMode : uint8_t { Rms, Peak };

struct EqBandParams
{
    std::atomic<bool> on{true};
    std::atomic<EqBandType> type{EqBandType::Peak};
    AtomicParam freq{1000.0f}; // Hz, 20 … 20000
    AtomicParam gainDb{0.0f};  // ±18
    AtomicParam q{1.0f};       // 0.1 … 10
};

struct ChannelDspParams
{
    AtomicParam inputTrimDb{0.0f}; // −24 … +24 dB, before everything else
    std::atomic<bool> polarityInvert{false};

    // High-pass
    std::atomic<bool> hpfOn{true};
    AtomicParam hpfHz{80.0f};        // 20 … 300
    std::atomic<bool> hpf24dB{false}; // 12 or 24 dB/oct

    // Noise gate (downward expander with range)
    std::atomic<bool> gateOn{false};
    AtomicParam gateThresholdDb{-50.0f}; // −80 … 0 dBFS
    AtomicParam gateRangeDb{-40.0f};     // −80 … 0 dB
    AtomicParam gateAttackMs{2.0f};      // 0.1 … 50
    AtomicParam gateHoldMs{80.0f};       // 0 … 500
    AtomicParam gateReleaseMs{150.0f};   // 5 … 2000

    // 4-band parametric EQ
    std::atomic<bool> eqOn{false};
    std::array<EqBandParams, 4> eq;

    // De-esser
    std::atomic<bool> deesserOn{false};
    AtomicParam deessFreq{6500.0f};       // 3 … 12 kHz
    AtomicParam deessThresholdDb{-30.0f}; // dBFS of the sibilance band
    AtomicParam deessAmountDb{6.0f};      // max reduction 0 … 18 dB

    // Compressor
    std::atomic<bool> compOn{false};
    AtomicParam compThresholdDb{-20.0f}; // −60 … 0
    AtomicParam compRatio{4.0f};         // 1 … 20
    AtomicParam compAttackMs{5.0f};      // 0.1 … 100
    AtomicParam compReleaseMs{100.0f};   // 10 … 2000
    AtomicParam compKneeDb{6.0f};        // 0 … 12
    AtomicParam compMakeupDb{6.0f};      // 0 … 24
    std::atomic<bool> compAutoMakeup{false};
    std::atomic<DetectorMode> compDetector{DetectorMode::Rms};

    // Limiter
    std::atomic<bool> limiterOn{true};
    AtomicParam limiterCeilingDb{-1.0f}; // −12 … 0

    // Reverb send (post-fader, to the shared reverb)
    AtomicParam reverbSend{0.0f}; // linear 0 … 1

    ChannelDspParams()
    {
        const EqBandType types[4] = {EqBandType::LowShelf, EqBandType::Peak, EqBandType::Peak, EqBandType::HighShelf};
        const float freqs[4] = {120.0f, 400.0f, 3500.0f, 10000.0f};
        for (size_t i = 0; i < 4; ++i)
        {
            eq[i].type = types[i];
            eq[i].freq.set(freqs[i]);
            eq[i].q.set(i == 0 || i == 3 ? 0.7f : 1.0f);
        }
    }
};

struct MasterDspParams
{
    std::atomic<bool> limiterOn{true};
    AtomicParam limiterCeilingDb{-1.0f};
    std::atomic<bool> truePeak{true};

    // Shared reverb
    AtomicParam reverbRoomSize{0.45f};
    AtomicParam reverbDamping{0.5f};
    AtomicParam reverbWidth{1.0f};
    AtomicParam reverbReturn{0.35f}; // wet level of the return
};

} // namespace pf8
