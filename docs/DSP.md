# DSP

All blocks process `float` mono blocks in place, have `prepare(sampleRate, maxBlock)` (the only place
memory may be allocated), `reset()`, and `process(float*, n)`. Parameters are read once per block from
atomics and smoothed. Bypass crossfades over 10 ms. Denormals are flushed (FTZ/DAZ set on every
audio thread).

## 1. Channel strip order

```
input trim (−24…+24 dB)
 → polarity invert (opt)
 → High-pass (20–300 Hz, 12 or 24 dB/oct, default 80 Hz / 12 dB)
 → Noise gate
 → 4-band parametric EQ
 → De-esser
 → Compressor
 → Limiter (default ceiling −1 dBFS)
 → [record tap]  → pan / fader / mute → routing
 → reverb send (post-fader, to a shared reverb returned into Main + HP buses)
```

## 2. Blocks

### Biquad
RBJ cookbook coefficients (TDF-II). Types: HP, LP, peak, low shelf, high shelf, band-pass.
Coefficients are recomputed at most once per block and interpolated if the parameter moved,
preventing zipper noise.

### High-pass
One or two cascaded Butterworth biquads (Q = 0.7071; 24 dB/oct = Q 0.5412 + 1.3066).

### Noise gate
Peak detector with a 1 ms look-ahead-free envelope; parameters: threshold (−80…0 dBFS), range
(−80…0 dB, default −40 = "downward expander"), attack (0.1–50 ms), hold (0–500 ms), release
(5–2000 ms), hysteresis 3 dB. The gain ramp is smoothed in the linear domain with attack/release time
constants. Sidechain HPF at 100 Hz, so rumble doesn't open it.

### Compressor
Feed-forward, log-domain, soft knee (0–12 dB). Level detector: RMS (10 ms) or peak, then
attack/release ballistics on the gain-reduction signal. Parameters: threshold (−60…0), ratio
(1:1–20:1), attack (0.1–100 ms), release (10–2000 ms), knee, makeup (0–24 dB), auto-makeup option.

```
gr(x) = 0                                  x < T − W/2
      = (1/R − 1)(x − T + W/2)² / (2W)      |x − T| ≤ W/2
      = (1/R − 1)(x − T)                    x > T + W/2      (x, T in dB)
```

### 4-band parametric EQ
Band 1: low shelf / peak, band 2–3: peak, band 4: high shelf / peak. Each: frequency (20 Hz–20 kHz),
gain (±18 dB), Q (0.1–10). Per-band enable.

### De-esser
A split-band sidechain: band-pass (frequency 3–12 kHz, default 6.5 kHz, Q 2) → envelope (1 ms attack,
60 ms release). When above the threshold, a dynamic peak filter at the same frequency is attenuated by
up to `amount` dB (0–18), proportional to the overshoot, so only the sibilant band is reduced.

### Limiter
Look-ahead brick-wall limiter: 1.5 ms look-ahead (pre-allocated delay line), peak hold, 50 ms
release, ceiling −12…0 dBFS (default −1). True-peak option: 4× oversampled peak detection on the
master. The limiter adds latency: the 1.5 ms is applied to every channel equally so tracks stay aligned
with each other (the latency is constant and reported).

### Reverb send
One shared JUCE `juce::dsp::Reverb` (room preset) fed by per-channel post-fader sends; the return goes
to Main (default −∞ = off) and optionally HP buses. Off by default.

### Master
Master fader, master limiter (default −1 dBFS ceiling, true-peak on), mute.

## 3. Presets

### Compressor

| Preset | Threshold | Ratio | Attack | Release | Knee | Makeup |
|---|---|---|---|---|---|---|
| Speech | −18 dB | 3:1 | 10 ms | 120 ms | 6 dB | +4 dB |
| Podcast | −20 dB | 4:1 | 5 ms | 100 ms | 6 dB | +6 dB |
| Aggressive Voice | −24 dB | 8:1 | 2 ms | 60 ms | 3 dB | +9 dB |
| Soft Voice | −16 dB | 2:1 | 20 ms | 200 ms | 10 dB | +3 dB |
| Radio | −26 dB | 6:1 | 1 ms | 50 ms | 4 dB | +10 dB |

