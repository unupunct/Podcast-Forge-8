# Podcast Forge 8 — Architecture

Podcast Forge 8 is a native Windows (x64) 8-person podcast console: 8 microphones, 8 headphone
outputs, per-channel DSP, a routing matrix with 8 independent headphone mixes, multitrack
crash-safe recording, soundboard, music with ducking, talkback, and hot-plug recovery across
independently clocked USB devices.

## 1. Technology

| Concern | Choice |
|---|---|
| Language | C++20 |
| Framework | JUCE 8.0.x (GUI, audio device layer, file formats) — AGPLv3 |
| Build | CMake ≥ 3.25 + Ninja, MSVC 14.44 (VS 2022 Build Tools), Windows SDK 10.0.26100 |
| Audio backend | WASAPI (shared + exclusive); ASIO optional (`PF8_ASIO_SDK_DIR`) |
| Persistence | SQLite 3 amalgamation (settings, device identities, hotkeys, restore snapshot); `Project.json` per project |
| Tests | Catch2 v3, `EngineHarness` offline renderer |
| Installer | Inno Setup 6 |

## 2. Module map

Each module is a static library with a narrow public header set. Arrows = "depends on".

```
                 app ──────────────┐
                  │                │
                  ▼                ▼
                 ui ──────────► project ──► core
                  │                │
                  ▼                ▼
   ┌──────── engine ◄───────── record
   │          │   │               │
   ▼          ▼   ▼               ▼
devices    routing dsp          core
   │          │     │
   ▼          ▼     ▼
  core       core  core
 sources ──► core ;  engine ──► sources, media
 media   ──► dsp, core
```

| Module | Responsibility | Key types |
|---|---|---|
| `core` | Lock-free queues, parameter store, IDs, time, log front-end, RT guard | `SpscRing<T>`, `MpscQueue<T>`, `AtomicParam`, `SeqLockSnapshot<T>`, `Log`, `RealtimeGuard` |
| `devices` | Enumerate endpoints, stable identity, hot-plug notifications, assignment persistence | `DeviceRegistry`, `DeviceIdentity`, `DeviceInfo`, `HotplugWatcher`, `AssignmentStore` |
| `engine` | Open per-device streams, master clock, drift control, the master tick | `AudioEngine`, `DeviceStream`, `MasterClock`, `InternalClock`, `DriftController`, `VarResampler`, `EngineCommand` |
| `sources` | Abstract audio sources | `AudioSource`, `DeviceInputSource` (USB mic), `SystemLoopbackSource`, `FileAudioSource`, `NetworkAudioSource` (null), `VirtualAudioSource` (null) |
| `dsp` | Per-channel processing blocks + presets | `Biquad`, `HighPass`, `NoiseGate`, `Compressor`, `ParametricEq4`, `DeEsser`, `Limiter`, `ReverbSend`, `ChannelStrip`, `Presets` |
| `routing` | Gain matrix, headphone mixes, monitor section, solo/PFL, talkback, cough mute | `RoutingMatrix`, `BusId`, `SourceId`, `HeadphoneMix`, `MonitorSection`, `SoloPfl`, `Talkback`, `CoughMute` |
| `record` | Record ring → disk, formats, journal, recovery, pre-roll, disk guard, markers | `Recorder`, `TrackWriter`, `WavBwfWriter`, `FlacWriter`, `Journal`, `Recovery`, `PreRollBuffer`, `DiskGuard`, `MarkerList` |
| `media` | Music player with ducking, 24-cart soundboard | `MusicPlayer`, `Ducker`, `Soundboard`, `Cart` |
| `project` | Project model, JSON (de)serialisation, SQLite store, archive | `Project`, `ProjectIO`, `SettingsDb`, `ProjectArchive` |
| `ui` | All JUCE components | `MainWindow`, `TopBar`, `ChannelStripView`, `MasterView`, `DeviceMatrixView`, `RoutingView`, `HeadphoneMixView`, `SoundboardView`, `MusicView`, `MarkersView`, `SettingsWindow`, `DiagnosticsView`, `MicWizard` |
| `app` | Entry point, CLI modes, hotkeys, crash handler | `PodcastForgeApp`, `HotkeyManager`, `CliModes` |

## 3. Threads

