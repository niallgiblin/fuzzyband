---
phase: 31-architecture-deepening
reviewed: 2026-05-03T00:00:00Z
depth: standard
files_reviewed: 16
files_reviewed_list:
  - CMakeLists.txt
  - src/AccompanimentProcessor.cpp
  - src/AccompanimentProcessor.h
  - src/analysis/PlaybackGate.cpp
  - src/analysis/PlaybackGate.h
  - src/analysis/StablePitchTracker.cpp
  - src/analysis/StablePitchTracker.h
  - src/analysis/TempoStabiliser.cpp
  - src/analysis/TempoStabiliser.h
  - src/inference/OnnxInference.cpp
  - src/inference/RuleBasedInference.cpp
  - src/inference/pattern_rules.h
  - tests/test_pattern_rules.cpp
  - tests/test_playback_gate.cpp
  - tests/test_stable_pitch_tracker.cpp
  - tests/test_tempo_stabiliser.cpp
findings:
  critical: 0
  warning: 4
  info: 2
  total: 6
status: issues_found
---

# Phase 31: Code Review Report

**Reviewed:** 2026-05-03
**Depth:** standard
**Files Reviewed:** 16
**Status:** issues_found

## Summary

Phase 31 extracted four private state machines (`TempoStabiliser`, `PlaybackGate`, `StablePitchTracker`, `PatternRules`) from `AccompanimentProcessor` into standalone classes with their own test files. The extraction is structurally clean — no leftover state variables remain in `AccompanimentProcessor`, all new `.cpp` files are registered in `CMakeLists.txt` for both the plugin and unit test targets, and threading constraints are preserved (all new classes are value members accessed only on the audio thread).

Four issues were found: two logic/correctness warnings, one API contract warning, and one build hygiene warning, plus two info items. No security vulnerabilities. No crashes or data loss risks.

---

## Warnings

### WR-01: `applyExclusion` can produce state-incompatible pattern index 6 for LOUD+high-BPM

**File:** `src/inference/pattern_rules.h:74`

**Issue:** `applyExclusion(result, excludeIndex)` uses `(excludeIndex + 1) % kPatternCount` where `kPatternCount = 7`. When the active pattern is index 5 (LOUD, fast BPM) and the player presses "next pattern" (exclusion), the function returns `(5+1) % 7 = 6`. Pattern index 6 is the "Breakdown" pattern, which `isPatternCompatibleWithState` does not mark as compatible with any state:

```cpp
case StructureState::LOUD:  return patternIndex >= 4 && patternIndex <= 5;
// index 6 → falls through to: return false;
```

The pattern library safely clamps the index in `getPattern()` so there is no crash. But the Breakdown pattern will play during a LOUD section when the exclusion wraps, which is a musically incorrect result. The same path exists in `OnnxInference::selectPattern`: after the compatibility guard redirects to `rulePatternForState`, if that also returns 5 and `excludeIndex == 5`, the final `applyExclusion` call at line 158 still produces 6.

This defect was latent before the refactor but is now directly visible in the shared namespace. The refactor is the right time to fix it.

**Fix:** Either clamp the exclusion result to the compatibility range for the current state, or wrap within the state-appropriate sub-range:

```cpp
inline int applyExclusion(int result, int excludeIndex, StructureState state) noexcept
{
    if (excludeIndex < 0 || result != excludeIndex)
        return result;
    // Wrap within the state-compatible range
    switch (state)
    {
        case StructureState::SILENT: return 0;
        case StructureState::SOFT:   return (excludeIndex - 1 + 3) % 3 + 1; // cycles [1,2,3]
        case StructureState::LOUD:   return (excludeIndex == 4) ? 5 : 4;    // toggles [4,5]
    }
    return result;
}
```

Alternatively, add `case StructureState::LOUD: return patternIndex >= 4 && patternIndex <= 6;` to `isPatternCompatibleWithState` if Breakdown is intended to be reachable in loud sections.

---

### WR-02: `TempoStabiliser::getStableBpm()` diverges from `update()` return value when gate is closed

**File:** `src/analysis/TempoStabiliser.cpp:17-21`

**Issue:** When `playbackOpen == false`, `update()` returns the clamped candidate BPM without updating `stablePlaybackBpm`. The member `stablePlaybackBpm` retains its previous value (120 at startup), so `getStableBpm()` will return a value that differs from the return value of `update()` for the entire duration the gate is closed.

```cpp
if (!playbackOpen)
{
    pendingCandidateBpm     = candidate;
    pendingCandidateSamples = 0;
    return candidate;        // returns candidate
    // stablePlaybackBpm NOT updated — getStableBpm() returns different value
}
```

