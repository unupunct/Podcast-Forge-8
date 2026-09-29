# Podcast Forge 8 — Design Spec

Date: 2026-09-29
Status: approved in brainstorming (sections 1–3), pending written-spec review

This spec is the decision record. The detailed subsystem documents live in `/docs`:
ARCHITECTURE, AUDIO_ENGINE, ROUTING, DEVICE_MANAGEMENT, RECORDING, DSP, TESTING.
The original product brief (37 sections) is the requirements baseline; this spec records how it
is met and where the approach deviates.

## 1. Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D1 | C++20, JUCE 8.0.x, CMake + Ninja, MSVC 14.44, Windows SDK 10.0.26100 | Brief recommends JUCE; toolchain present on the build machine |
| D2 | Licence **AGPLv3** (not the usual MIT) | JUCE 8 open-source licence is AGPLv3; user chose this over a commercial JUCE licence |
| D3 | **Multi-device engine, Approach A**: one master clock, every other device bridged by SPSC ring + variable-ratio resampler steered by a PI drift controller | 8 independent USB devices = 8 clocks; only continuous resampling gives click-free sync and real-time cross-device monitor mixes |
| D4 | WASAPI is the always-built backend. **Revised at planning:** each endpoint is opened by its *endpoint ID* through Podcast Forge's own `WasapiStream` (event-driven `IAudioClient`/`IAudioClient3`, MMCSS), not through JUCE's WASAPI `AudioIODevice`. JUCE is still used for UI, file formats, and the optional ASIO path | JUCE opens WASAPI devices by friendly name with order-dependent " (2)" suffixes for duplicates — identical USB mics would be resolved by enumeration order, which the brief forbids. Own streams also expose `IAudioClock` positions for drift control |
| D5 | ASIO optional: compiled only when `PF8_ASIO_SDK_DIR` points at a locally supplied Steinberg SDK | SDK is not redistributable; ASIO opens one driver at a time, so it only helps with a single multi-channel interface |
| D6 | No kernel driver. "Stream outputs" (Main / Clean Feed / Music) go to any output endpoint, e.g. VB-Audio Virtual Cable. A `VirtualDeviceBridge` interface is reserved for a future separately-signed driver | Brief forbids unsafe kernel drivers; VB-Cable is present on the machine |
| D7 | Hand-written DSP (biquads, envelope followers) instead of third-party DSP libs | Deterministic, testable against closed-form maths, no allocation |
| D8 | Project = folder with `Project.json` (+ `Metadata/`), audio as files. SQLite (amalgamation) for app settings, device identities, hotkeys, crash-restore snapshot | Brief: never store audio in the DB |
| D9 | WAV/BWF writers with periodic header patching + `Journal.json`; FLAC via JUCE `FlacAudioFormat` | Crash-safe: files are playable after a kill, headers rebuildable from the journal |
| D10 | Tests: Catch2 v3 + offline `EngineHarness` with fake clock-offset devices; in-app `--verify-ui` offscreen render and `--e2e` real-device session | Standing rule on this machine: never verify UI with desktop screenshots |
| D11 | Installer: Inno Setup 6, installed per-user into `tools/` | Clean uninstall that never touches projects/recordings |
| D12 | Execution: all 14 stages autonomously, each gated on Release x64 build + passing tests | User choice |

## 2. Scope

In scope: everything in the brief's sections 1–37, with these explicit limits:

- **Remote guests** (WebRTC/Discord/Zoom/browser): architecture only — `NetworkAudioSource` exists as
  an interface + null implementation; no network code ships.
- **Virtual audio devices**: provided via existing loopback endpoints (D6); no driver in this project.
- **Hardware mic gain**: the wizard never changes it. It recommends a software trim and tells the user
  what to adjust on the device. `IAudioEndpointVolume` level changes are offered only as an explicit,
  user-confirmed action where the endpoint reports a hardware volume range.
- **ASIO**: see D5.

## 3. Engine summary

```
Device threads (N, WASAPI, MMCSS "Pro Audio")
   input devices:  capture → SPSC ring ─┐
                                        ▼
Master tick (master device callback, or InternalClock on master loss)
   pull: 8 mics (resampled) + music + carts + talkback + remote
   per channel: trim → HPF → gate → EQ4 → de-esser → comp → limiter → pan/fader
   RoutingMatrix → Main, Clean, RecordMix, HP1..8, PFL, Monitor
   push: output rings → [resampler] → headphone/monitor/stream device threads
   push: record ring (lock-free) → Recorder worker → disk
   publish: meter/state snapshot (atomics, seqlock) → UI @ 30–60 Hz
UI → engine: atomic parameters + lock-free command queue for structural changes
```

Real-time rules (brief §36) are enforced by design and by a debug-build guard
(`RealtimeGuard` flags allocation on the tick thread via a hooked `operator new` in test builds).

## 4. Stages

As in the brief §35 (1: shell/enumeration/backend … 14: installer). Stage boundaries and their
test gates are set out in the implementation plan.

## 5. Known limitations (initial; maintained in docs/LIMITATIONS.md)

- WASAPI shared mode period is driver-limited (typ. 10 ms; 2.67–3 ms with IAudioClient3 on
  capable drivers). 64/128-sample buffers generally require exclusive mode, which locks the device
  away from other apps.
- USB class-compliant mics each run their own crystal; drift is corrected, not eliminated.
  Corrections are ±~1000 ppm max; beyond that the device is flagged "unstable sync".
- Endpoint IDs change if a device is moved to a different USB port on some systems; the
  fingerprint fallback restores it only when unambiguous.
- Two identical mics (same VID/PID/name, no serial) moved between ports cannot be told apart —
  the app asks rather than guessing.
- ASIO opens only one driver at a time.
- Test machine has 1 USB audio device + Realtek + NVIDIA HDMI + VB-Cable; 8×8 operation is
  verified with simulated devices in the harness, with real devices only up to what is attached.
