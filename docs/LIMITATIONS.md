# Known limitations

Maintained as each stage lands. Windows-specific constraints first.

## Multiple independent USB audio devices

- **Every USB device has its own crystal.** Podcast Forge corrects drift continuously (variable
  resampling steered by a PI loop per device, ±1000 ppm range). It does not, and cannot, make the
  devices share one clock. Devices beyond ±1000 ppm are reported **Unstable**. Typical USB audio
  devices are within ±100 ppm.
- **Latency depends on the device periods Windows grants.** In WASAPI **shared** mode most devices
  run 10 ms periods, and each bridged device needs roughly 1.5–3 periods of buffering to absorb
  the packet/burst pattern safely. Mic → headphones across two shared-mode devices is ≈ 65–70 ms.
  In **exclusive** mode with 128-frame periods it is ≈ 15–20 ms. Exclusive mode takes the device
  away from other applications while Podcast Forge runs.
- **Low-latency shared mode (`IAudioClient3`)** is used when requested, but only drivers that
  support it grant periods below 10 ms. Most USB class-compliant devices do not.
- **The master device** drives the engine. Its own path adds at most one engine block. If it is
  unplugged, the next device takes over; the old and new master streams reopen (a short dropout on
  those two devices only). With no device at all the engine runs on an internal timer.
- **Drift correction takes 20–25 s to lock** after a device opens. During that time the correction
  can briefly reach up to 1000 ppm (1.7 cents — not noticeable on speech).
- **Sample alignment between different devices is not sample-exact.** Windows offers no clean
  per-sample hardware timestamps for USB audio (measured: WASAPI packet stamps wobble by up to
  ±15 ms), so each device's timeline is estimated from its callback times with a DLL. The mean
  alignment never drifts; it wanders by ≈ 2–3 samples (≈ 50 µs) with normal callback jitter and
  more when the system is heavily loaded. For multitrack editing this is far below anything audible
  (50 µs ≈ 1.7 cm of sound travel). Channels on the *same* device are always sample-exact.
- **Virtual devices make poor master clocks.** VB-Cable's timing is jittery (the live tests use it as
  master and still lock, but with more residual correction noise than a USB headset would give).

## Windows audio effects

- Shared-mode streams ask Windows to bypass the endpoint's audio effects (raw streams: no AGC,
  loudness equalisation, noise suppression or "enhancements" on mics and mixes; Settings → Audio,
  on by default). A driver that does not support raw mode keeps its effects; the Diagnostics page
  and the e2e report show "Windows FX" for such a stream. Exclusive mode always bypasses them.
- The Windows endpoint volume still applies in shared mode (it is not an effect); keep device
  volumes at 100 % for predictable levels.

## Audio engine settings

- Sample rate, engine block and WASAPI mode apply at the next start (the engine is not rebuilt
  while running).
- The EQ curve display is drawn for 48 kHz; at 44.1 / 96 kHz the audio is correct, only the drawn
  curve near the top octave differs slightly.

## Device identity

- Assignments are stored by Windows endpoint ID and restored only on an exact match, or on a
  USB serial match when the device moved to another port. Many cheap USB mics report **no serial**:
  two identical such mics can't be told apart, so a moved one shows **POSSIBLE MATCH** and waits
  for a one-click confirmation instead of guessing.
- The serial of composite devices (webcams, headsets with HID buttons) is read from the parent USB
  device node.

## Recording

- **FLAC and a full disk:** if the disk refuses a write in the middle of a FLAC frame, that frame's
  encoder state is lost; recording continues into a new `_part2` file. WAV/BWF keep every sample.
- Recovery rebuilds WAV/BWF/RF64 headers from the real file length; markers of an interrupted session
  are in `Metadata/Markers.json` (the cue chunk is only written when a recording stops normally).
- The System (loopback) and Remote tracks are reserved: they record silence until their sources
  exist (later stages).

## ASIO

- Optional build (`PF8_ASIO_SDK_DIR`); the SDK is not redistributable. ASIO opens **one** driver at
  a time, so it helps only with a single multi-channel interface, not with 8 separate USB devices.

## Virtual audio devices

- Podcast Forge ships **no kernel driver**. Stream outputs (Main / Clean / Music) are sent to any
  render endpoint, e.g. VB-Audio Virtual Cable, which OBS/Discord/Zoom can capture. A signed virtual
  driver would be a separate component behind the `VirtualAudioSource` interface.

## Test coverage vs real hardware

- The build machine has one USB microphone (Logitech BRIO), Realtek, NVIDIA HDMI and VB-Cable.
  8 × 8 operation, ±200 ppm drift and hot-plug are verified with the offline harness (simulated
  devices driving the real bridges and engine); real-device tests cover what is attached.
