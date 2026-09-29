# Routing

## 1. Sources and buses

**Sources** (`SourceId`, 12):

| Id | Source |
|---|---|
| 0–7 | Channel 1–8 (after the strip: post-fader for Main, selectable pre/post for others) |
| 8 | Music (post-ducker) |
| 9 | Carts (soundboard sum) |
| 10 | Talkback mic |
| 11 | Remote (sum of `NetworkAudioSource`s; silent for now) |

**Buses** (`BusId`, stereo each):

| Id | Bus | Feeds |
|---|---|---|
| Main | Programme mix | Master strip → Main output, RecordMix track |
| Clean | Main minus Music and Carts | Stream output "Clean Feed" |
| MusicOut | Music + Carts | Stream output "Music" |
| HP1–HP8 | Headphone mixes | Headphone device of channel N |
| PFL | Pre-fade listen sum | Monitor section |
| Monitor | Selected by the monitor section | Operator monitor output |

Record tracks come from taps, not buses: `Track n` = channel n post-strip, pre-fader (so the fader
can't ruin the isolated track). Track 9 = Main post-master. Tracks 10–12 = System loopback, Music,
Remote.

## 2. The matrix

`RoutingMatrix` holds `gain[source][bus]` as `AtomicParam<float>` (linear), plus
`pan[source]` and a pre/post flag per `(source, bus)`. At the start of each tick each cell's target is
read; the applied gain ramps linearly to the target over 10 ms (480 samples at 48 kHz).

```
bus[b].L += Σ_s  g[s][b] · panL(s) · src[s]      (mono sources)
bus[b].R += Σ_s  g[s][b] · panR(s) · src[s]
```

Pan law: −3 dB constant power (`cos/sin` of `(p+1)·π/4`). Stereo sources (music, carts) use a balance
law. The matrix mixes in a fixed order, so results are bit-reproducible, which the tests rely on.

Defaults for a new project:

| | Main | Clean | HP(self) | HP(others) | Record |
|---|---|---|---|---|---|
| CH n | 1.0 | 1.0 | 1.0 | 0.7 | tap |
| Music | 1.0 | 0 | 0.2 | 0.2 | track 11 |
| Carts | 1.0 | 0 | 0.5 | 0.5 | via Main |
| Talkback | **0 (locked)** | **0 (locked)** | per talkback target | — | **off** |

## 3. Headphone mixes

`HeadphoneMix` n has:
- `mode`: `Main` (bus = Main), `Personal` (matrix column HPn with the "self 100 / others 70 /
  music 20" template), `Custom` (the user-edited column).
- `volume`, `mute`, max-volume protection (a limiter at a user ceiling, default −6 dBFS).
- Switching mode crossfades over 20 ms.
- Its output device is the headphone endpoint assigned to channel n. Disconnected → mix keeps being
  computed and discarded; audio is never sent to a different device.

## 4. Mute, solo, PFL, cough

- **Mute**: channel gain → 0 on every bus (ramped). The record track is unaffected unless the "mute
  affects isolated track" option is on (default off; a muted channel is still recorded, so a
  mistaken mute doesn't lose audio). The cough mute has its own flag for this, also default off.
- **Solo** (solo-in-place on the Monitor bus only): when any channel is soloed, the Monitor bus
  (when monitoring Main) contains only the soloed channels. Main, headphones and recording are never
  affected.
- **PFL**: channel pre-fader signal is summed onto the PFL bus. When any PFL is active and "auto
  PFL" is on, the Monitor bus switches to PFL; releasing the last PFL returns it.
- **Cough mute** per channel: `PushToMute` (held = muted), `PushToTalk` (held = open), `Toggle`.
  Ramps 5 ms. Driven by `HotkeyManager` through atomics.

## 5. Talkback

- Source: any assigned input (usually a producer mic) or channel N's mic.
- Targets: any subset of HP1–HP8, or All. Target gain in the matrix is set only while the talkback
  key is held (or latched).
- While talking back, the target headphones are optionally dimmed by −12 dB ("talkback dim").
- Main/Clean/Record gains are 0 and locked unless *Settings → Routing → Talkback to recording*
  is on.

## 6. Monitor section

Sources: Main, PFL, HP1–HP8, Clean. Controls: volume, mute, dim (−20 dB default), mono sum,
max volume (limiter ceiling, hard-limited to the configured protection level).

## 7. Invariants (tested)

1. With default routing, an impulse on CH n appears on Main with gain `fader·panL/R`, on HPn at 1.0,
   and on HPm (m≠n) at 0.7, exact to 1e-6.
2. Talkback never appears on Main, Clean or any record track while the lock is on.
3. Solo/PFL never change Main, HP buses or record tracks (bit-exact).
4. Mute changes the record tap only when the option is on.
5. Gain changes produce no step discontinuity greater than the 10 ms ramp slope.
