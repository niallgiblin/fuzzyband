---
phase: 11-pitch-harmony
status: passed
verified: 2026-04-17
---

# Phase 11 verification — Pitch & harmony

## Must-haves (from plans)

| ID | Check | Evidence |
|----|--------|----------|
| PITCH-01 | YIN runs on audio path, feeds features | `PitchEstimator::process` in `AccompanimentProcessor::processBlock`; `FeatureVector` enqueued with pitch fields |
| PITCH-02 | Root + confidence per window | `FeatureVector::pitchRootMidi`, `pitchConfidence`; D-11-04 policy in processor |
| PITCH-03 | Bass transposed at playback when confident | `PatternPlayer::setBassSemitoneOffset`; offset from `round(pitchRootMidi)-40` |
| PITCH-04 | Synthetic tests + documented tolerance | `tests/test_pitch_estimator.cpp`, `docs/PHASE11_PITCH_TESTING.md`; `ctest` green |

## Automated

- `cmake --build build --config Release`
- `ctest --output-on-failure -C Release` (from `build/`) — 10/10 tests passed (macOS)

## Notes

- YIN uses smallest lag among near-minima of the difference function within ~75–500 Hz lag band to favor the fundamental on synthetic sines.
- Drum channel events unchanged (offset gated on bass channel only).
