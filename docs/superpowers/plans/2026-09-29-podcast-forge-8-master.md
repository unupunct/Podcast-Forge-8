# Podcast Forge 8 — Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Podcast Forge 8, a native Windows x64 8-mic / 8-headphone podcast console, in 14 gated stages.

**Architecture:** JUCE 8 C++20 app of 11 static-library modules (core, devices, engine, sources, dsp, routing, record, media, project, ui, app). Multi-device WASAPI engine: one master device's callback drives the tick; every other device is bridged by an SPSC ring and a variable-ratio resampler steered by a PI drift controller.

**Tech Stack:** C++20, JUCE 8.0.15, CMake + Ninja (VS 2022 BuildTools bundled), MSVC 14.44, Windows SDK 10.0.26100, SQLite 3.53.4 amalgamation, Catch2 v3.16.0, Inno Setup 6.

**Spec:** `docs/superpowers/specs/2026-09-29-podcast-forge-8-design.md` + `docs/ARCHITECTURE.md`, `AUDIO_ENGINE.md`, `ROUTING.md`, `DEVICE_MANAGEMENT.md`, `RECORDING.md`, `DSP.md`, `TESTING.md`.

## Global Constraints

- Windows 10/11 x64 only; `asInvoker` manifest; no admin for normal operation.
- Licence AGPLv3 (JUCE open-source licence). Copyright line: `Copyright (C) 2026 unupunct`.
- Default 48 kHz, internal float, 24-bit files; buffers 64/128/256/512.
- Tick thread: no allocation, no locks, no file I/O, no disk logging, no network, no UI calls.
- Never delete, truncate or overwrite an existing recording. Files created with `CREATE_NEW` semantics.
- Never assign a device by enumeration order; never auto-reassign a channel whose device vanished.
- Talkback never reaches Main/Clean/record unless "Talkback to recording" is on.
- No telemetry, no network code, no process spawning, no shell execution. Never log audio.
- Logs: `%LOCALAPPDATA%\PodcastForge8\Logs\`, JSON lines, 10 MB × 10 rotation.
- Everything lives in `C:\Projects\PodcastForge8`; build output in `build/`.
- Commits: identity `unupunct <65507390+unupunct@users.noreply.github.com>`, trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- UI is verified with `--verify-ui` offscreen rendering, never desktop screenshots.

## Stage gate (every stage)

```powershell
& tools\env.ps1            # sets MSVC x64 env + PATH to bundled cmake/ninja
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
build\release\PodcastForge8_artefacts\Release\PodcastForge8.exe --verify-ui   # from Stage 4
```
A stage is done only when all of the above pass, `docs/CHANGELOG.md` has a stage entry, and the
stage is committed. Never start the next stage from a broken build.

## Stages

Each stage gets its own detailed plan `docs/superpowers/plans/2026-09-29-stageNN-<name>.md`,
written immediately before execution against the code that actually exists by then.

| # | Stage | Deliverable | Gate tests |
|---|---|---|---|
| 1 | Shell, enumeration, backend | Build system, `core` (rings, params, snapshot, RT guard, logger), `devices` (registry, classification, identity), `engine` skeleton (DeviceStream, InternalClock, master tick of silence), app window with top bar + device list, `--list-devices` | core, logger rotation, classification, registry diff, clock tick count |
| 2 | 8 mic channels, 8 HP outputs | Channel model, `AssignmentStore` + resolution rules, `HotplugWatcher`, per-device streams, `VarResampler`, `DriftController`, `EngineHarness` with fake devices | resolution rules, reconnection, drift lock ±200 ppm 10 min, no discontinuity, RT guard = 0 |
| 3 | Routing matrix | `RoutingMatrix`, pan law, ramps, mute/solo/PFL, cough mute state machine, buses | ROUTING.md invariants 1–5 |
| 4 | Mixer UI | Channel strips, master, meters, top bar live, Device Matrix (drag-drop, Auto Assign confirm), `--verify-ui` | verify-ui at 3 scales, Auto Assign conflict logic |
| 5 | DSP | HPF, gate, compressor, EQ4, de-esser, limiter, reverb send, presets, strip chain, mic wizard | DSP.md §6 targets, presets, wizard analysis on synthetic signals |
| 6 | Multitrack recording | Record ring, recorder worker, WAV/BWF/FLAC writers, journal, recovery, DiskGuard, markers, session naming | round-trip, kill-recovery, disk-full, no-overwrite, markers export |
| 7 | Headphone mixes | HP modes, templates, protection limiter, monitor section (dim/mono/mute/max), HP UI | HP invariants, protection ceiling, mode crossfade |
| 8 | Soundboard | 24 carts, decoder thread, fades, routing, hotkeys, UI | envelopes, 24 simultaneous carts, no RT allocation |
| 9 | Music / ducking | MusicPlayer, Ducker, Music tab | ducking timing, clean-feed exclusion |
| 10 | Talkback | Talkback source/targets/lock/dim, UI + hotkey | talkback never on Main/record while locked |
| 11 | Pre-record buffer | PreRollBuffer, allocation on enable, recorder integration | sample-exact pre-roll impulse test |
| 12 | Project management | Project model, JSON IO, SettingsDb (SQLite), New/Open/Save/Save As/Archive, crash-restore snapshot, hotkey config | round-trip every field, corrupt-file handling, migration |
| 13 | Diagnostics | CPU/load/xrun/disk/drift panels, glitch reports, watchdog + controlled restart, settings pages (all 10), `--e2e` | watchdog restart harness test, e2e on real devices |
| 14 | Installer | Inno Setup script, uninstall preserves projects/recordings/config, README, LIMITATIONS, OBS.md, final Release x64 E2E | install/uninstall into a temp dir, files preserved |

## Cross-stage interfaces (fixed now so stages line up)

```cpp
namespace pf8 {
constexpr int kNumChannels = 8;
constexpr int kMaxBlock    = 512;
enum class SourceId : uint8_t { Ch1=0, /*…*/ Ch8=7, Music=8, Carts=9, Talkback=10, Remote=11, Count=12 };
enum class BusId    : uint8_t { Main=0, Clean, MusicOut, Hp1, /*…*/ Hp8=10, Pfl, Monitor, Count };

class AudioSource {                       // sources module
public:
  virtual ~AudioSource() = default;
  virtual void prepare(double sampleRate, int maxBlock) = 0;   // may allocate
  virtual int  numChannels() const noexcept = 0;               // 1 or 2
  virtual void pull(float* const* dest, int numFrames) noexcept = 0; // RT-safe, silence if offline
  virtual SourceStatus status() const noexcept = 0;
};

class TickClient {                        // engine module: the tick entry point
public:
  virtual ~TickClient() = default;
  virtual void tick(int numFrames) noexcept = 0;
};
}
```
