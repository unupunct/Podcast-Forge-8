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

## Stage 5 — DSP (2026-09-29)

**Added**
- `src/dsp`: RBJ biquads (TDF-II, coefficient interpolation), A-weighting, 12/24 dB Butterworth HPF,
  4-band parametric EQ (peak / shelves), noise gate (sidechain HPF, hysteresis, hold, range),
  feed-forward soft-knee compressor (RMS/peak detector, auto makeup), split-band de-esser, look-ahead
  brick-wall limiter with true-peak option, Freeverb-topology reverb, `ChannelStrip` chain with
  S-curve bypass crossfades and constant 72-sample latency, compressor and EQ presets from DSP.md,
  `MicAnalyzer`.
- Engine: per-channel strips before the record tap, mic-wizard analysis tap (raw input ring),
  shared reverb → routing source FX, true-peak master limiters on Main and Clean, strip meters.
- UI: `DspEditor` (CONFIG) with every parameter, presets, EQ/HPF response curve, live gate /
  de-esser / compressor activity; `MicWizard` (detect → noise → speech → results → Apply software
  settings; hardware gain is never touched).
- `--verify-ui` covers the DSP editor and the wizard (30 screens).

**Verified**
- 70 tests incl. DSP.md §6: HPF −3 dB at fc ±2 %, slopes, EQ gain at fc ±0.1 dB, compressor static curve
  ±0.2 dB at 10 levels, gate timing, limiter never above the ceiling (overdriven sines + impulse trains,
  sample and true peak), exact latency passthrough, de-esser selectivity, reverb decay, mic analysis and
  recommendations, zero RT allocations. Live (BRIO + VB-Cable, WASAPI shared): audio-thread load 3.7 %
  average, 11 % peak with 8 strips, routing, reverb and master limiters.

**Found and fixed during the stage**
- Limiter delay line one sample too long (73 vs 72): the delayed sample fell outside the look-ahead
  window protecting it. Caught by the latency-passthrough test.
- Linear bypass crossfade left a slope corner; replaced with a raised cosine.

## Stage 6 — Multitrack recording (2026-10-01)

**Added**
- `src/record`: `RecordTap` (per-track SPSC rings, all-or-nothing block push), `IFileSink` +
  `Win32FileSink` (CREATE_NEW only), `WavWriter` (WAV / BWF bext + iXML / RF64 promotion, header
  patching, cue + labels), `FlacWriter` (JUCE, through the same sink), TPDF-dithered conversion,
  `Journal` (atomic JSON), `Recovery` (header-only, never touches audio bytes), `MarkerList` with
  CSV / Audacity / Audition / REAPER exports, session folders with collision suffixes, file-name
  sanitising, `DiskGuard`, and `Recorder` (worker thread, 2 s checkpoints, write-error retry with
  audio held in memory, "continue elsewhere" into `_part2` files, pre-roll hand-off for Stage 11).
- Engine: isolated tracks (post-DSP, pre-fader) delayed by the master-limiter look-ahead so they are
  sample-aligned with the Main mix; Main recorded post limiter.
- UI: transport (REC / PAUSE / STOP with confirmation / MARKER, timecode, disk time, dropout and
  write-error banners, CONTINUE ELSEWHERE) in the tab row; MARKERS dock tab; start-up recovery
  prompt. CLI: `--recover <session>`.

**Verified**
- 83 tests + 3 live tests. Round trips in WAV/BWF/FLAC at 16/24/32f, RF64, cue/bext/iXML; crash
  recovery leaves every audio byte identical; disk-full → continue on another "drive" with every frame
  accounted for and part 2 continuing the signal exactly; nothing overwritten; tap all-or-nothing and
  allocation-free; isolated tracks align with Main sample-exactly. Live: a real 3-track session
  (BRIO, VB-Cable loop, Main) — all tracks exactly 240 000 frames, finalised, zero dropouts.

**Found and fixed during the stage**
- Disk full mid-frame: the torn frame's prefix was lost when continuing elsewhere → handed over.
- Recovery on a finalised file declared its trailing cue/label chunks as audio → kept consistent headers.
- Clock sync, revised from live measurements (systematic debugging): WASAPI packet timestamps wobble by
  up to ±15 ms on real devices → time bases are now DLL-filtered callback times on both sides of every
  bridge; start-up re-centring; PI retuned by a measured sweep. Live lock went from "sometimes 35 s,
  sometimes not in 40 s" to 20–25 s in 3/3 runs with zero underruns. TESTING thresholds for
  inter-device alignment were changed from 1 to 4 samples to match what the hardware allows
  (documented in LIMITATIONS.md).

