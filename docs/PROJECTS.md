# Projects, crash restore and hotkeys

## 1. Projects

A project is a folder (default `Documents\PodcastForge8\Projects\<name>`):

```
<Project>\Project.json                 settings, carts, playlist, session index
<Project>\Session_YYYY-MM-DD_HHMMSS\    one folder per recording (RECORDING.md)
```

`Project.json` (`"format": "PodcastForge8.Project"`) holds `state`, which `project::captureState`
builds and `project::applyState` restores. Each parameter group (channel DSP, master, routing
matrix, headphone mixes, monitor, talkback, ducking) is described by one `visit…` function used for
both directions, so save and load cannot drift apart. Covered: device assignments and channel names
(re-resolved by identity on another PC), DSP, record arms, routing, headphones, monitor, master,
talkback (incl. key mode), recording format / depth / Main / Music track, pre-roll length, all 24
carts (file, name, colour, hotkey, volume, fades, loop), the music playlist, volume, auto-advance and
ducking. Solo / PFL and the talkback key state are deliberately not saved.

Loading never trusts the file: wrong types, out-of-range enums and non-finite numbers are ignored
(the current value stays); missing cart / music files are listed for the user.

| Action | Behaviour |
|---|---|
| New | New folder (never reuses an existing one: `Name (2)` …); keeps devices, DSP and routing, starts with an empty soundboard and playlist |
| Open | Picks a `Project.json`; refused while recording |
| Save | Atomic: `Project.json.saving` is written and flushed, then replaces the old file (`MoveFileEx … WRITE_THROUGH`) |
| Save As | New project folder with the current state; recordings stay with the original project |
| Archive | Zips the whole folder (audio stored, metadata deflated) to a new `.zip`; never overwrites; the project is untouched |

Paths and names are UTF-8 everywhere (`paths::utf8` / `paths::fromUtf8`): `path::string()` throws
for characters outside the ANSI code page, e.g. Romanian ș / ț on an English Windows (tested).

## 2. Crash restore

Every 3 s the working state is compared with the last snapshot; when it changed it is written to
`Settings.db` (`restore.snapshot`, with the project folder). `app.running` is `1` while the app runs
and `0` after a clean exit. If it is still `1` at start, the previous run ended abnormally and the
snapshot is applied over the reopened project, with a note asking the user to save. Interrupted
recordings are offered for header recovery as before (RECORDING.md).

The title bar and the PROJECT field show `*` while there are unsaved changes; quitting asks to
save them, and a running recording is only stopped (and finalised) after confirmation.

## 3. Hotkeys

| Default | Action |
|---|---|
| F9 | Record / resume |
| F10 | Stop (asks for confirmation) |
| F11 | Pause / resume |
| F12 | Marker |
| Space | Record start / stop (stop still asks) |
| F8 | Talkback (hold, or tap to latch — TALKBACK tab) |
| Num 1 … Num 8 | Cough CH 1–8 (push-to-mute by default; push-to-talk or toggle per channel) |
| per cart | the hotkey set on the cart (soundboard right-click) |

Bindings and cough modes are per machine (`Settings.db`, key `hotkeys`); cart hotkeys travel with
the project. A chord bound twice is reported as a conflict.

- **Focused** (default): a `WH_KEYBOARD` hook on the UI thread — only while Podcast Forge 8 is the
  active app. Ignored while typing in a text field or while a dialog is open; key-ups are always
  processed and losing focus releases every hold, so a cough or talkback key can never stay stuck.
  Auto-repeat never re-triggers.
- **Global** (per binding): press actions use `RegisterHotKey` (a chord another program already
  owns is reported, not stolen); hold actions use a `WH_KEYBOARD_LL` hook that exists only while
  such a binding exists, never swallows keys and ignores them while our app is active.
- Only virtual-key codes are compared with the bindings; keystrokes are never stored or logged.
