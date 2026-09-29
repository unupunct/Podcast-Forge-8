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

### DeviceStream
Wraps one `WasapiStream` (own event-driven IAudioClient, opened by endpoint ID; shared, low-latency shared via IAudioClient3, or exclusive) — or, in ASIO builds, a JUCE ASIO `AudioIODevice` — for one endpoint.
Direction: input, output, or both (headsets). Owns:

- `SpscRing<float>` per direction, sized `8 × max(deviceBlock, engineBlock) × channels`.
- `VarResampler` per direction (engine rate ↔ device rate × drift ratio).
- `DriftController`.
- Counters (atomics): xruns, underruns, overruns, dropped samples, callback period jitter.

The device callback only copies interleaved/non-interleaved samples to or from its ring.
For the **master** device the callback additionally invokes `AudioEngine::tick()` after pushing its
own input and before pulling its own output. This gives the master zero added latency.

### MasterClock
Selects the device whose callback drives the tick:

1. The user's explicit choice (Settings → Audio → Master clock), else
2. The first assigned headphone output that is online, else
3. The first online input, else
4. `InternalClock`: an MMCSS thread waiting on a high-resolution waitable timer
   (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`), ticking every engine block.

On master loss the control thread switches to the next candidate. The switch is posted as a command
and takes effect at a block boundary; the ex-master's rings are flushed, and all drift controllers are
reset to their current ratio (not to 1.0) so there's no pitch jump.

### VarResampler
Windowed-sinc polyphase resampler (32 taps, 256 phases, Kaiser β≈8.6, linear interpolation between
phases). The ratio can change every block. The ratio is smoothed (one-pole, τ≈2 s), so the pitch
change from a correction is inaudible (max slew ~2 ppm/s). Passband flat to 20 kHz at 48 kHz.
Latency: 16 samples. All state is pre-allocated.

### DriftController
Per non-master stream. Error signal = (ring fill − target fill) in samples, averaged over ~0.5 s,
cross-checked against device and engine positions from QPC-stamped callbacks (`IAudioClock`
position where available). PI loop:

```
ratio = nominal × (1 + Kp·e + Ki·∫e)   clamped to ±1000 ppm
```

Kp and Ki are tuned so a 200 ppm step settles within 30 s with no overshoot beyond ±0.25 blocks.
Status reported:

| Status | Condition |
|---|---|
| `Locked` | \|e\| < 0.5 block for 10 s |
| `Converging` | otherwise, within limits |
| `Unstable` | clamp hit, or estimated drift variance > threshold, or ≥3 xruns/min |
| `Offline` | device not running |

The UI shows each device's ppm, fill, status, and whether resampling is active (always active for a
non-master; for the master, "native").

### Ring target fill
`target = 1.5 × max(deviceBlock, engineBlock)`. Underrun (ring empty on pull) → output zeros, a short
fade-in on recovery, increment `underruns`, log event. Overrun → drop oldest, fade, increment `overruns`.

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

## 5. Latency budget (128 samples at 48 kHz, WASAPI exclusive)

| Stage | Samples | ms |
|---|---|---|
| Input device buffer | 128 | 2.67 |
| Input ring target (non-master) | 192 | 4.0 |
| Resampler | 16 | 0.33 |
| Engine block | 128 | 2.67 |
| Output ring target (non-master) | 192 | 4.0 |
| Output device buffer | 128 | 2.67 |
| **Total, mic → headphone on two non-master devices** | | **≈16 ms** |
| Mic → headphone on the master device (headset) | | ≈8 ms |

Shared mode adds the WASAPI engine period (typically 10 ms, 3 ms with `IAudioClient3`).

## 6. Failure handling

| Event | Behaviour |
|---|---|
| Endpoint removed | `HotplugWatcher` → control thread closes only that `DeviceStream`; source outputs silence; channel shows DISCONNECTED; nothing reassigned |
| Endpoint returns | Identity matched → stream reopened on the control thread → posted to engine → drift controller starts `Converging` |
| Device rate changes | Stream reopened at the new rate; resampler nominal ratio updated; warning shown; other streams untouched |
| Master lost | See MasterClock |
| Tick stall > 500 ms (watchdog) | Save project + snapshot; stop streams; recorder finalises headers of open files (they stay valid); rebuild engine; resume; write a diagnostics report |
| xrun/glitch | Counter incremented; event (device, time, fill, ratio, load) queued for Diagnostics |

## 7. Measurement

- Audio thread load = tick duration / block period, EWMA + max over 1 s.
- Process CPU from `GetProcessTimes`.
- Per stream: xruns, underruns, overruns, dropped samples, ppm, fill.
- Disk: recorder bytes/s, free space, estimated time remaining.
