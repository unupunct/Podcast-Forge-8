# Audio Engine

## 1. Goals

1. Run 8 input and 8 output USB devices at once, each with its own clock.
2. Hold them in sync indefinitely with no audible correction.
3. Keep the tick real-time safe: no allocation, locks, I/O, logging or UI calls.
4. Survive devices vanishing, changing rate, or stalling, affecting only that device's path.

## 2. Formats

- Engine rate: 48 000 Hz (configurable 44.1 / 48 / 96 kHz). Engine block: 64/128/256/512.
- Internal sample type: `float` (32-bit). "24-bit processing" is exceeded; files are written at 24-bit
  integer (or 32-bit float if chosen) with TPDF dither on the integer conversion.
- Each device runs at its own native rate if it can't do the engine rate; the resampler bridges it.

## 3. Components

As built in Stage 2. Source: `src/engine/`.

### WasapiStream
One event-driven WASAPI stream (capture or render) opened **by endpoint ID** (never by name or
enumeration order). Shared, low-latency shared (`IAudioClient3`), or exclusive; float32 / int32 /
int24 / int16 device formats converted to float on the stream's MMCSS "Pro Audio" thread (FTZ/DAZ
set). Capture callbacks carry the hardware QPC time of each packet's first frame; render callbacks
carry the `IAudioClock` playback position and its QPC time. In ASIO builds a JUCE ASIO
`AudioIODevice` takes this role.

### Bridges (`Bridges.h`)
Each open endpoint has one bridge between its clock and the engine clock:

- `InputBridge` (device → engine) and `OutputBridge` (engine → device), each an SPSC ring of
  interleaved device frames + a `VarResampler` + a `DriftController`.
- **Device side** is called from the stream thread (or the test harness); **engine side** from the
  tick. They share only the ring and atomics.
- A multi-channel input bridge carries every device channel; a channel picks one input or the
  average of all. An output bridge carries one or more stereo pairs; mono devices get (L+R)/2.
- Counters (atomics): underruns, overruns, dropped frames, ppm, averaged fill, status.

**Fill measurement.** The quantity regulated is the *linearised* fill, not the raw ring count:

- input: `ring + deviceRate × (now − end of the last packet, in hardware time)`;
- output: `framesWritten − (framesTakenAtLastPacket + deviceRate × (now − that packet's hardware start))`.

Using hardware timestamps (not callback times) keeps both the device's packet staircase and
scheduler jitter out of the loop. Sampling the raw ring at engine ticks aliases the packet/block
beat into a slow false oscillation (≈ 13 s period at 200 ppm) that a PI loop would chase —
measured during development as ±90 ppm estimate error, gone with linearisation.

**Target fill** (device frames):
`(3·max(P, B) + min(P, B)) / 2 + 2 ms` where P = device period and B = the engine *burst*
(below), plus half a period on inputs (live shared-mode captures deliver a packet up to one period
after its last frame) and the device's own buffering on outputs.

**Priming and re-centring.** A bridge outputs silence until the linearised fill reaches the
target, then discards any excess once so it starts exactly centred, and fades in over 5 ms.
Without this, a device that started before its bridge joined the graph began hundreds of frames
high, which at the ±1000 ppm clamp takes > 10 s to drain (found in the live VB-Cable test).
Underrun → silence, re-prime, fade in; overrun beyond 4× target → drop to target.

### Master clock and the pull model
The master device's own bridge runs at the fixed nominal ratio (an exact zero-latency passthrough
when rates match). A **render master** asks its bridge for `n` device frames; the bridge calls
`tick(engineBlock)` until it holds `n`, so the engine runs in *bursts* of
`ceil(masterPeriod / engineBlock)` blocks per master callback and the master path adds at most one
engine block of latency. A **capture master** ticks whenever a whole block is available. Non-master
bridges size their targets for that burst.

Selection (`MasterClock.h`): user-preferred online endpoint → first online channel headphone
output → first online render → first online input → `InternalClock` (MMCSS thread on a
high-resolution waitable timer, absolute QPC scheduling).

`tick()` is guarded by an atomic try-flag: during a master hand-off a second caller skips instead
of re-entering. Changing the master reopens only the old and new master streams (their bridges are
rebuilt with the new role); other devices keep running.

### VarResampler
Polyphase windowed sinc: 48 taps, 256 phases with linear interpolation between phase rows,
Kaiser β = 8, cutoff 0.93 × min(1, 1/ratio) of the input Nyquist (flat to ≈ 19.8 kHz at 48 kHz,
≈ 80 dB stopband). Each phase row normalised to unity DC gain. The ratio correction is slew-limited
to 0.02 ppm per output frame (≈ 1000 ppm/s), so a correction is a glide, never a step. Latency
24 input frames (0 in master passthrough). All state pre-allocated.

