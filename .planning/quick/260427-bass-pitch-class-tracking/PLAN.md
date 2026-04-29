# Quick Task: Fix bass pitch tracking to follow guitar root

**Date:** 2026-04-27
**Status:** complete (implementation in tree; see SUMMARY.md)

## Goal
Make the library-pattern bass notes follow the guitar's tonic rather than staying hardcoded at E2 (MIDI 40).

## Root Cause
Two bugs in `AccompanimentProcessor::processBlock`:

1. **Stability tracked in absolute MIDI space**: YIN can octave-flip (E2↔E3). `diff = abs(currentMidi - lastStablePitchMidi)` sees a 12-semitone jump, resets the counter, and `setBassSemitoneOffset` is never called.

2. **Offset calculation ignores register**: `rounded - 40` adds raw semitones. If YIN detects E3 (52), offset=12 → bass at MIDI 52 (E3). If it detects E4 (64), offset=24 → bass at MIDI 64 (way too high).

## Fix (AccompanimentProcessor.cpp only)

**Task 1 — pitch-class stability** (lines ~483-494):
Replace absolute MIDI diff with pitch-class comparison. A note and its octave double share pitch class, so octave flips no longer reset the counter.

```cpp
// before
const float diff = std::abs(currentMidi - lastStablePitchMidi);
if (diff <= 0.5f) { pitchStableCounterSamples += numSamples; }
else { pitchStableCounterSamples = 0; lastStablePitchMidi = currentMidi; }

// after
const int currentPc = (static_cast<int>(std::round(currentMidi)) % 12 + 12) % 12;
const int lastPc    = (static_cast<int>(std::round(lastStablePitchMidi)) % 12 + 12) % 12;
if (currentPc == lastPc) { pitchStableCounterSamples += numSamples; }
else { pitchStableCounterSamples = 0; lastStablePitchMidi = currentMidi; }
```

**Task 2 — bass register mapping** (lines ~501-505):
Replace `rounded - 40` with pitch-class-to-nearest-semitone mapping. Max ±6 semitones from E2, keeps bass in E1–A#2 register.

```cpp
// before
const int rounded = static_cast<int>(std::lround(static_cast<double>(currentMidi)));
patternPlayer.setBassSemitoneOffset(rounded - 40);

// after
const int rounded   = static_cast<int>(std::lround(static_cast<double>(currentMidi)));
const int pc        = (rounded % 12 + 12) % 12;
const int bassRootPc = 40 % 12;   // 4 = E
int delta = pc - bassRootPc;
if (delta > 6)  delta -= 12;
if (delta < -6) delta += 12;
patternPlayer.setBassSemitoneOffset(delta);
```

**Task 3 — version bump**: 0.3.6 → 0.3.7 in CMakeLists.txt line 4.

## Files
- `src/AccompanimentProcessor.cpp`
- `CMakeLists.txt`
