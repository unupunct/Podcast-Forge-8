# Stage 2 — 8 Mic Channels, 8 Headphone Outputs, Drift Bridges, Hot-plug

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 8 channels, each with an assigned input endpoint (+ channel selection) and headphone endpoint, running concurrently on independent devices. Each non-master device is bridged by ring + variable resampler + PI drift control, verified by an offline harness at ±200 ppm for 10 simulated minutes. Assignments persist by stable identity and survive unplug/replug without reassigning anything.

**Spec:** AUDIO_ENGINE.md §3–6, DEVICE_MANAGEMENT.md §2–3, TESTING.md §2–3 (sync/drift, reconnection, device assignment).

## Key design refinements (recorded in AUDIO_ENGINE.md during this stage)

1. **Bridges are independent of WASAPI.** `InputBridge` / `OutputBridge` expose a *device side*
   (called from a device thread or the harness) and an *engine side* (called from the tick).
   `WasapiStream` callbacks and `FakeDevice`s both drive the same bridge code.
2. **Master pull model.** The master device's callback asks its own bridge for `n` device frames;
   the bridge runs `engine.tick(engineBlock)` until it has ≥ n frames, then hands them over.
   Engine block (the user's buffer setting) is therefore decoupled from each device's period,
   the master path adds ≤ 1 engine block of latency, and the master bridge runs at a fixed nominal
   ratio (no drift loop). A capture-only master ticks whenever its ring holds ≥ one block.
3. **Tick guard.** `tick()` is protected by an atomic try-flag; a second caller during a master
   hand-off skips instead of re-entering.
4. **Multi-channel endpoints.** Input bridges carry every device channel; a channel's input is
   `{endpoint, channelIndex}` where `-1` = average of all channels (default for 1–2 ch devices).
   Output bridges carry a stereo pair `{endpoint, pairIndex}`; mono devices get (L+R)/2.
5. **Target fill** = device period + engine block + 48 frames (1 ms). Shared-mode 10 ms devices
   therefore add ≈ 13–20 ms; exclusive 128-frame devices ≈ 6 ms. Shown per device in the UI.

## Files

```
src/engine/VarResampler.h/.cpp      polyphase windowed-sinc, variable ratio, multi-channel
src/engine/DriftController.h/.cpp   PI loop on averaged fill error → ratio (ppm), status
src/engine/Bridges.h/.cpp           InputBridge, OutputBridge (device side / engine side)
src/engine/EngineGraph.h            channel ↔ bridge wiring passed to the engine via command queue
src/engine/AudioEngine.*            tick: pull inputs → channel buffers → meters → HP outputs; tick guard; master pull
src/engine/EngineController.*       stream lifecycle, hot-plug handling, master failover, assignment apply
src/devices/DeviceIdentity.h/.cpp   identity + fingerprint + resolution rules (pure)
src/devices/AssignmentStore.h/.cpp  channel → identity (+channel/pair index), JSON (de)serialisation
src/devices/HotplugWatcher.h/.cpp   IMMNotificationClient → callback (debounced by the controller)
src/devices/WinEndpointEnumerator.cpp  parent USB serial via CM_Get_Parent for composite devices
src/project/SettingsDb.h/.cpp       SQLite key/value store (%LOCALAPPDATA%\PodcastForge8\Settings.db)
src/ui/ChannelsView.h/.cpp          8 rows: name, mic + input channel, headphones, status, level meter
tests/harness/FakeDevice.h, EngineHarness.h/.cpp, Signals.h, Analysis.h
tests/unit/test_resampler.cpp test_drift.cpp test_bridges.cpp test_identity.cpp test_assignments.cpp test_settingsdb.cpp
tests/harness/test_harness_sync.cpp test_harness_hotplug.cpp
```

## Interfaces

```cpp
namespace pf8 {
class VarResampler {                       // not thread-safe; engine or device side only
public:
  void prepare(int channels, double nominalRatio /*in/out rate*/, int maxOutFrames); // allocates
  void setRatioCorrection(double ppm) noexcept;   // applied smoothly (slew ≤ 2 ppm/ms of output)
  double effectiveRatio() const noexcept;
  int  inputFramesNeeded(int outFrames) const noexcept;   // to produce outFrames next call
  void pushInput(const float* interleaved, int frames) noexcept;
  int  process(float* interleavedOut, int outFrames) noexcept;  // returns produced (== outFrames when enough input)
  int  bufferedInput() const noexcept;
  void reset() noexcept;
  static constexpr int kTaps = 32, kPhases = 256;
};
enum class SyncStatus : uint8_t { Offline, Priming, Converging, Locked, Unstable, Native };
class DriftController {
public:
  void prepare(double engineRate, double targetFill);
  // called once per engine block with the instantaneous fill (frames); returns ppm correction
  double update(double fillFrames, int blockFrames) noexcept;
  double ppm() const noexcept; double averagedError() const noexcept; SyncStatus status() const noexcept;
  void reset(double keepPpm = 0.0) noexcept;
};
struct BridgeStats { uint64_t underruns, overruns, droppedFrames; double ppm, fill, target; SyncStatus status; };
class InputBridge {            // device → engine
public:
  InputBridge(int deviceChannels, int deviceRate, int devicePeriod, int engineRate, int engineBlock, bool master);
  void deviceWrite(const float* interleaved, int frames) noexcept;               // device thread
  void engineRead(float* const* perChannelOut, int frames) noexcept;               // tick: deviceChannels outputs at engine rate
  int  deviceChannels() const noexcept; size_t readableDeviceFrames() const noexcept;
  BridgeStats stats() const noexcept;                                              // any thread (atomics)
};
class OutputBridge {           // engine → device
public:
  OutputBridge(int deviceChannels, int deviceRate, int devicePeriod, int engineRate, int engineBlock, bool master);
  void engineWrite(const float* left, const float* right, int frames) noexcept;    // tick
  void deviceRead(float* interleaved, int frames) noexcept;                        // device thread
  size_t readableDeviceFrames() const noexcept;
  BridgeStats stats() const noexcept;
};
}
```

Resolution (DEVICE_MANAGEMENT.md §2):
```cpp
struct DeviceIdentity { std::string endpointId; Flow flow; std::string containerId; uint16_t vid=0,pid=0;
                        std::string serial /*empty if none*/; std::string friendlyName; };
DeviceIdentity identityOf(const DeviceInfo&);
enum class MatchKind { Exact, Fingerprint, PossibleMatch, None };
struct Resolution { MatchKind kind; std::string endpointId; /* resolved or candidate */ };
// `taken` = endpoint ids already resolved for other channels in the same role
Resolution resolve(const DeviceIdentity& saved, const std::vector<DeviceInfo>& online, const std::set<std::string>& taken);
struct ChannelAssignment { std::optional<DeviceIdentity> mic; int micChannel = -1;
                           std::optional<DeviceIdentity> headphones; int hpPair = 0; std::string name; };
struct Assignments { std::array<ChannelAssignment, 8> ch; std::string preferredMaster; };
std::string toJson(const Assignments&); std::optional<Assignments> assignmentsFromJson(const std::string&);
```

## Tasks (each: failing test → implement → gate → commit)

1. **VarResampler.** Tests: ratio 1.0 → a 1 kHz sine passes with error < −80 dBFS after the filter delay;
   44.1 k→48 k and 48 k→44.1 k: the output tone lands at the right frequency (Goertzel peak ≥ −0.1 dB,
   residual after subtracting a best-fit sine < −70 dB); a correction step of +500 ppm produces no
   discontinuity (max |2nd difference| within 1.5× the ideal sine's); `process` allocates 0 times under RealtimeGuard;
   `inputFramesNeeded` is exact (process after pushing exactly that many returns outFrames).
2. **DriftController.** Closed-loop test against a simulated fill integrator (device ±200 ppm, 480-frame
   device period sawtooth): converges to |mean error| < 0.5 block within 30 s, ppm estimate within ±5 ppm of the
   true offset after 60 s, status Locked; a ±1500 ppm device saturates at the clamp → Unstable.
3. **Bridges.** Tests: InputBridge primes (silence) until target fill, then passes a ramp contiguously; forced
   underrun counts once and fades in; overrun counts dropped frames; OutputBridge mono device gets (L+R)/2;
   master mode passes samples with zero added delay (ratio exact, no drift loop).
4. **EngineHarness + engine wiring.** `FakeDevice{rate, period, ppm, jitterUs, signal}` scheduled by simulated
   time; `AudioEngine` accepts an `EngineGraph` (bridges per channel, master bridge id) via a command; tick pulls
   8 inputs into channel buffers, publishes per-channel peak/RMS, writes HP n = channel n (placeholder until the
   Stage 3 matrix). Harness tests: (a) 3 input + 3 output fake devices at +200/−150/+80 ppm for 10 simulated
   minutes → all Locked, no underruns after priming, discontinuity detector clean on 1 kHz sines, inter-track
   correlation lag constant ±1 sample over the last 5 minutes; (b) master pull model delivers exactly the
   requested frames with ≤ 1 block extra latency; (c) RealtimeGuard = 0 allocations across the whole run.
5. **Identity + resolution + assignments JSON + SettingsDb.** Tests: exact; fingerprint with serial updates the id;
   weak fingerprint (no serial) → PossibleMatch, never assigned; an endpoint taken by another channel is not reused;
   headset pairs resolved by container; JSON round-trip; SettingsDb set/get/overwrite/persist across reopen.
   Enumerator: parent serial via `CM_Get_Parent` + `CM_Get_Device_IDW` when the interface's own instance id has '&'.
6. **Hot-plug in the harness.** Remove CH3's input device at t=5 s, return at t=8 s (same identity): CH3 silent while
   gone, every other channel's output bit-identical to a run without the event, CH3 restored to channel 3 with no
   other assignment changed; removing the master fails over (to the next render device, or the internal clock) with
   no tick running twice concurrently.
7. **Live wiring + ChannelsView.** `HotplugWatcher` (IMMNotificationClient) → controller debounce 300 ms → rescan →
   resolve → open/close only affected streams. ChannelsView rows with mic/HP combo boxes (grouped: USB first), input
   channel selector, status text (OK / OFFLINE / DISCONNECTED / POSSIBLE MATCH / IN USE), live peak meter.
   Live test `[live]`: assign the BRIO mic to CH1 and VB-Cable Input as HP1 + Realtek as HP2, run 3 s, meters move
   (BRIO room noise > −90 dBFS), both outputs Running, drift status Converging or Locked.
8. **Gate + docs:** AUDIO_ENGINE.md refinements 1–5, CHANGELOG Stage 2, commit.
