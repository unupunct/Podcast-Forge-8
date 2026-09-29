# Stage 3 — Routing Matrix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Every source reaches every bus through one ramped gain matrix; channel fader/pan/mute,
solo-in-place (monitor only), PFL, cough mute (push-to-mute / push-to-talk / toggle), headphone-mix
modes (Main / Personal / Custom), monitor source/volume/dim/mono/mute, and the talkback lock — all
satisfying the ROUTING.md invariants exactly.

**Spec:** ROUTING.md §1–7.

## Files
```
src/routing/RoutingTypes.h     SourceId, BusId, HpMode, MonitorSource, constants
src/routing/Ramp.h             linear gain ramp (per-sample, allocation-free)
src/routing/RoutingParams.h    all UI-writable routing parameters as atomics
src/routing/RoutingEngine.h/.cpp  process(sources → buses): strip output (fader/pan/mute/cough),
                               matrix, HP modes, PFL, solo-in-place monitor, monitor controls
src/routing/CoughMute.h/.cpp   key state machine → open/closed
src/engine/AudioEngine.*       tick: inputs → [DSP, Stage 5] → record tap → routing → bus outputs
src/engine/EngineGraph.h       + monitor / main / clean / music stream outputs (assigned in Stage 7)
tests/unit/test_routing.cpp, test_cough.cpp
```

## Interfaces
```cpp
enum class SourceId : uint8_t { Ch1..Ch8 = 0..7, Music = 8, Carts = 9, Talkback = 10, Remote = 11, Count = 12 };
enum class BusId : uint8_t { Main, Clean, MusicOut, Hp1..Hp8 (3..10), Pfl = 11, Monitor = 12, Count = 13 };
enum class HpMode : uint8_t { Main, Personal, Custom };
enum class MonitorSource : uint8_t { Main, Pfl, Clean, Hp1..Hp8 };
struct RoutingInputs  { const float* channel[8]; const float* musicL, *musicR, *cartsL, *cartsR, *talkback, *remote; };
struct RoutingOutputs { float* left[13]; float* right[13]; };
class RoutingEngine { void prepare(double rate, int maxBlock); void process(const RoutingInputs&, RoutingOutputs&, int n) noexcept;
                      RoutingParams& params() noexcept; };
```

## Tests (ROUTING.md §7 + extras)
1. Impulse on CH n with default routing: Main = fader·panL/R, HP n = 1.0, HP m = 0.7 (1e-6) after ramps.
2. Talkback never reaches Main, Clean or the record tap while locked; reaches its HP targets only while held.
3. Solo and PFL leave Main, Clean and every HP bus bit-identical to a run without them; Monitor = soloed channels / PFL.
4. Mute removes a channel from Main/Clean/HP buses but not from the record tap.
5. A gain step produces a ramp no steeper than the 10 ms slope (no discontinuity).
6. Constant-power pan: centre = −3.01 dB each side, hard left = 1/0.
7. HP modes: Main mode = Main bus × HP volume; Personal template = self 1.0 / others 0.7 / music 0.2.
8. Cough state machine for all three modes, plus 5 ms ramps.
9. Monitor dim (−20 dB), mono ((L+R)/2), mute, volume.
10. No allocations in process() (RealtimeGuard).
