---
phase: 31-architecture-deepening
plan: "04"
completed: "2026-05-03"
status: complete
commits:
  - "055fbf2 feat(31-04): add StablePitchTracker class, tests, CMakeLists wiring, version 0.5.0"
  - "55c8d55 refactor(31-04): wire StablePitchTracker into AccompanimentProcessor (ARCH-02)"
key-files:
  created:
    - src/analysis/StablePitchTracker.h
    - src/analysis/StablePitchTracker.cpp
    - tests/test_stable_pitch_tracker.cpp
  modified:
    - src/AccompanimentProcessor.h
    - src/AccompanimentProcessor.cpp
    - CMakeLists.txt
---

## Summary

Extracted pitch-class stability state machine from `AccompanimentProcessor` into standalone `StablePitchTracker` value-type class (ARCH-02). Bumped project version to 0.5.0.

## What was built

**Task 1 — StablePitchTracker class + unit tests:**

`src/analysis/StablePitchTracker.h` / `.cpp` — stateless value-type class with:
- `reset()` — clears all state to E2 (MIDI 40) defaults
- `update(rawMidi, rawConf, bpm, numSamples, sampleRate, isSilent)` — returns semitone offset `[-6, 6]` when the pitch-class stability window (one beat) is satisfied; returns `INT_MIN` otherwise

Internal fields: `heldPitchRootMidi`, `heldPitchConfidence`, `pitchHoldValid`, `pitchStableCounterSamples`, `lastStablePitchMidi` — now fully encapsulated.

`tests/test_stable_pitch_tracker.cpp` — 8 TEST_CASEs, 35 assertions covering:
- Silence reset and counter clear
- Low-confidence gate (< 0.35 threshold)
- One-beat stability window requirement
- Octave-flip tolerance (E2↔E3 same pitch class)
- Semitone boundary values (Bb=+6, B=-5 via wrap)
- reset() clears all accumulated state
- Pitch-class change resets counter
- E root returns offset 0

`CMakeLists.txt` — version bumped 0.4.18 → 0.5.0; `StablePitchTracker.cpp` added to plugin sources and test sources.

**Task 2 — Wiring into AccompanimentProcessor:**

`AccompanimentProcessor.h`:
- Added `#include "analysis/StablePitchTracker.h"`
- Added `StablePitchTracker stablePitchTracker` value member
- Removed 5 pitch-stability private fields (moved into StablePitchTracker)
- `lastBassUpdateSample` stays (inference-thread guard, not pitch stability)

`AccompanimentProcessor.cpp`:
- `prepareToPlay`: 5-field inline reset replaced with `stablePitchTracker.reset()`
- `processBlock`: 20-line pitch-hold-update block + 28-line stability-window block replaced with single `stablePitchTracker.update()` call; `INT_MIN` sentinel gates `setBassSemitoneOffset()`; silence branch explicitly resets to offset 0
- `kMinPitchConfidence` local constant removed (now lives in StablePitchTracker.cpp)
- Added `<climits>` for `INT_MIN`
- `fv.pitchRootMidi` / `fv.pitchConfidence` now use `rawMidi` / `rawConf` directly

## Deviations

None significant. Minor: `fv.pitchRootMidi` now receives the raw (pre-hold) YIN reading rather than the held value — the held value was only needed for the stability logic which is now fully internal to `StablePitchTracker`. The inference thread already filters by confidence.

## Test results

**402 assertions / 109 test cases — all pass** (35 new from this plan; 367 carried forward from Plans 01–03).

## Self-Check: PASSED
