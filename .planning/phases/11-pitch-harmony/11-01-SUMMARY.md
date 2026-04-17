---
phase: 11-pitch-harmony
plan: 01
subsystem: dsp
tags: [yin, pitch, feature-vector]

requires: []
provides:
  - PitchEstimator (YIN + ring buffer window)
  - FeatureVector pitchRootMidi / pitchConfidence
  - D-11-04 hold/snap in AccompanimentProcessor
affects: []

tech-stack:
  added: []
  patterns: [Ring-buffered YIN; smallest-lag among argmin difference for fundamental]

key-files:
  created:
    - src/analysis/PitchEstimator.h
    - src/analysis/PitchEstimator.cpp
  modified:
    - src/analysis/FeatureVector.h
    - src/AccompanimentProcessor.h
    - src/AccompanimentProcessor.cpp
    - CMakeLists.txt

key-decisions:
  - "Fundamental period: smallest lag τ in guitar band where d(τ) is near global min (avoids pure-sine harmonic ties at 2T,3T…)."
  - "Lag bounds: ~75–500 Hz via sr/500 … sr/75 so 440 Hz @ 48 kHz is in range."

patterns-established:
  - "Pitch confidence blends CMNDF spread with depth when firstMin < 0.05"

requirements-completed: [PITCH-01, PITCH-02]

duration: —
completed: 2026-04-17
---

# Phase 11 — Plan 01 Summary

**YIN-based `PitchEstimator` with a 4096-sample ring buffer, `FeatureVector` pitch fields, and D-11-04 silence/hold/snap policy before `try_enqueue`.**

## Self-Check: PASSED

- Release build succeeded.
- Acceptance greps satisfied for plan tasks.

## Files Created/Modified

- `src/analysis/PitchEstimator.{h,cpp}` — YIN d(τ), CMNDF, fundamental pick, confidence.
- `src/analysis/FeatureVector.h` — `pitchRootMidi`, `pitchConfidence`.
- `src/AccompanimentProcessor.{h,cpp}` — wiring + hold state.
- `CMakeLists.txt` — plugin + tests link `PitchEstimator.cpp`.
