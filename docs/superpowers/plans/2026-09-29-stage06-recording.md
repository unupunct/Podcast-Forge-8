# Stage 6 — Multitrack Recording

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Crash-safe multitrack recording exactly as RECORDING.md specifies: tracks 1–8 (isolated,
post-DSP pre-fader), 9 Main (post master limiter), 10–12 reserved (System / Music / Remote);
WAV / BWF / RF64 / FLAC at 16 / 24 / 32f; session folders; journal + recovery; disk guard; markers
with exports; never overwrite or delete.

## Architecture
```
tick ──► RecordTap (engine-owned, lock-free): one SpscRing per track, all-or-nothing push per block
            │  isolated tracks delayed by the master-limiter latency so they align with Main
            ▼
Recorder worker (50 ms cadence) ──► TrackWriter (WavWriter | FlacWriter) ──► IFileSink (Win32 | test)
            │ every 2 s: patch headers, flush, Journal.json (tmp + MoveFileEx WRITE_THROUGH)
            └ DiskGuard: free space, time remaining, write-error retry, "continue elsewhere" (_part2)
Recovery: journal not finalised → frames from real file length → header rewritten (bytes never removed)
```

## Files
```
src/record/RecordTap.h        track layout, per-track rings, flags, counters (header-only, RT-safe)
src/record/FileSink.h/.cpp    IFileSink; Win32FileSink (CREATE_NEW, sequential); FaultyFileSink for tests
src/record/SampleFormat.h/.cpp float → int16/24/32f with TPDF dither
src/record/WavWriter.h/.cpp   RIFF/WAVE, BWF bext + iXML, RF64 (ds64) switch, header patching, cue chunk
src/record/FlacWriter.h/.cpp  JUCE FlacAudioFormat into a CREATE_NEW file
src/record/Journal.h/.cpp     Journal model, JSON, atomic write
src/record/Recovery.h/.cpp    find unfinished sessions, recover headers, Recovery.log
src/record/Markers.h/.cpp     MarkerList, Markers.json/.csv, Audacity / Audition / REAPER exports
src/record/Session.h/.cpp     unique session folder, file-name sanitising, CREATE_NEW semantics
src/record/DiskGuard.h/.cpp   estimates and thresholds
src/record/Recorder.h/.cpp    state machine Idle/Recording/Paused/Stopping, worker thread
src/ui/TransportBar.*         REC / PAUSE / STOP / MARKER, timecode, disk time, dropouts
src/ui/MarkersView.*          dock tab: list, rename, export
src/app/CliModes.cpp          --recover <dir>
```

## Tests
- WAV/BWF/FLAC round trip (read back with JUCE readers), 16/24/32f within quantisation; bext fields;
  RF64 switch at an injected small threshold; cue chunk markers.
- Kill simulation: stop the worker mid-write without finalise → `recover()` → files valid, frame
  count = bytes written / block align, samples untouched.
- Disk full via FaultyFileSink at N bytes: recording continues into memory, alert raised, no file
  deleted, `continueElsewhere()` opens `_part2`, first part finalised to what was written.
- Overwrite safety: session and file collisions get suffixes; creating over an existing file fails.
- Engine push is all-or-nothing and allocation-free; isolated tracks align with Main sample-exactly.
- Markers JSON/CSV/Audacity/Audition/REAPER exports; journal round trip.
