---
phase: 11-pitch-harmony
plan: 02
subsystem: midi
tags: [bass, transpose, pattern-player]

requires:
  - plan: 11-01
    provides: FeatureVector pitch fields + policy
provides:
  - Bass-only semitone offset at MIDI emit time
affects: []

tech-stack:
  added: []
  patterns: [Clamp offset ±24; reset clears offset]

key-files:
  created: []
  modified:
    - src/midi/PatternPlayer.h
    - src/midi/PatternPlayer.cpp
    - src/AccompanimentProcessor.cpp

key-decisions:
  - "Offset = round(pitchRootMidi) - 40 when confidence ≥ kMinPitchConfidence; else 0."

requirements-completed: [PITCH-01, PITCH-03]

duration: —
completed: 2026-04-17
---

# Phase 11 — Plan 02 Summary

**`PatternPlayer::setBassSemitoneOffset` applies only to bass channel; `AccompanimentProcessor` sets offset from post-policy `FeatureVector` before `process()`.**

## Self-Check: PASSED

## Files Modified

- `src/midi/PatternPlayer.{h,cpp}` — offset API, `reset()` clears offset, emit path uses `outNote`.
- `src/AccompanimentProcessor.cpp` — computes offset from `fv` before pattern playback.
