---
phase: 31-architecture-deepening
plan: "02"
subsystem: analysis
tags: [refactor, tempo, hysteresis, testability, architecture]
dependency_graph:
  requires: []
  provides: [TempoStabiliser class, BPM hysteresis unit tests]
  affects: [AccompanimentProcessor, analysis layer]
tech_stack:
  added: []
  patterns: [value-member encapsulation, deadband hysteresis, hold-period commit]
key_files:
  created:
    - src/analysis/TempoStabiliser.h
    - src/analysis/TempoStabiliser.cpp
    - tests/test_tempo_stabiliser.cpp
  modified:
    - src/AccompanimentProcessor.h
    - src/AccompanimentProcessor.cpp
    - CMakeLists.txt
decisions:
  - "TempoStabiliser.update() updates stablePlaybackBpm internally (unlike original free function where caller stored return value) — enables getStableBpm() to be always current"
  - "3 reset() call sites: prepareToPlay, silence-timeout block, processBlockBypassed — all three member reset sites replaced"
  - "kTempoChangeDeadbandBpm and kTempoChangeHoldSec constants moved from AccompanimentProcessor.cpp anonymous namespace into TempoStabiliser private section"
metrics:
  duration_minutes: 12
  completed: "2026-05-03T08:02:00Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 3
  files_modified: 3
---

# Phase 31 Plan 02: TempoStabiliser Extraction Summary

**One-liner:** Extracted BPM deadband+hold hysteresis state machine from AccompanimentProcessor into standalone TempoStabiliser value member, enabling isolated unit testing of the hold-period commit logic.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Create TempoStabiliser class and unit tests | d8ba9a2 | TempoStabiliser.h, TempoStabiliser.cpp, test_tempo_stabiliser.cpp, CMakeLists.txt |
| 2 | Wire TempoStabiliser into AccompanimentProcessor | 798848e | AccompanimentProcessor.h, AccompanimentProcessor.cpp |

## What Was Built

`TempoStabiliser` is a plain value-type class (no heap allocation, no atomics) implementing the exact deadband + hold-period algorithm previously inlined in AccompanimentProcessor as a free function. It has three public methods:

- `reset()` — sets stable=120, pending=120, samples=0
- `update(candidateBpm, playbackOpen, numSamples, sampleRate)` — clamps to [80,220], applies EMA within deadband, accumulates hold period outside deadband, commits on timeout
- `getStableBpm()` — returns current stable BPM

AccompanimentProcessor loses the three private BPM-hysteresis fields and holds a `TempoStabiliser tempoStabiliser` value member instead. `tempoStabiliser.reset()` is called in three locations (prepareToPlay, silence-timeout block, processBlockBypassed). The `stabilizePlaybackBpm` free function and its two constants are deleted from AccompanimentProcessor.cpp.

## Test Coverage

7 new test cases in `tests/test_tempo_stabiliser.cpp` covering:
- Initial state (120 BPM)
- reset() restores 120 from any state
- Within-deadband EMA (candidate=124, delta=4, boundary case)
- Outside-deadband hold until 2s commit (130 BPM, 88200 samples at 44100 Hz)
- Pre-gate snap: playbackOpen=false returns candidate directly
- Large candidate change resets pending counter
- Out-of-range clamping (50→80, 300→220)

Full suite: **347 assertions in 93 test cases — all pass.**

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Completeness] Third reset() site in silence-timeout block**
- **Found during:** Task 2 implementation
- **Issue:** The plan's acceptance criteria listed >= 2 reset() calls (prepareToPlay + processBlockBypassed), but there was a third inline reset of all three fields in the silence-timeout path inside processBlock (the phrase-breath expiry path). This was the same three-field pattern and needed to be replaced.
- **Fix:** Added `tempoStabiliser.reset()` in the silence-timeout block, bringing total reset() call count to 3.
- **Files modified:** src/AccompanimentProcessor.cpp
- **Commit:** 798848e

## Threat Surface Scan

No new network endpoints, auth paths, file access patterns, or schema changes introduced. `TempoStabiliser::update()` is pure arithmetic — no heap, no atomics, no blocking (satisfies T-31-02-01 mitigate disposition from plan threat register).

## Self-Check: PASSED

- src/analysis/TempoStabiliser.h — EXISTS
- src/analysis/TempoStabiliser.cpp — EXISTS
- tests/test_tempo_stabiliser.cpp — EXISTS
- Commit d8ba9a2 — EXISTS
- Commit 798848e — EXISTS
- AccompanimentProcessor.h has 0 occurrences of stablePlaybackBpm, pendingTempoCandidateBpm, pendingTempoCandidateSamples — CONFIRMED
- AccompanimentProcessor.cpp has 0 occurrences of stabilizePlaybackBpm — CONFIRMED
- AccompanimentProcessor.cpp has 3 occurrences of tempoStabiliser.reset() — CONFIRMED
- AccompanimentProcessor.cpp has 1 occurrence of tempoStabiliser.update — CONFIRMED
- Full test suite: 347 assertions in 93 test cases all pass — CONFIRMED
