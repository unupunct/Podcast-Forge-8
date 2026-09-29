# Podcast Forge 8

A native Windows x64 console for podcast recording and monitoring: up to 8 independent USB
microphones, 8 independent USB headphone outputs, per-channel DSP, individual headphone mixes,
crash-safe multitrack recording, soundboard, music with ducking, talkback, and drift correction
across independently clocked USB devices.

> **Status: in development.** Stage 1 of 14 is done (shell, device enumeration, WASAPI backend).
> See [docs/CHANGELOG.md](docs/CHANGELOG.md).

## Documentation

| Doc | Contents |
|---|---|
| [ARCHITECTURE](docs/ARCHITECTURE.md) | Modules, threads, UI↔engine contract, file layout |
| [AUDIO_ENGINE](docs/AUDIO_ENGINE.md) | Multi-device engine, master clock, drift control, latency |
| [ROUTING](docs/ROUTING.md) | Routing matrix, headphone mixes, solo/PFL, talkback |
| [DEVICE_MANAGEMENT](docs/DEVICE_MANAGEMENT.md) | Stable device identity, hot-plug, Device Matrix |
| [RECORDING](docs/RECORDING.md) | Crash-safe recording, journal, recovery, pre-roll, markers |
| [DSP](docs/DSP.md) | Channel strip, presets, mic wizard, ducking |
| [TESTING](docs/TESTING.md) | Unit tests, engine harness, UI and end-to-end verification |

## Building from source

Requirements: Windows 10/11 x64, Visual Studio 2022 Build Tools (MSVC v143, Windows SDK,
"C++ CMake tools"), Git.

```powershell
.\scripts\fetch-deps.ps1     # JUCE 8.0.15, Catch2 v3.16.0, SQLite 3.53.4 into third_party/
.\scripts\gate.ps1           # configure + Release x64 build + tests
.\scripts\gate.ps1 -Live     # also run tests against real audio devices
build\release\bin\PodcastForge8.exe
build\release\bin\PodcastForge8.exe --list-devices   # JSON dump of all endpoints
```

Optional ASIO: download the Steinberg ASIO SDK yourself and configure with
`-DPF8_ASIO_SDK_DIR=<path>`. The default build is WASAPI only.

## Privacy

No telemetry, no network code, no audio leaves the machine. Logs go to
`%LOCALAPPDATA%\PodcastForge8\Logs\` and never contain audio.

## Licence

GNU Affero General Public License v3 — see [LICENSE](LICENSE). Podcast Forge 8 is built on
[JUCE](https://juce.com), used under its AGPLv3 open-source licence.

Copyright (C) 2026 unupunct.
