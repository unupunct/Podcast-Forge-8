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