| Thread | Priority | Does | Must not |
|---|---|---|---|
| Device thread ×N (JUCE WASAPI) | MMCSS "Pro Audio" | Move samples between the WASAPI buffer and its SPSC ring. The master device's thread also runs the tick | allocate, lock, I/O, log to disk |
| Master tick (on master device thread or `InternalClock` thread) | MMCSS "Pro Audio" | DSP, routing, mixes, meter snapshot, record push | same as above |
| Recorder worker | above normal | Drain record ring, write files, patch headers, update journal | touch UI |
| Media decoder | normal | Decode music/carts into pre-allocated buffers ahead of playback | block the tick |
| Device control | normal | Open/close/rebuild `DeviceStream`s on hot-plug and rate change | run on the tick |
| Logger | below normal | Drain log MPSC queue to rotating JSON files | — |
| UI (JUCE message thread) | normal | Render, poll snapshots at 60 Hz, post commands | call engine methods that block |
| Watchdog | normal | Detect tick stall > 500 ms, trigger controlled restart | touch open recordings |

## 4. UI ↔ engine contract

- **Continuous parameters** (gain, fader, pan, send levels, DSP params): `AtomicParam<float>`
  written by UI, read once per tick, smoothed inside the engine (10–50 ms ramps).
- **Structural changes** (assign device, add/remove stream, reset DSP state, start/stop record,
  load cart buffer): `EngineCommand` pushed onto a lock-free MPSC queue; the tick applies them at
  block start. Any memory a command needs is allocated by the sender and handed over by pointer;
  retired memory is returned on a "garbage" SPSC ring and freed on the UI thread.
- **State out** (meters, peaks, clip, drift, xruns, record position, disk): `SeqLockSnapshot`
  written once per tick, read by the UI at 60 Hz.

## 5. Process lifecycle

1. `app` starts logging, opens `SettingsDb`, restores last project (or crash-restore snapshot).
2. `DeviceRegistry` enumerates endpoints; `AssignmentStore` resolves saved assignments
   (see DEVICE_MANAGEMENT.md). Unresolved → `[OFFLINE]`.
3. `AudioEngine` opens one `DeviceStream` per distinct assigned endpoint, chooses the master,
   starts the tick. Unassigned channels still run (silence in), so routing stays valid.
4. UI builds; `HotkeyManager` registers bindings.
5. On exit: stop recording (finalise files), stop engine, save project, flush logs.

## 6. Command-line modes

| Flag | Purpose |
|---|---|
| `--verify-ui [--scale=1,1.5,2]` | Render every screen offscreen at each scale into `%LOCALAPPDATA%\PodcastForge8\verify\`, report layout/exception errors, exit code 0/1 |
| `--e2e [--seconds=N]` | Real WASAPI session on present devices + simulated channels; records, validates files, prints JSON report |
| `--list-devices` | JSON dump of the device registry |
| `--recover <session dir>` | Rebuild headers from `Journal.json` without opening the UI |

## 7. Security and privacy

- `asInvoker` manifest; nothing needs admin. The only per-machine thing is the installer.
- No network code, no telemetry, no process spawning, no shell execution.
- The keyboard hook is installed only while push-to-talk / cough bindings exist and only compares
  virtual-key codes against bindings; keystrokes are never stored or logged.
- Audio never leaves the machine except through output devices the user assigns.

## 8. Filesystem layout

```
%LOCALAPPDATA%\PodcastForge8\
  Settings.db          SQLite: settings, device identities, hotkeys, restore snapshot
  Logs\                podcastforge8-YYYYMMDD-N.jsonl (10 MB × 10)
  Diagnostics\         glitch reports (JSON)
  verify\              --verify-ui output
Documents\PodcastForge8\Projects\<Project>\
  Project.json
  Session_YYYY-MM-DD_HHMMSS\
    Audio\  CH01_Host1.wav … CH08_Guest6.wav, (Music.wav, System.wav, Remote.wav)
    Mix\    MainMix.wav
    Metadata\ Journal.json, Markers.json, Markers.csv, Session.json
```

## 9. Repository layout

```
CMakeLists.txt  CMakePresets.json  cmake/  third_party/ (JUCE, Catch2, sqlite — fetched, pinned)
src/{core,devices,engine,sources,dsp,routing,record,media,project,ui,app}/
tests/{unit,harness}/  installer/  docs/  tools/ (Inno Setup, gitignored)
```
