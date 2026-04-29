---
status: complete
phase: quick-260427-t42
plan: 01
subsystem: midi
tags: [juce, midi, patternplayer, onset-detection, drum-timing, count-in]

# Dependency graph
requires:
  - phase: quick-260420-421
    provides: tempo and bass stability fixes (PatternPlayer, AccompanimentProcessor)
provides:
  - 4-onset count-in gate before drums fire on pattern entry
  - 2-bar minimum hold on drum pattern index switches
  - snapToBarStart() utility on PatternPlayer
  - Version 0.3.4
affects: [drum-timing, pattern-switching, feel, humanization]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Count-in gate: accumulate N onsets before allowing MIDI output; reset on silence re-entry"
    - "Hold guard: plain int64_t sampleTimestamp comparison for minimum-duration pattern lock (same pattern as bass 2-bar hold)"
    - "Display vs. commit separation: displayPatternIndex updated freely for UI; latestPatternIndex gated by hold expiry"

key-files:
  created: []
  modified:
    - src/midi/PatternPlayer.h
    - src/midi/PatternPlayer.cpp
    - src/AccompanimentProcessor.h
    - src/AccompanimentProcessor.cpp
    - CMakeLists.txt

key-decisions:
  - "snapToBarStart() resets beatPosition and pendingPatternIndex only — does NOT call reset() which would zero sampleCounter and generative bass state"
  - "countInComplete and countInOnsetCount are plain (non-atomic) types: written and read exclusively on the audio thread"
  - "lastDrumPatternChangeSample is plain int64_t (not atomic): inference thread writes it; audio thread resets it only during SILENT state when no MIDI is playing — stale read is at most one 20ms cycle (same risk accepted for lastBassUpdateSample)"
  - "displayPatternIndex updated on every inference cycle (unthrottled) so the UI Pattern: readout reflects inference intent even while the audible switch is held"

requirements-completed: [TIMING-01]

# Metrics
duration: 12min
completed: 2026-04-27
canonical_copy: 260427-t42-SUMMARY.md
---

Full narrative (same as **`260427-t42-SUMMARY.md`**): four-onset count-in, 2-bar drum hold, `snapToBarStart()`, v0.3.4 — implementation complete 2026-04-27.
