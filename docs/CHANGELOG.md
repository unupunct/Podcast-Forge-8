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
