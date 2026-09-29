#pragma once
// Sources, buses and modes of the routing matrix (ROUTING.md §1).
#include <cstdint>

namespace pf8 {

constexpr int kRoutingChannels = 8;

enum class SourceId : uint8_t
{
    Ch1 = 0, Ch2, Ch3, Ch4, Ch5, Ch6, Ch7, Ch8,
    Music = 8,
    Carts = 9,
    Talkback = 10,
    Remote = 11,
    Fx = 12, // shared reverb return (stereo)
};
constexpr int kSourceCount = 13;

enum class BusId : uint8_t
{
    Main = 0,
    Clean = 1,    // Main without music and carts
    MusicOut = 2, // music + carts
    Hp1 = 3, Hp2, Hp3, Hp4, Hp5, Hp6, Hp7, Hp8,
    Pfl = 11,
    Monitor = 12,
};
constexpr int kBusCount = 13;

constexpr int hpBus(int channel) noexcept { return static_cast<int>(BusId::Hp1) + channel; }
constexpr int idx(SourceId s) noexcept { return static_cast<int>(s); }
constexpr int idx(BusId b) noexcept { return static_cast<int>(b); }

enum class HpMode : uint8_t { Main, Personal, Custom };

enum class MonitorSource : uint8_t { Main = 0, Pfl, Clean, Hp1, Hp2, Hp3, Hp4, Hp5, Hp6, Hp7, Hp8 };

enum class CoughMode : uint8_t { PushToMute, PushToTalk, Toggle };

const char* toString(SourceId s) noexcept;
const char* toString(BusId b) noexcept;
const char* toString(HpMode m) noexcept;

} // namespace pf8
