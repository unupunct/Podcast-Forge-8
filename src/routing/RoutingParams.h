#pragma once
// Every UI-writable routing parameter. Written by the UI/control thread, read once per block by
// the tick (relaxed atomics), and smoothed there.
#include <array>
#include <atomic>

#include "core/AtomicParam.h"
#include "routing/RoutingTypes.h"

namespace pf8 {

struct ChannelOutputParams
{
    AtomicParam fader{1.0f};  // linear gain, post-DSP
    AtomicParam pan{0.0f};    // −1 … +1
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};
    std::atomic<bool> pfl{false};
    std::atomic<bool> coughOpen{true}; // driven by CoughMute; closed = removed from every bus
};

struct HeadphoneParams
{
    std::atomic<HpMode> mode{HpMode::Personal};
    AtomicParam volume{1.0f};
    std::atomic<bool> mute{false};
    std::atomic<bool> preFader{true}; // channel sends taken before the channel fader
    // Hearing protection: a brick-wall limiter on this headphone feed (0.5 ms look-ahead).
    std::atomic<bool> protectOn{true};
    AtomicParam protectCeilingDb{-6.0f}; // −24 … 0 dBFS
};

struct MonitorParams
{
    std::atomic<MonitorSource> source{MonitorSource::Main};
    std::atomic<bool> autoPfl{true}; // any PFL switches the monitor to the PFL bus
    AtomicParam volume{1.0f};
    std::atomic<bool> mute{false};
    std::atomic<bool> dim{false};
    AtomicParam dimGain{0.1f};       // −20 dB
    std::atomic<bool> mono{false};
    // Maximum-volume protection for the operator monitor (always on; ceiling configurable).
    AtomicParam maxCeilingDb{-3.0f}; // −24 … 0 dBFS
};

struct RoutingParams
{
    std::array<ChannelOutputParams, kRoutingChannels> channel;
    std::array<HeadphoneParams, kRoutingChannels> headphones;
    MonitorParams monitor;
    AtomicParam masterFader{1.0f};
    std::atomic<bool> masterMute{false};
    // gain[source][bus] for the buses the matrix controls directly (Main, Clean, MusicOut, HP1–8).
    std::array<std::array<AtomicParam, kBusCount>, kSourceCount> gain;
    std::array<AtomicParam, kSourceCount> sourcePan; // non-channel sources (music/carts use balance)
    // Talkback reaches Main / Clean / record only when this is set (Settings → Routing).
    std::atomic<bool> talkbackToProgram{false};
    // Talkback key held (or latched) and which headphone buses it reaches.
    std::atomic<bool> talkbackActive{false};
    std::array<std::atomic<bool>, kRoutingChannels> talkbackTarget{};
    AtomicParam talkbackLevel{1.0f};
    std::atomic<bool> talkbackDim{true}; // dim target headphones by −12 dB while talking

    RoutingParams() { applyDefaults(); }

    void applyDefaults() noexcept
    {
        for (auto& row : gain)
            for (auto& g : row) g.set(0.0f);
        for (int ch = 0; ch < kRoutingChannels; ++ch)
        {
            gain[static_cast<size_t>(ch)][idx(BusId::Main)].set(1.0f);
            gain[static_cast<size_t>(ch)][idx(BusId::Clean)].set(1.0f);
        }
        for (SourceId s : {SourceId::Music, SourceId::Carts})
        {
            gain[static_cast<size_t>(idx(s))][idx(BusId::Main)].set(1.0f);
            gain[static_cast<size_t>(idx(s))][idx(BusId::MusicOut)].set(1.0f);
        }
        gain[static_cast<size_t>(idx(SourceId::Remote))][idx(BusId::Main)].set(1.0f);
        gain[static_cast<size_t>(idx(SourceId::Remote))][idx(BusId::Clean)].set(1.0f);
        gain[static_cast<size_t>(idx(SourceId::Fx))][idx(BusId::Main)].set(1.0f);
        gain[static_cast<size_t>(idx(SourceId::Fx))][idx(BusId::Clean)].set(1.0f);
        for (int hp = 0; hp < kRoutingChannels; ++hp) applyPersonalTemplate(hp);
    }

    // "Self 100 %, others 70 %, music 20 %, carts 50 %, remote 70 %".
    void applyPersonalTemplate(int hp) noexcept
    {
        const int bus = hpBus(hp);
        for (int ch = 0; ch < kRoutingChannels; ++ch) gain[static_cast<size_t>(ch)][bus].set(ch == hp ? 1.0f : 0.7f);
        gain[static_cast<size_t>(idx(SourceId::Music))][bus].set(0.2f);
        gain[static_cast<size_t>(idx(SourceId::Carts))][bus].set(0.5f);
        gain[static_cast<size_t>(idx(SourceId::Remote))][bus].set(0.7f);
        gain[static_cast<size_t>(idx(SourceId::Fx))][bus].set(0.7f);
        gain[static_cast<size_t>(idx(SourceId::Talkback))][bus].set(0.0f); // talkback uses its own path
    }
};

} // namespace pf8
