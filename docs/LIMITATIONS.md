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
- **Drift correction starts smoothly but takes 10–20 s to lock** after a device opens. During that
  time the pitch correction can briefly reach a few hundred ppm (well under 1 cent — inaudible).

## Device identity

- Assignments are stored by Windows endpoint ID and restored only on an exact match, or on a
  USB serial match when the device moved to another port. Many cheap USB mics report **no serial**:
  two identical such mics can't be told apart, so a moved one shows **POSSIBLE MATCH** and waits
  for a one-click confirmation instead of guessing.
- The serial of composite devices (webcams, headsets with HID buttons) is read from the parent USB
  device node.

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
