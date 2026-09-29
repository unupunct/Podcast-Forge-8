# Device Management

## 1. Enumeration

`DeviceRegistry` enumerates every endpoint through two layers:

1. **MMDevice API** (`IMMDeviceEnumerator`, all states: active, disabled, unplugged, not present):
   endpoint ID, friendly name, data flow (render/capture), state, form factor
   (`PKEY_AudioEndpoint_FormFactor`: microphone, headset, headphones, speakers, line, digital),
   `PKEY_AudioEngine_DeviceFormat` (native rate/channels), container ID (`PKEY_Device_ContainerId`).
2. **SetupAPI / device properties** of the parent device instance: instance ID (gives
   `USB\VID_xxxx&PID_xxxx\serial`), manufacturer (`DEVPKEY_Device_Manufacturer`), bus type (USB,
   HDAudio, PCI, Bluetooth, virtual).

JUCE's WASAPI type is used to *open* devices; it identifies them by name, so the registry maps
endpoint ID → JUCE device name, and resolves duplicate names (Windows adds "2- " prefixes) by
endpoint ID.

Supported sample rates are probed by `IAudioClient::IsFormatSupported` (exclusive) for 44.1, 48,
88.2, 96 kHz; shared mode reports the mix format.

`DeviceInfo` (shown in *Settings → Devices*):

| Field | Source |
|---|---|
| Name | friendly name |
| Manufacturer | device property |
| Type | USB microphone / USB headset / USB interface / built-in / HDMI / virtual / Bluetooth — derived from bus + form factor + capture/render pairing on the same container |
| Input / Output | data flow; a headset = one container with both |
| Sample rates, channels | probed |
| Device ID | endpoint ID |
| Status | Active / Disconnected / Disabled / Not present |

## 2. Stable identity

```
DeviceIdentity {
  endpointId      // "{0.0.1.00000000}.{guid}" – primary key
  flow            // capture | render
  containerId     // groups mic + headphones of one headset
  vid, pid, serial// from the USB instance ID (serial may be empty or port-derived)
  friendlyName
  lastSeen
}
```

Assignment is stored per channel: `mic[ch] = DeviceIdentity`, `hp[ch] = DeviceIdentity`.
Channels are **never** resolved by enumeration order.

### Resolution rules (at startup and on every device-arrival event)

For each channel with a saved identity, in order:

1. **Exact**: an active endpoint with the same `endpointId` → assigned.
2. **Fingerprint**: no exact match, and exactly one active endpoint with the same
   `flow + vid + pid + serial` (serial non-empty), not already used by another channel → assigned,
   and the stored `endpointId` is updated. Logged as "restored by fingerprint".
3. **Weak fingerprint**: same `flow + vid + pid + friendlyName` and no serial → **not** assigned
   automatically. The channel shows `[OFFLINE] — possible match: <name>` with a one-click confirm.
4. Otherwise → `[OFFLINE]`.

An endpoint is never assigned to two channels in the same role. Headsets: if the mic and the
headphones of a channel share a `containerId`, the pair is resolved together.

## 3. Hot-plug

`HotplugWatcher` implements `IMMNotificationClient` (`OnDeviceStateChanged`, `OnDeviceAdded`,
`OnDeviceRemoved`, `OnDefaultDeviceChanged` ignored). It runs on a COM MTA thread and only posts an
event to the device control thread; that thread re-enumerates (debounced 300 ms, since a USB headset
fires several events) and diffs.

| Change | Action |
|---|---|
| Assigned endpoint leaves | Close its `DeviceStream`; channel → DISCONNECTED; other channels untouched; engine keeps running; log `device.disconnect` |
| Assigned endpoint returns | Resolution rules → reopen stream → DriftController `Converging`; log `device.reconnect` |
| New unassigned endpoint | Shown in the Device Matrix pool; **never** auto-assigned |
| Master device leaves | MasterClock failover (AUDIO_ENGINE.md §3) |
| Device reports new format | Reopen that stream only; warn |

JUCE's own `AudioIODeviceType::scanForDevices` is called only on the control thread, never on an
audio or UI thread.

## 4. Device Matrix UI

A table with rows = channels 1–8 and columns Microphone | Headphones | Status, plus a pool of unassigned
devices. Drag a device from the pool (or from another cell, which swaps) onto a cell to assign it.
Status per cell: OK, OFFLINE, DISCONNECTED, POSSIBLE MATCH, IN USE BY OTHER APP (exclusive-mode
failure), RATE MISMATCH (resampled).

**Auto Assign**: fills only empty cells, pairing mic + headphones by container ID (headsets first,
then USB mics in registry order, then USB headphones). If any non-empty cell would change, a dialog
lists every change and requires confirmation. The default button is Cancel.

## 5. Exclusive mode

Per device, the user can choose shared (default) or exclusive. Exclusive failure
(`AUDCLNT_E_DEVICE_IN_USE`) falls back to shared *only if the user enabled fallback*; otherwise the
cell shows IN USE BY OTHER APP.

## 6. Persistence

SQLite table `device_identity`, and in `Project.json` a `devices` section, so a project moved to
another PC shows its channels OFFLINE until assigned there.
