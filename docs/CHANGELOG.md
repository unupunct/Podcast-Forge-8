# Changelog

## Stage 1 — Shell, device enumeration, audio backend (2026-09-29)

**Added**
- CMake/Ninja build (static CRT, x64 only), `scripts/env.ps1`, `scripts/fetch-deps.ps1`,
  `scripts/gate.ps1` (stage gate: build + tests, `-Live`, `-VerifyUi`).
- `core`: `SpscRing`, `MpscQueue` (bounded, Vyukov), `AtomicParam`, `SeqLockSnapshot`,
  `RealtimeGuard` (test builds count heap allocations on real-time threads), structured JSON-lines
  logger with an RT-safe front end and rotation (10 MB × 10), `Paths`, `CpuMeter`, `diskSpace`.
- `devices`: `EndpointRaw`/`DeviceInfo`, USB interface-path parsing (VID/PID/instance/serial),
  classification (USB mic / headset / interface / headphones, built-in, HDMI, virtual, Bluetooth),
  `DeviceRegistry` with add/remove/change diffs, live MMDevice + IDeviceTopology + SetupAPI
  enumerator including exclusive-mode rate probing.
- `engine`: `WasapiStream` — event-driven WASAPI capture/render opened **by endpoint ID**, shared,
  low-latency shared (IAudioClient3) and exclusive modes, float/int32/int24/int16 conversion,
  MMCSS "Pro Audio", FTZ/DAZ, device-invalidated detection. `InternalClock` (high-resolution
  waitable timer, absolute scheduling). `selectMaster`. `AudioEngine` skeleton (silent tick, load
  metering, automatic fallback to the internal clock). `EngineController` control thread (COM MTA).
- `ui`: dark look-and-feel, top bar (backend + granted mode, rate, requested vs granted buffer,
  CPU, audio load, disk, devices, record status), device list table with rescan.
- `app`: `PodcastForge8.exe` (asInvoker, PerMonitorV2), `--list-devices` JSON, `--help`.

**Changed**
- Design decision D4 revised: devices are opened through our own `WasapiStream` by endpoint ID
  instead of JUCE's WASAPI device type, which identifies devices by order-dependent names.

**Verified**
- 19 unit tests, 3 live tests on the build machine (Realtek, VB-Cable, Logitech BRIO USB mic).
- App starts, opens the default output by endpoint ID (shared, 48 kHz, 480-frame period granted),
  and exits cleanly (log shows `app.exit clean`).

