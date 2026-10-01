# Recording

## 1. Tracks

| Track | Content | Default |
|---|---|---|
| 1–8 | Channel n post-DSP, pre-fader, mono | on (per-channel REC arm) |
| 9 | Main mix post-master, stereo | on |
| 10 | System loopback (WASAPI loopback of a chosen render endpoint), stereo | off |
| 11 | Music, stereo | off |
| 12 | Remote guests, mono | off |

Option per channel: record raw (pre-DSP) instead of post-DSP; option globally: also record a raw
safety copy of every channel.

## 2. Formats

WAV (RIFF, RF64 above 4 GB), Broadcast WAV (WAV + `bext` chunk: description, originator
"Podcast Forge 8", origination date/time, time reference in samples since midnight, UMID empty,
coding history; plus `iXML` with track name/channel), FLAC (JUCE `FlacAudioFormat`, level 5).
Bit depth 16 / 24 / 32-float. Default 48 kHz / 24-bit WAV.

## 3. Pipeline

```
tick ──► RecordRing (lock-free, pre-allocated, ≈ 2 s × all tracks)
             │  frames + block timestamp + track mask
             ▼
       Recorder worker (wakes every 50 ms or on half-full)
             │ de-interleave per track, convert to target format
             ▼
       TrackWriter[n]  ──► 1 MB write buffer ──► WriteFile (FILE_FLAG_SEQUENTIAL_SCAN)
             │
       every 2 s: patch RIFF/data sizes (WAV/BWF), FlushFileBuffers, update Journal.json (atomic
       write: tmp + MoveFileEx REPLACE_EXISTING|WRITE_THROUGH)
```

If the ring would overflow (disk too slow), the tick **never blocks**: it drops the newest block
for the recorder only, counts `recordDroppedSamples`, and the UI shows a red "RECORDING DROPOUT"
banner with the count. Live audio is never affected.

## 4. Session layout and naming

```
<ProjectDir>\Session_YYYY-MM-DD_HHMMSS\
   Audio\CH01_<name>.wav … CH08_<name>.wav, System.wav, Music.wav, Remote.wav
   Mix\MainMix.wav
   Metadata\Journal.json, Markers.json, Markers.csv, Session.json
```

- The session folder name includes seconds; on a collision, `_2`, `_3`… is appended.
- Files are created with `CREATE_NEW`; `ERROR_FILE_EXISTS` → next suffix. Code never opens an
  existing audio file for writing, except Recovery patching the header of a journaled file.
- Channel names are sanitised for the filesystem (reserved names, `<>:"/\|?*`, trailing dots).
- Pause/resume continues the same files and adds a `pause` marker. Stop/start = a new session folder.

## 5. Journal and recovery

`Journal.json`:
```json
{ "version": 1, "state": "recording|stopped|finalised",
  "sampleRate": 48000, "startedUtc": "...", "prerollSamples": 480000,
  "tracks": [ { "file": "Audio/CH01_Host1.wav", "format": "wav", "bits": 24, "channels": 1,
                "headerBytes": 44, "samplesWritten": 123456789, "lastPatchUtc": "..." } ] }
```

- On clean stop: final header patch, journal `finalised`.
- On startup, any session in the recent-projects list whose journal isn't `finalised` → the
  **Recovery** dialog: for each file, compute frames from the real file length (not the journal, which
  may lag by up to 2 s), truncate a partial trailing frame *in a copy-free way* (only the header's
  data size is changed, bytes are never removed), rewrite the header, mark it `recovered`.
  FLAC: the stream is re-scanned; if it can't be finalised, the WAV safety copy (if enabled) is used.
- Recovery never deletes or overwrites audio samples. It writes `Recovery.log` next to the journal.
- `--recover <dir>` runs the same code headless.

## 6. Disk safety (DiskGuard)

- Before arming: required bytes/min = Σ tracks × rate × bytes × channels. Show estimated recording
  time available. If < 30 min → warning dialog; if < 2 min → strong warning (the user can still record).
- During recording: free space polled every 2 s (`GetDiskFreeSpaceEx`) on the worker; the top bar
  shows the remaining time. < 10 min → amber banner, < 2 min → red flashing banner + sound on the
  monitor bus (optional). **Never stops recording by itself.**
- Write errors (`ERROR_DISK_FULL`, `ERROR_HANDLE_EOF`, device removed): the writer keeps the
  unwritten buffer, raises a modal non-blocking alert, retries every second, and after 10 s offers
  "Continue recording to another folder" which opens new files (`_part2`) there. Existing files are
  finalised (header patched to what was written) — never deleted.

## 7. Pre-roll

`PreRollBuffer`: per-track circular float buffer of `N` seconds (5/10/30/60, transport bar), allocated
when the user enables pre-roll and freed when it is turned off (the engine takes it out of the tick
and waits out an in-flight tick before freeing). It holds every recordable track whether armed or
not — CH1–CH8 (mono), Main and Music (stereo): 12 channels, 60 s × 48 kHz × 4 bytes ≈ 138 MB; the
tooltip shows the cost.

Contiguity: the tick reads the tap state once per block. On the first block the tap is active, the
buffer *freezes instead of taking that block*, and the block goes to the tap — so the last pre-roll
frame and the first live frame are adjacent samples (tested sample-exactly with a counter signal).
`Recorder::start` waits for that block (≤ 500 ms; without a tick it records without pre-roll and
logs it), so the journal's `prerollSamples` and a `Record pressed` marker at `N` s are right from
the start. The worker writes the frozen frames first, then drains the live stream; the tap ring is
enlarged while it does. On STOP the buffer restarts empty. Marker times and the timecode are
relative to the start of the file, so the pre-roll starts at 00:00:00.

## 8. Markers

`MarkerList`: `{ id, samplePos, timecode, label, colour, createdUtc }`. F12 creates one at the
current record position with the label "Marker n" (editable afterwards). Stored in
`Metadata/Markers.json` (written immediately on each change) and in `Project.json`. Export:
CSV, Audacity labels (`.txt`), Adobe Audition CSV, REAPER `.csv`, and BWF cue chunks written into
the Main mix file on finalise.