## Stage 7 — Headphone mixes (2026-10-01)

**Added**
- Hearing protection: a brick-wall limiter (0.5 ms look-ahead, ceiling −6 dBFS by default,
  selectable 0 … −20 dB) on every headphone feed, and maximum-volume protection (−3 dBFS) on the
  operator monitor. Gain-reduction meters for both.
- HEADPHONES dock tab: eight personal mixes — mode (Main mix / Personal / Custom), volume, mute,
  protection ceiling, live PROTECT indicator, 12 draggable send bars (CH 1–8, Music, Carts, Remote,
  Reverb; double-click toggles), PERSONAL resets to the template. Sends switch to two compact
  columns when the dock is short.
- `--verify-ui` renders every bottom-dock tab (44 screens).

**Verified**
- +12 dB of overload into a headphone feed comes out at exactly the −6 dBFS ceiling (limited, not
  muted); the monitor ceiling holds. 84 tests, 44 UI screens.

## Stage 8 — Soundboard (2026-10-01)

**Added**
- `src/media`: `Soundboard` (24 carts; immutable pre-decoded buffers handed to the audio thread through
  atomics, freed only after the audio thread lets go; play/retrigger, 5 ms click-free stop, fade-out
  over the cart's time, fade-in, volume, loop) and `loadAudioFile` (WAV / MP3 / FLAC via JUCE,
  mono → stereo, rate conversion with the engine's resampler, 10-minute cart limit).
- `VarResampler` moved from `engine` to `dsp` (now shared by the engine and the file loader).
- Engine renders the soundboard into the routing source Carts (Main, headphones; recorded via Main).
- SOUNDBOARD dock tab: 24 pads (name, colour, remaining time, progress, loop, hotkey label), click /
  Shift-click fade / Ctrl-click stop, right-click settings, background loading, STOP ALL, FADE ALL.

**Verified**
- 89 tests incl. envelopes, retrigger/loop, 24 simultaneous carts with zero RT allocations, buffer
  lifetime across swaps, WAV 44.1k mono and FLAC decoding; 51 UI screens.

## Stage 9 — Music and auto-ducking (2026-10-01)

**Added**
- `src/media/MusicPlayer`: playlist streamed from disk by a decoder thread into a lock-free ring
  (4 s); play / pause / resume / next / fade-out / stop, 50 ms fade-in, 20 ms pause ramp, auto-advance,
  rate conversion with the shared resampler, flush handshake on track change, underrun counter that
  ignores the expected gap while a track opens.
- `dsp::Ducker`: RMS sidechain on the previous block's post-fader voice sum; threshold, depth, attack
  (must stay above threshold for the whole attack — no pumping on clicks), hold and gentle release.
- Engine renders music into the Music routing source with the duck gain; `musicDuckDb` meter;
  optional separate Music record track (`Recording settings → recordMusic`).
- MUSIC dock tab: playlist (double-click to play, durations, unreadable files flagged), transport,
  volume, AUTO NEXT, REC TRACK, DUCKING with threshold / depth / release and a live duck meter.

**Verified**
- 92 tests incl. ducking timing (attack, hold, release, off), zero RT allocations, playlist streaming
  across 48k / 44.1k files with pitch kept, pause silence, auto-advance without underruns, fade-out;
  58 UI screens.

## Stage 10 — Talkback (2026-10-01)

**Added**
- Dedicated talkback mic assignment (`Assignments::talkback`, saved/restored by identity like channel
  mics, Fingerprint re-binding, may share an interface with a channel) and an engine talkback route;
  or a channel's processed mic as the source. Mic trim, talkback level, `talkbackPeak` meter.
- `TalkbackKey`: hold-to-talk, tap-to-latch, momentary and latch modes (key repeat ignored).
- TALKBACK dock tab: big TALK button, key mode, source, mic picker with device state, input
  channel, trim / level, HP1–HP8 targets with ALL / NONE, dim, and the program lock with a clear
  LOCKED / OPEN note.

**Verified**
- 96 tests: key behaviour in all modes; harness — talkback mic reaches only the target headphones,
  never Main / Clean, disappears on release, zero RT allocations; channel-source talkback; assignment
  JSON round-trip including the talkback mic. 65 UI screens.

## Stage 11 — Pre-record buffer (2026-10-01)

**Added**
- `PreRollBuffer` (5 / 10 / 30 / 60 s) fed by the tick with every recordable track; freezes on the
  first recorded block so pre-roll and live audio are sample-contiguous; restarts empty after STOP.
- Engine `setPrerollSeconds` (safe hand-over from the tick before freeing); recorder writes the
  pre-roll first, adds a `Record pressed` marker, journals `prerollSamples`, enlarges the tap ring
  meanwhile. `Recorder::setPreroll(data)` replaced by `setPrerollSource`.
- Transport bar: PRE-ROLL selector (memory cost in the tooltip), fill indicator while idle,
  "incl. N s pre-roll" while recording.

**Verified**
- 99 tests: buffer wrap / freeze / release; every sample of every file continues a counter signal
  across the pre-roll → live joint (two takes); journal and marker positions; engine feeds it with
  zero RT allocations and runs on without it. 65 UI screens.

## Stage 12 — Projects, crash restore, hotkeys (2026-10-01)

**Added**
- `src/project`: `ProjectState` (capture / apply of every persistent setting through shared
  field lists), `Project` (create / atomic save / open / save-as / session index / zip archive),
  `Hotkeys` (actions, chord parser, defaults F9–F12 / Space / F8 / Num 1–8, conflicts, JSON,
  dispatcher with hold / repeat / focus-loss handling).
- UI: `ProjectController` (PROJECT menu in the top bar: New, Open, Save, Save As, Archive; unsaved
  marker; quit asks to save; crash-restore snapshot every 3 s + unclean-exit detection; one cart-
  loader thread joined on exit) and `HotkeyManager` (focused thread hook, RegisterHotKey, low-level
  hook only for global holds, cough state machines, cart hotkeys picked up live).
- Quitting while recording asks first, then finalises every file.

**Fixed**
- Recording with non-ANSI characters in the project or channel names (e.g. "Ședință") threw
  from `path::string()`; all path ↔ text conversions are now UTF-8 (`paths::utf8`, `fromUtf8`),
  file names are built from UTF-8 and truncated on character boundaries.
- Two test names contained non-ASCII punctuation that ctest could not match.

**Verified**
- 107 tests: chord round trips and rejects, defaults, conflicts, JSON; dispatcher holds / repeats /
  modifier release / focus loss; capture → apply → capture identical on a fresh engine; garbage
  values ignored and missing files reported; project create / unique folders / atomic save /
  reopen / sessions; archive contents and no-overwrite; Unicode recording regression. 65 UI
  screens; real-app start → quit smoke run clean (`app.running` reset).

## Stage 13 - Diagnostics, watchdog, settings, end-to-end (2026-10-01)

**Added**
- Tick watchdog (own thread): no engine tick for 500 ms while the control thread is idle -> error,
  diagnostics report, controlled audio restart (every stream closed and reopened; a recording keeps
  running), at most 3 restarts per minute. Glitch log (per-stream underruns / overruns, stalls,
  restarts; 500 entries) and a JSON diagnostics report (`Diagnostics\diagnostics-*.json`).
- DIAGNOSTICS tab: CPU, audio load, clock, ticks, watchdog, recorder throughput, a 256 MB
  write-through disk speed test (own temp file, removed), per-stream table (state, sync, ppm,
  fill/target, xruns, format, Windows effects), glitch log, SAVE REPORT, RESTART AUDIO.
- SETTINGS tab: Audio & performance (rate, block, WASAPI mode, Windows-effects bypass - next start),
  Devices (preferred master, rescan), Routing & monitoring, Recording & projects, Master DSP,
  Hotkeys (bindings, global, cough modes, conflicts, Windows registration errors), Appearance
  (interface size, live), Logging & privacy (debug log, live). `AppSettings` in Settings.db.
- Raw WASAPI streams (`AUDCLNT_STREAMOPTIONS_RAW`) so Windows AGC / enhancements never process
  the mics or mixes; reported per stream.
- `--e2e`: real-device end-to-end run (TESTING.md section 5), simulated sources, VB-Cable loopback,
  never plays out loud, JSON report.
- verify-ui renders every main tab and every settings page (128 screens).

**Fixed**
- Real loopback measured up to 3.9 dB off with a slow glide: Windows endpoint effects; fixed by raw
  streams (now -23.05 dB for -23.01 expected).
- Low-latency shared period assumed a 48 kHz engine.
- SeqLock test could fail under a loaded parallel run when the reader thread was never scheduled
  during the writes (test-design race; the seqlock itself was never torn).

**Verified**
- 111 tests (watchdog verdicts and rate limit, AppSettings round trip and rejects, 2-channel
  drifting input level, custom headphone pan level); 128 UI screens; `--e2e --seconds=30` on the
  real devices (Logitech BRIO + VB-Cable loop): all checks PASS, no warnings.
## Stage 14 - Installer, documentation, 1.0.0 (2026-10-01)

**Added**
- Inno Setup 6 installer (`installer/PodcastForge8.iss`): per-user by default (no admin), x64,
  Windows 10 1809+, AGPLv3 licence page, closes a running copy first, optional desktop icon;
  uninstall removes only the program - never projects, recordings, settings or logs.
- `scripts/package.ps1`: full gate, version check of the exe, installer and portable zip with SHA-256.
- Application icon (`scripts/make-icon.py`, dependency-free) and Windows version resource.
- README rewritten for 1.0; `docs/OBS.md` (OBS, Discord / Zoom, mix-minus for remote guests);
  LIMITATIONS updated (ASIO not in 1.0, reserved System / Remote tracks, unsigned installer).
- Version 1.0.0.

**Verified**
- Gate: 111 tests, 128 UI screens; live tests 3/3; `--e2e --seconds=60` on the real devices: PASS.
- Installer: silent per-user install, installed exe runs, silent uninstall removes the program and
  its uninstall entry while Settings.db and the logs stay.
## Final review (2026-10-01)

Two independent audits (real-time / threading, data safety / UI flows) of the whole code base;
every finding was checked against the code, fixed where real, and covered by a regression test that
was shown to fail on the old code where practical.

**Fixed - real-time and threading**
- A newly opened master (hotplug, reassignment, watchdog restart) could tick the engine about 3x
  (outputs) or up to 64x (inputs) faster than real time until the graph contained its bridge: a
  recording would get time-compressed garbage. Bridges are now "attached" by the tick; an unattached
  master drives at its nominal rate; the internal clock stops before a new master opens and the
  master joins the graph immediately. Verified on real devices with an audio restart in the middle
  of an e2e recording (files exactly real-time length).
- Ring pushes are whole frames only (an overrun on 3/6/10-channel devices rotated channels).
- Devices with more than 32 channels are refused (they overflowed the per-bridge buffers).
- Soundboard buffers are reclaimed by render epoch (use-after-free window when a cart was replaced).
- Graph publishing never waits for the tick (a stalled device could freeze the control thread with
  the state lock held); the internal-clock flag is atomic (status() raced on it).
- Music: the decoder re-validates the playlist after opening a file (out-of-bounds write when a
  track was removed meanwhile) and never drops input for files below ~24 kHz (audible skips).
- Shutdown can no longer reopen devices from a queued hotplug job; a stream stuck in a driver call
  is abandoned after 3 s instead of hanging the restart; SeqLock without a shared fallback copy;
  exact record-tap handshake at stop instead of a 30 ms sleep.

**Fixed - data safety and UI**
- Continue-elsewhere is all-or-nothing (a failure used to destroy the held audio).
- Stop / quit during a write error rescues the held audio to `%LOCALAPPDATA%\PodcastForge8\Rescue`
  (it used to be discarded); the dialogs say so.
- FLAC tracks hold their audio after a failed write (they used to drop everything after it).
- WAV re-aligns after a torn frame (a permanent false WRITE ERROR and no more checkpoints).
- Project.json is flushed to disk before replacing the old one, with Project.json.bak as fallback.
- Archives: refused above 4 GB (no ZIP64), verified after writing, never appended to a stale temp
  file.
- Hotkeys: global hold keys read the live modifier state; hotkeys keep working while a DSP editor or
  mic wizard is open (now non-modal windows); a forced release never latches talkback; a hold whose
  key-up went to another app is released; New Project keeps the carts if it fails.
- e2e: an audio restart mid-recording; loopback tone checks tolerant of drift convergence.

**Verified**
- 119 tests, 128 UI screens, live tests 3/3, `--e2e --seconds=60` twice: PASS, no warnings.