**Known gaps carried into Stage 2**
- For composite USB devices (e.g. a webcam's mic) the serial lives on the parent USB device;
  the enumerator currently reads only the audio interface's instance id → `hasSerial=false`.

## Stage 2 — 8 mic channels, 8 headphone outputs, drift bridges, hot-plug (2026-09-29)

**Added**
- `VarResampler` (48-tap polyphase windowed sinc, slew-limited variable ratio, master passthrough).
- `DriftController` (PI, three-stage fill filter, Locked/Converging/Unstable).
- `InputBridge` / `OutputBridge` with hardware-timestamp fill linearisation, burst-aware targets,
  measured-fill priming with one-time re-centring, 5 ms fades, counters.
- `AudioEngine` rebuilt device-agnostic around an `EngineGraph` published through a lock-free
  queue (retired graphs freed on the control thread), tick guard, `EngineTap`, per-channel meters.
  Headphones temporarily monitor their own channel until the Stage 3 matrix.
- `EngineController` reconcile loop: registry + assignments → resolution → open/close only the
  affected streams → master selection → graph. Hot-plug via `HotplugWatcher` (300 ms debounce).
- `DeviceIdentity`, resolution rules (exact / serial fingerprint / possible match / none),
  `Assignments` JSON, parent-USB serial for composite devices, `SettingsDb` (SQLite),
  `core/Json`, `core/Clock`.
- UI: Channels page (name, mic + input channel, headphones, status, sync, level meter, one-click
  confirm for possible matches); top bar shows master, mode, streams and device health.
- `tests/harness`: `EngineHarness` with fake devices (ppm, period, jitter), click detector.

**Verified**
- 43 tests + 2 live tests. Harness: 10-minute six-clock lock (estimates within 0.06 ppm, no
  underruns/glitches, zero RT allocations), inter-device alignment ≤ 0.15 samples, bit-exact
  hot-plug isolation. Live: BRIO + VB-Cable loop Locked in 15 s with zero underruns.

**Found and fixed during the stage**
- Aliasing of the packet/block beat into the drift loop (±90 ppm error) → fill linearisation.
- Callback jitter in the measurement (±2.5 samples alignment wander) → hardware timestamps.
- Master burst not covered by bridge targets (underruns at 128-frame devices) → `engineBurst`.
- Bridges starting hundreds of frames high after late graph join → measured-fill priming.
- Resampler read before its buffer when leaving passthrough → passthrough is master-only.

## Stage 3 — Routing matrix (2026-09-29)

**Added**
- `src/routing`: `RoutingEngine` (12 sources × 13 buses, 10 ms matrix ramps, channel fader / pan /
  mute, cough, pre- or post-fader headphone sends, HP modes Main / Personal / Custom with a 20 ms
  crossfade, HP volume / mute, PFL, solo-in-place on the monitor only, monitor source / auto-PFL /
  volume / dim / mono / mute, master fader / mute on Main and Clean, talkback lock + targets + dim),
  `RoutingParams` (atomics, Personal template), `Ramp`, `CoughMute` (push-to-mute / push-to-talk /
  toggle with auto-repeat and focus-loss safety).
- Engine: routing runs on every tick; each channel's HP bus feeds its headphone endpoint;
  `EngineGraph::busOutputs` for monitor and stream outputs; per-bus peak meters, solo/PFL flags.

**Verified**
- 56 tests: ROUTING.md invariants 1–5 (exact gains, talkback lock, solo/PFL bit-exact isolation of
  Main/Clean/HP, mute vs record tap, ramp slope), pan law, HP modes, monitor section, talkback dim,
  cough state machine, zero RT allocations; the 10-minute harness run passes through routing.

## Stage 4 — Mixer UI, Device Matrix, --verify-ui (2026-09-29)

**Added**
- Console widgets: rotary `Knob`, dB `Fader` (−∞…+10 dB, skewed, collision-free scale), `Meter`
  (mono/stereo, peak + RMS + 1.5 s hold + clip latch, −18/−6 dBFS guides), `ToggleLed` in function
  colours (mute red, solo yellow, PFL teal, DSP blue, REC red, MON violet), `StatusLed`.
- `ChannelStripView` ×8: CH/name (double-click to rename), input device + status LED, GAIN (input
  trim, live), GATE/COMP/EQ/DE-ESS (bound; processing in Stage 5), PAN, MUTE/SOLO/PFL, fader,
  meter, REC arm, MON (headphones on/off), CONFIG (mic wizard hook).
- `MasterStripView`: stereo program meter, master fader, LIMITER, MUTE; MONITOR source (auto-PFL
  shown), monitor output device, volume, DIM, MONO, MUTE.
- `RoutingGridView` (bottom dock): 12 sources × 11 buses, drag to set, double-click toggles,
  editing a headphone column switches it to Custom; talkback row shows LOCKED and HP targets.
- `DeviceMatrixView`: channel rows (mic, input channel, headphones, status, sync, meter), output
  roles (Monitor, Stream Main / Clean / Music), unassigned-device pool, drag and drop (pool → cell,
  cell → cell swaps), per-cell device menu, AUTO ASSIGN with a confirmation listing every change
  (Return and Escape cancel).
- `proposeAutoAssign` / `applyProposal`: fills only empty cells (headsets paired by container, USB
  only), never overwrites — also not a cell assigned between proposal and confirmation.
- Output roles in `Assignments` + controller reconcile + `EngineGraph::busOutputs`; per-channel
  input trim (ramped), REC arm, DSP enable flags (`dsp/DspParams.h`).
- `--verify-ui`: 3 pages × 7 resolution/scale configurations (1080p 100/125/150 %, 1440p 100/150 %,
  2160p 150/200 %), layout checks, PNGs + report.json; now part of `scripts\gate.ps1`.

**Found and fixed during the stage**
- The first verify runs reported 0 issues because the offscreen root was invisible and skipped
  entirely; a built-in canary (a deliberately broken layout that must be flagged) now fails the run
  if the checker ever stops checking. With the checker working it found: PAN dial 32 px at
  1080p/150 %, CONFIG text overflow, crowded fader scale — all fixed.