### DriftController
Per non-master bridge. The linearised fill passes three cascaded one-pole filters (τ = 0.2 s each);
error e = filtered fill − target. PI: `c = Kp·e + Ki·∫e dt` with `Kp = 2ζωn/R`, `Ki = ωn²/R`,
ωn = 0.2 rad/s, ζ = 1 (R = device rate); clamped to ±1000 ppm with anti-windup.

| Status | Condition |
|---|---|
| `Priming` | waiting for the target fill |
| `Converging` | running, not yet within tolerance for 10 s |
| `Locked` | \|e\| < 0.5 engine block for 10 s (hysteresis: leaves only beyond 4×) |
| `Unstable` | clamp hit for > 2 s |
| `Native` | master bridge (no resampling correction) |

### Verified (Stage 2)
- Harness, 10 simulated minutes, three inputs at +200 / −150 / +80 ppm (periods 480 / 441 / 128)
  and outputs at +120 / −90 ppm, 500 µs callback jitter: estimates within 0.06 ppm, all Locked,
  zero underruns/overruns, no discontinuities, zero allocations on the audio path.
- Channels on three different devices (+180 / −200 / +60 ppm): arrival-time difference varies
  by ≤ 0.15 samples over 150 s after lock; click spacing exactly 48000.00 samples.
- Hot-plug: unplugging and replugging one mic leaves every other channel **bit-identical** to an
  undisturbed run.
- Live: Logitech BRIO mic + VB-Cable capture bridged to a VB-Cable render master, WASAPI shared:
  Locked within 15 s, zero underruns.

## 4. The tick

```
tick(nFrames):
  applyPendingCommands()                     // lock-free MPSC, bounded count
  for each input source s: s.pull(bufIn[s])  // resample from ring; silence if offline
  for ch in 0..7:  strip[ch].process(bufIn[ch])   // trim→HPF→gate→EQ→de-ess→comp→limit
  media.render(music, carts)                  // pre-decoded buffers
  ducker.update(speechLevel(main contributors))
  talkback.process()
  routing.mix(sources → buses)                // ramped gains
  master.process(Main)                        // master limiter, fader
  monitor.process(Main | PFL | HPx)           // dim, mono, mute, max-volume protection
  for each output stream o: o.push(bus[o])    // resample into ring
  recorder.push(tracks)                        // lock-free, never blocks
  preroll.push(tracks)                         // when enabled and not recording
  meters.publish()                             // seqlock snapshot
  loadMeter.update(QPC elapsed / block period)
```

Every buffer is allocated at engine start for the maximum block size (512) × max sources/buses.

## 5. Latency (measured targets, 48 kHz)

Per non-master bridge, from `bridgeTargetFill`:

| Configuration | Input bridge | Output bridge |
|---|---|---|
| Exclusive, 128-frame device periods, 128-frame master | ≈ 8.7 ms | ≈ 7.3 ms + device buffer |
| Shared mode, 10 ms device periods (480), 10 ms master | ≈ 28 ms | ≈ 23 ms + device buffer |

Plus resampler 0.5 ms and the device's own WASAPI buffering. The master path adds ≤ 1 engine block.
Mic → headphones across two non-master devices is therefore roughly 15–20 ms with small
exclusive-mode buffers but ≈ 65–70 ms with default 10 ms shared-mode devices — too much for
comfortable self-monitoring, which is why exclusive mode (or a headset as master) is recommended. The UI shows each device's period, mode and fill.
Lowest latency: exclusive mode, and use a headset's own output as the master.

## 6. Failure handling

| Event | Behaviour |
|---|---|
| Endpoint removed | `HotplugWatcher` → control thread closes only that `DeviceStream`; source outputs silence; channel shows DISCONNECTED; nothing reassigned |
| Endpoint returns | Identity matched → stream reopened on the control thread → posted to engine → drift controller starts `Converging` |
| Device rate changes | Stream reopened at the new rate; resampler nominal ratio updated; warning shown; other streams untouched |
| Master lost | Next candidate becomes master (only the old/new master streams reopen); internal clock if none |
| Tick stall > 500 ms (watchdog) | Save project + snapshot; stop streams; recorder finalises headers of open files (they stay valid); rebuild engine; resume; write a diagnostics report |
| xrun/glitch | Counter incremented; event (device, time, fill, ratio, load) queued for Diagnostics |

## 7. Measurement

- Audio thread load = tick duration / block period, EWMA + max over 1 s.
- Process CPU from `GetProcessTimes`.
- Per stream: xruns, underruns, overruns, dropped samples, ppm, fill.
- Disk: recorder bytes/s, free space, estimated time remaining.