`AccompanimentProcessor` correctly uses the return value of `update()`, not `getStableBpm()`, so there is no bug in the current call site. However, the API silently violates the expectation set by the method name: a caller who reads `getStableBpm()` immediately after `update()` will see inconsistent state. The `test_tempo_stabiliser.cpp` test "pre-gate snap returns candidate directly" (line 77) tests the return value but does not assert on `getStableBpm()` afterward, leaving this inconsistency undocumented.

**Fix:** Either update `stablePlaybackBpm` on the `!playbackOpen` path, or document the divergence explicitly:

```cpp
if (!playbackOpen)
{
    pendingCandidateBpm     = candidate;
    pendingCandidateSamples = 0;
    stablePlaybackBpm = candidate;  // keep getter consistent with return value
    return candidate;
}
```

Add a test assertion confirming `getStableBpm() == result` after the pre-gate update call.

---

### WR-03: `PlaybackGate::resetTrackers` fires every block during sustained silence, not once

**File:** `src/analysis/PlaybackGate.cpp:35-46`

**Issue:** After the 2-second silence threshold is crossed, `gd.resetTrackers = true` fires on every subsequent audio block for as long as the signal remains silent. The condition `silenceSamples > phraseBreathSamples` is met on the first crossing block; `prevStructureState` is then set to `SILENT` (line 98), so the branch `prevStructureState != SILENT` (line 29) is false on subsequent silent blocks — `silenceSamples` is never reset, and the condition keeps being satisfied.

In `processBlock`, each `resetTrackers` block calls:
- `onsetDetector.resetTempoLock()` — likely idempotent
- `beatTracker.reset()` — resets internal beat tracker state each block
- `tempoStabiliser.reset()` — resets to 120 BPM each block
- `lastDrumPatternChangeSample = -1` — resets hold guard each block

Repeatedly resetting the beat tracker and tempo stabiliser every block (at 44100/256 ≈ 172 Hz) during sustained silence has no correctness consequence since the gate is closed and no MIDI fires, but it represents wasted work on the audio thread and obscures the intended semantics of "reset once on long-silence entry."

**Fix:** Add a one-shot flag or clamp `silenceSamples` after the reset fires:

```cpp
if (silenceSamples > phraseBreathSamples && !fullResetFired)
{
    fullResetFired = true;
    // ... existing reset body ...
    gd.resetTrackers = true;
}
```

Reset `fullResetFired` in `reset()` and when exiting SILENT back to a non-silent state.

---

### WR-04: `prevStructureSilent` is written but never read — dead state survives the refactor

**File:** `src/AccompanimentProcessor.h:153`, `src/AccompanimentProcessor.cpp:159, 529`

**Issue:** `prevStructureSilent` is a member of `AccompanimentProcessor` that was presumably used before the `PlaybackGate` extraction. After phase 31, it is written in two places (`prepareToPlay` and `processBlock`) but has zero read sites. It contributes no behavior and clutters the processor state.

```cpp
// AccompanimentProcessor.h:153
bool prevStructureSilent = false;

// AccompanimentProcessor.cpp:159 (prepareToPlay)
prevStructureSilent = false;

// AccompanimentProcessor.cpp:529 (processBlock, end of block)
prevStructureSilent = (st == StructureState::SILENT);
```

**Fix:** Delete the member declaration and both write sites.

---

## Info

### IN-01: `moodycamel` dependency pinned to `master` branch, not a fixed tag

**File:** `CMakeLists.txt:39`

**Issue:** The moodycamel ReaderWriterQueue is fetched with `GIT_TAG master`, a moving target. If the upstream repo introduces a breaking change, CI builds can fail non-deterministically across different checkout times. All other dependencies (`JUCE`, `Catch2`) are pinned to specific release tags.

**Fix:**
```cmake
GIT_TAG        v1.0.6   # or whichever is the current stable tag
```

Check the current HEAD SHA or latest release on https://github.com/cameron314/readerwriterqueue and pin to it.

---

### IN-02: `PlaybackGate` test for phrase-breath re-entry does not verify gate state after armCrash

**File:** `tests/test_playback_gate.cpp:99-113`

**Issue:** The "armCrash fires on phrase-breath re-entry" test correctly checks `gd.armCrash == true` on the single LOUD block after phrase breath. However, it does not check whether `gd.gateOpen` is correct on that same block, nor does it verify that `gate.isGateOpen()` transitions from closed to open. This leaves the "gate transitions open after phrase-breath re-entry" path untested. A regression that clears `gateOpen` in the armCrash path would not be caught.

**Fix:** Add assertions for gate state after the crash-arm block:
```cpp
GateDecision gd = gate.update(StructureState::LOUD, 0.5, true, true, 0.8f, 512, sr);
REQUIRE(gd.armCrash);
// Also verify gate behavior on re-entry (snapBeatNow or gateOpen expected):
REQUIRE((gd.gateOpen || gd.snapBeatNow));  // gate should be opening
```

---

_Reviewed: 2026-05-03_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
