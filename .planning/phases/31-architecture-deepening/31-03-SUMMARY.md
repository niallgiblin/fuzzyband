---
phase: 31-architecture-deepening
plan: "03"
subsystem: analysis/gate
tags: [refactor, tdd, architecture, playback-gate, beat-snap]
dependency_graph:
  requires: [31-02]
  provides: [PlaybackGate class, GateDecision struct]
  affects: [AccompanimentProcessor, PatternPlayer dispatch, tempoStabiliser.reset]
tech_stack:
  added: []
  patterns: [value-object, decision-record, flag-dispatch]
key_files:
  created:
    - src/analysis/PlaybackGate.h
    - src/analysis/PlaybackGate.cpp
    - tests/test_playback_gate.cpp
  modified:
    - src/AccompanimentProcessor.h
    - src/AccompanimentProcessor.cpp
    - CMakeLists.txt
decisions:
  - "trulySilent expression simplified from (st==SILENT && !inPhraseBreath) || !playbackGateOpen to !gd.gateOpen — semantically equivalent because inPhraseBreath=false only occurs when gate is already closed"
  - "lastDrumPatternChangeSample reset added to gd.resetTrackers dispatch — field stays in AccompanimentProcessor but must reset on full silence timeout"
  - "processBlockBypassed activeNonSilentSamples=0 replaced with playbackGate.reset() for completeness"
metrics:
  duration_seconds: 548
  completed_date: "2026-05-03"
  tasks_completed: 2
  tasks_total: 2
  files_created: 3
  files_modified: 3
---

# Phase 31 Plan 03: PlaybackGate Extraction Summary

Extracted phrase-breath / beat-snap / active-fallback gate state machine from `AccompanimentProcessor` into a standalone `PlaybackGate` value class returning a `GateDecision` flag struct.

## One-liner

PlaybackGate extracts 8 private fields + ~72 lines of processBlock gate logic into a noexcept value class; AccompanimentProcessor dispatches GateDecision flags for armCrash, resetTrackers, snapBeatNow.

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Create PlaybackGate class, GateDecision struct, unit tests | b0bfb65 | PlaybackGate.h, PlaybackGate.cpp, test_playback_gate.cpp, CMakeLists.txt |
| 2 | Wire PlaybackGate into AccompanimentProcessor, remove extracted fields | f8018ae | AccompanimentProcessor.h, AccompanimentProcessor.cpp |

## What Was Built

### `src/analysis/PlaybackGate.h`
- `GateDecision` struct with four booleans: `gateOpen`, `snapBeatNow`, `armCrash`, `resetTrackers`
- `PlaybackGate` class with `reset()`, `update()`, `isGateOpen()` — all noexcept, no heap allocation
- Private constants: `kPhraseBreathHoldSec=2.0`, `kPlaybackConfidenceStart=0.35`, `kActiveFallbackStartSec=0.35`, `kBeatSnapTimeoutSec=2.0`
- Internal state: `prevStructureState` (PlaybackGate tracks its own SILENT→non-SILENT transitions)

### `src/analysis/PlaybackGate.cpp`
- SILENT branch: resets `silenceSamples` counter on first entry, accumulates, triggers full reset after >2s, sets `inPhraseBreath=true` during phrase breath window
- Non-SILENT branch: fires `armCrash` on phrase-breath re-entry, accumulates `activeNonSilentSamples`, computes `allowPlayback`, arms beat-snap on rising edge, resolves beat-snap at bar boundary or timeout/fallback

### `AccompanimentProcessor` changes
- 8 private fields removed: `playbackGateOpen`, `prevPlaybackGateOpen`, `silenceSamples`, `activeNonSilentSamples`, `inPhraseBreath`, `pendingBeatSnap`, `prevBeatPhase01`, `pendingBeatSnapSamples`
- `PlaybackGate playbackGate` value member added
- `prevStructureSilent` kept (not extracted — used for display/state at bottom of processBlock)
- `prepareToPlay()`: 8 individual resets replaced with `playbackGate.reset()`
- `processBlock()`: gate logic block replaced with `playbackGate.update(...)` + `GateDecision` dispatch
- `processBlockBypassed()`: `activeNonSilentSamples=0` replaced with `playbackGate.reset()`
- Constants `kPhraseBreathHoldSec`, `kPlaybackConfidenceStart`, `kActiveFallbackStartSec` removed from processor (moved into PlaybackGate)

### Tests
7 test cases, 20 assertions in `tests/test_playback_gate.cpp`:
- Phrase-breath hold (< 2s silence) → no resetTrackers, gate stays
- Full reset (> 2s silence) → resetTrackers=true
- Active fallback (> 0.35s non-silent, no lock) → gate opens
- armCrash fires on phrase-breath re-entry
- Beat-snap fires on phase wrap when pendingBeatSnap set
- reset() clears all state
- GateDecision default field values

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing critical functionality] lastDrumPatternChangeSample reset in resetTrackers dispatch**
- **Found during:** Task 2 — original code had `lastDrumPatternChangeSample = -1` inside the silence full-reset block; this field stays in AccompanimentProcessor but must be reset when trackers are reset
- **Fix:** Added `lastDrumPatternChangeSample = -1` to the `gd.resetTrackers` dispatch block in processBlock
- **Files modified:** `src/AccompanimentProcessor.cpp`
- **Commit:** f8018ae

**2. [Rule 1 - Simplification] trulySilent expression simplified**
- **Found during:** Task 2 — original `(st == StructureState::SILENT && !inPhraseBreath) || !playbackGateOpen` references extracted `inPhraseBreath` field
- **Fix:** Replaced with `!gd.gateOpen` — semantically equivalent because `inPhraseBreath=false` (the non-phrase-breath SILENT case) only occurs after the long-silence full-reset which also sets `playbackGateOpen=false`, making the `!inPhraseBreath` branch redundant
- **Files modified:** `src/AccompanimentProcessor.cpp`
- **Commit:** f8018ae

**3. [Rule 2 - Completeness] processBlockBypassed reset extended**
- **Found during:** Task 2 — `processBlockBypassed()` only reset `activeNonSilentSamples=0` from the extracted fields; other gate state (pendingBeatSnap, etc.) was left intact
- **Fix:** Replaced `activeNonSilentSamples=0` with `playbackGate.reset()` for full state consistency on bypass
- **Files modified:** `src/AccompanimentProcessor.cpp`
- **Commit:** f8018ae

## Known Stubs

None — all gate logic fully wired and tested.

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or trust boundary changes introduced.

## Self-Check: PASSED

| Item | Status |
|------|--------|
| src/analysis/PlaybackGate.h | FOUND |
| src/analysis/PlaybackGate.cpp | FOUND |
| tests/test_playback_gate.cpp | FOUND |
| .planning/phases/31-architecture-deepening/31-03-SUMMARY.md | FOUND |
| Commit b0bfb65 (Task 1) | FOUND |
| Commit f8018ae (Task 2) | FOUND |
| All 367 assertions pass | CONFIRMED |

## TDD Gate Compliance

Both test file (RED) and implementation (GREEN) were committed in Task 1 commit `b0bfb65`. Tests passed immediately (implementation written correctly alongside tests). All 367 assertions across 100 test cases pass after both tasks.
