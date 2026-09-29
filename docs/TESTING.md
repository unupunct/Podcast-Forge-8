# Testing

## 1. Layers

| Layer | Tool | Runs where |
|---|---|---|
| Unit | Catch2 v3 (`pf8_tests.exe`) | every stage gate, no devices needed |
| Engine harness | `EngineHarness` inside `pf8_tests.exe` (tag `[harness]`) | every stage gate, no devices |
| UI verification | `PodcastForge8.exe --verify-ui` | from Stage 4 |
| End-to-end | `PodcastForge8.exe --e2e` on the build machine's real devices + simulated channels | Stage 13+, and before release |

Stage gate command (all must pass):
```
cmake --preset release && cmake --build --preset release
ctest --preset release          # unit + harness
build\release\PodcastForge8.exe --verify-ui   # from Stage 4
```

## 2. EngineHarness

Runs the real `AudioEngine::tick()` offline with `FakeDeviceStream`s instead of WASAPI:

- `FakeDevice { rate, blockSize, ppmOffset, jitterUs, dropoutAt[], signal }`: an offline scheduler
  advances simulated time and calls each fake device's callback when its own clock says so, so ±ppm
  drift and callback jitter are reproduced exactly.
- Signals: `Sine(f, dBFS)`, `PinkNoise(seed)`, `Impulse(at)`, `Silence`, `Sweep`.
- Captures every bus, record track and output ring for analysis.
- Hooks: disconnect/reconnect a device at time t, change its rate, stall the tick, make the disk fail.

Analysis helpers: RMS/peak, Goertzel magnitude at f, cross-correlation lag, discontinuity detector
(max |second difference| vs signal expectation), THD+N.

## 3. Test catalogue (maps to brief §32)

| Area | Tests |
|---|---|
| Device enumeration | Registry built from a fake MMDevice snapshot: types classified (USB mic, headset by container, HDMI, virtual); duplicate names resolved by endpoint ID; smoke test on the real enumerator (tag `[live]`, skipped if no devices) |
| Device assignment | Exact match; fingerprint match updates the ID; weak fingerprint → POSSIBLE MATCH, not assigned; no endpoint on two channels; Auto Assign fills only empty cells and reports conflicts |
| Reconnection | Harness: remove CH3 mic at 5 s, return at 8 s → CH3 silent in between, other channels bit-identical to a run without the event, CH3 restored to the same channel |
| Routing | Invariants 1–5 from ROUTING.md with impulses; headphone modes; talkback lock |
| Gain / mute / solo / PFL | Exact gains; ramps have no step > slope; solo/PFL leave Main bit-exact |
| DSP | The verification targets in DSP.md §6; presets load the documented values |
| Sync / drift | Devices at +200, −150, +80 ppm for 10 simulated minutes → all `Locked`, fill within ±0.5 block, no underruns, discontinuity detector clean on a 1 kHz sine, and a correlation lag between tracks that is constant ±1 sample |
| Recording | Written WAV/BWF/FLAC read back by JUCE equal the ring input (24-bit tolerance); BWF `bext` fields; RF64 switch (simulated at a small threshold) |
| File recovery | Kill simulation: stop the worker mid-write without a finalise, then `Recovery` → files open, frame count = bytes written, samples untouched |
| Disk-full | Injectable `IFileSink` returns `ERROR_DISK_FULL` at N bytes → recording continues in memory up to the ring, alert raised, no file deleted, and a "continue elsewhere" call creates `_part2` |
| Overwrite safety | Session/file name collisions get suffixes; no code path opens an existing file with truncation (a test attempts it) |
| Pre-roll | Impulse at T−3 s with 5 s pre-roll → appears at exactly `(5−3)·rate` in the file |
| Project loading/saving | Round-trip of every field; version migration; corrupt JSON → error, the original is kept, the backup is loaded |
| Hotkeys | Binding parser; conflict detection; push-to-mute state machine driven by synthetic key events |
| Ducking | Speech burst → music at −depth within attack ±5 ms; returns after hold+release |
| Soundboard | Cart fade-in/out envelopes; routing; 24 simultaneous carts without allocation |
| RT safety | `RealtimeGuard` counts `operator new` calls on the tick thread across all harness tests → must be 0 |

## 4. UI verification (`--verify-ui`)

Builds every window and tab offscreen (`Component::createComponentSnapshot`) at 1920×1080 (×1),
2560×1440 (×1.25/1.5) and 3840×2160 (×2), with 8 simulated channels. Fails if any component has
zero size, overlaps a sibling unexpectedly, has text clipped (a label with a string wider than its
bounds), or throws. Writes PNGs to `%LOCALAPPDATA%\PodcastForge8\verify\` for optional manual review.
No desktop screen capture is ever used.

## 5. End-to-end (`--e2e`)

1. Enumerate real devices; assign every real capture endpoint present to channels 1…k and fill the
   rest with simulated sources (sine/pink); assign real render endpoints to headphones where present
   (VB-Cable captures one headphone mix back via its capture side, closing a real loop).
2. Run N seconds (default 60) with recording on, pre-roll 5 s, one marker, one cart, music with
   ducking, a talkback press.
3. Verify: files exist with the expected frame counts; the loopback through VB-Cable contains the
   injected tone at the expected level; talkback absent from MainMix; drift status per device;
   xrun counters; JSON report written.
