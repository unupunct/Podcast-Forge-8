# Stage 4 — Mixer UI, Device Matrix, --verify-ui

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** The main window looks and works like a hardware podcast console: 8 vertical channel
strips + MASTER, a top bar, a bottom dock (Routing grid), a Device Matrix window with drag-and-drop
and confirmed Auto Assign, and an offscreen UI self-test.

## Layout
```
TopBar (56 px @1x)
┌ Mixer ───────────────────────────────────────────────────────────────┬ MASTER ┐
│ CH1 … CH8 strips (equal width)                                        │        │
└───────────────────────────────────────────────────────────────────────┴────────┘
Bottom dock (tabs): ROUTING | (HEADPHONES, SOUNDBOARD, MUSIC, MARKERS arrive with their stages)
Menu: File · View (Device Matrix, Settings, Diagnostics) · Help
```
Strip (top → bottom): header (CH n, editable name) · input device + status LED · GAIN knob
(input trim −24…+24 dB) · GATE/COMP/EQ/DE-ESS toggles (bound to DSP params; processing in Stage 5)
· PAN knob · MUTE / SOLO / PFL · fader (−∞…+10 dB) + input meter (peak, RMS, hold, clip latch) ·
REC / MON.
Master: stereo Main meter, master fader, LIMITER (Stage 5), MUTE, monitor output selector,
monitor volume, DIM, MONO.

## Files
```
src/ui/Widgets.h/.cpp         Knob, Fader, Meter (mono/stereo), ToggleLed, colours per function
src/ui/ChannelStripView.*     one strip, bound to engine/routing params
src/ui/MasterStripView.*
src/ui/MixerView.*            8 strips + master; scales to any width
src/ui/RoutingGridView.*      sources × buses matrix of clickable gain cells
src/ui/DeviceMatrixView.*     rows = channels, columns Mic | Headphones | Status, device pool, drag & drop, Auto Assign
src/ui/VerifyUi.*             --verify-ui: offscreen render at 3 resolutions/scales + layout checks
src/devices/AutoAssign.*      proposal: fills only empty cells (headsets paired by container), lists every change
src/engine/*                  per-channel input trim, REC arm, DSP enable flags; bus outputs (monitor) in
                              Assignments + reconcile
```

## Tests
- `AutoAssign`: fills empty cells only; headset mic+headphones to the same channel; never proposes a
  change to an assigned cell; skips endpoints already used; deterministic order (USB first).
- Assignments JSON round-trips bus outputs.
- `--verify-ui` (gate from this stage): every screen at 1920×1080 ×1, 2560×1440 ×1.25,
  3840×2160 ×2 → no zero-size visible component, no child outside its parent, no label/button
  text wider than its bounds, no exception; PNGs in %LOCALAPPDATA%\PodcastForge8\verify\.
