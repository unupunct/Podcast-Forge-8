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
| Sync / drift | Devices at +200, −150, +80 ppm for 10 simulated minutes with callback jitter on every device → all `Locked`, no underruns, discontinuity detector clean on a 1 kHz sine, inter-device lag wander ≤ 4 samples (mean constant), 3 ms-jitter stress, DLL accuracy, no PI overshoot. Tuning sweep: `[.diag]` |
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

As built: 3 pages (Mixer, Device Matrix, Devices) × 7 configurations — 1920×1080 at 100/125/150 %,
2560×1440 at 100/150 %, 3840×2160 at 150/200 % — with eight long, realistic, OFFLINE device names.
Checks: zero-size visible components, children outside their parent, overlapping controls inside
console strips, button/label text wider than its bounds (labels explicitly marked ellipsis-OK, such
as device names with the full name in the tooltip, are exempt), knob dials under 36 px, fader travel
under 80 px, and self-drawn text via `LayoutSelfCheck`. A canary layout with three known defects
must produce three issues, otherwise the run fails.

## 5. End-to-end (`--e2e`)

`PodcastForge8.exe --e2e [--seconds=N] [--out=<dir>] [--loop-input=N] [--trace]` (default 60 s,
output `%LOCALAPPDATA%\PodcastForge8\e2e\`). It runs without a settings database, so the user's
saved assignments are neither used nor changed.

1. Real capture endpoints feed CH2…CH5; the other channels get simulated sines at -20 dBFS
   (CH6 is the talkback mic and is muted, so it is on no bus). **Real speakers and headphones are
   never used — no test tone is played out loud.** When VB-Cable is installed, HP8 → CABLE Input …
   CABLE Output → CH7 closes a real playback-to-capture loop (CH7 is muted on every bus so the loop
   can't feed Main; its pre-fader track is still recorded). HP8 is a Custom mix of CH1 only.
2. Records N s with 5 s pre-roll, a marker at 2 s, a generated cart at 3 s, a generated music file
   with ducking (and its own track), and a talkback press to HP8 from 4 to 6 s.
3. Verifies: journal finalised, no dropouts, pre-roll exactly 5 s, every file's length; simulated
   tones at -20.0 dBFS ±1 dB on their isolated tracks; CH1 on Main at the pan-law level (-23.01 dB
   ±0.5); cart on Main; talkback absent from Main (< -80 dBFS); music present and ducking engaged;
   the VB-Cable loop at the level predicted from the routing and the Windows endpoint volumes
   (±1 dB) and the talkback arriving on HP8; markers; per stream: sync state, xruns and whether
   Windows audio effects were bypassed; watchdog. Writes `e2e-report.json` (with the diagnostics
   report) and prints PASS / FAIL / WARN per check; exit code 0 = pass.

What the first real runs found (both fixed): (a) the loop through VB-Cable came back up to 3.9 dB
off, gliding over ~2 s after every level change — Windows' own effects on the endpoints (an AGC)
were processing our shared-mode streams; streams now request `AUDCLNT_STREAMOPTIONS_RAW`, and the
loop measures -23.05 dB for -23.01 expected. (b) A 44.1 / 96 kHz engine would have asked for the
wrong low-latency period (48 kHz was assumed).