### EQ (band: type f / gain / Q)

| Preset | B1 | B2 | B3 | B4 |
|---|---|---|---|---|
| Male Voice | LS 120 / +1.5 / 0.7 | Pk 300 / −2.5 / 1.2 | Pk 3.5k / +2.5 / 1.0 | HS 10k / +1.5 / 0.7 |
| Female Voice | LS 180 / +1.0 / 0.7 | Pk 400 / −2.0 / 1.2 | Pk 5k / +2.0 / 1.0 | HS 12k / +2.0 / 0.7 |
| Deep Voice | LS 100 / −1.5 / 0.7 | Pk 250 / −3.5 / 1.0 | Pk 2.5k / +3.0 / 0.9 | HS 9k / +2.0 / 0.7 |
| Bright Voice | LS 150 / +2.0 / 0.7 | Pk 500 / −1.0 / 1.0 | Pk 6k / −2.0 / 1.5 | HS 11k / −1.5 / 0.7 |
| Radio Voice | LS 90 / +3.0 / 0.7 | Pk 350 / −3.0 / 1.0 | Pk 3k / +4.0 / 0.8 | HS 8k / +3.0 / 0.7 |

Default HPF with presets: 80 Hz (male/deep 70 Hz, female/bright 100 Hz).

## 4. Microphone wizard

Steps, each with a live meter:

1. **Detect**: the channel's mic stream must be running and delivering non-zero blocks; otherwise it
   reports "no signal / device offline".
2. **Noise floor**: "stay silent" for 5 s → A-weighted RMS in dBFS (10th percentile of 100 ms windows).
3. **Level**: "speak normally" for 10 s → peak dBFS, average (RMS of the speech-gated windows),
   clip count (samples ≥ −0.1 dBFS).
4. **Recommendation**: software trim so the speech average sits at −18 dBFS with a peak ≤ −6 dBFS;
   if trim > +18 dB or noise floor > −50 dBFS → advise raising the hardware gain / moving closer;
   if clipping → advise lowering the hardware gain (the software trim cannot fix clipping at the ADC).
5. **Configure**: HPF 80 Hz (100 Hz if the noise floor has a > 6 dB bias below 100 Hz), gate threshold =
   noise floor + 10 dB, compressor threshold = average + 2 dB with the Podcast preset, limiter −1 dBFS.

The wizard shows Noise Floor / Peak / Average / Recommended Gain, and applies nothing until the user
presses **Apply**. It never changes hardware gain. If the endpoint exposes a hardware volume range
(`IAudioEndpointVolume::GetVolumeRange`), an extra, separately confirmed button can set it.

## 5. Ducking

`Ducker`: the sidechain is the sum of the channels routed to Main (post-fader, pre-limiter),
RMS 20 ms. When above threshold (default −35 dBFS) for more than the attack time (50 ms) → music
gain ramps to −depth (default −15 dB) over the attack time; when below for the hold time (500 ms) →
returns over the release time (1.5 s). Hysteresis 3 dB.

## 6. Verification targets (tested)

- HPF −3 dB at fc ±2 %, 12/24 dB/oct slope ±1 dB one octave below.
- Peak EQ gain at fc ±0.1 dB.
- Compressor static curve within ±0.2 dB of the formula at 10 levels.
- Gate closes to range within attack+hold+release ±1 ms.
- Limiter output never exceeds the ceiling (sample peak) for a +20 dB overdriven sine and impulses.
- De-esser reduces a 7 kHz tone above threshold, leaves 1 kHz unchanged (±0.1 dB).
- No block allocates (RealtimeGuard).
