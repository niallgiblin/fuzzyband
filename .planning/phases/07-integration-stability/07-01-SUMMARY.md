---
phase: 07-integration-stability
plan: 01
subsystem: audio
tags: [structure, hysteresis, state-machine]

requires: []
provides:
  - Per-state hysteresis hold times (SILENT immediate exit)
affects: [integration-stability]

tech-stack:
  added: []
  patterns:
    - "Switch on currentState for holdRequired before accepting transitions"

key-files:
  created: []
  modified:
    - src/analysis/StructureTagger.h
    - src/analysis/StructureTagger.cpp
    - tests/test_structure_tagger.cpp

key-decisions:
  - "Replaced kMinStateHoldSec with kHoldSilentSec (0.0), kHoldVerseSec (1.5), kHoldChorusSec (2.0), kHoldBreakdownSec (3.0) per D-06/D-07"

patterns-established:
  - "Uniform carve-out for SILENT removed; zero hold on SILENT makes exit immediate"

requirements-completed: [STAB-01, STAB-04]

duration: 15min
completed: 2026-04-16
---

# Phase 7 Plan 01 Summary

**StructureTagger now uses per-state minimum hold times so SILENT exits immediately when energy exceeds threshold, fixing the prior 2s wait after play resumes.**

## Accomplishments

- Replaced `kMinStateHoldSec` with four `kHold*Sec` constants and a `switch (currentState)` for `holdRequired` in `update()`.
- Removed the `currentState != SILENT` carve-out; `kHoldSilentSec == 0.0` preserves D-07.

## Deviation

- `tests/test_structure_tagger.cpp`: increased the BREAKDOWN→SILENT loop from 200 to 260 iterations so simulated time exceeds `kHoldBreakdownSec` (3.0s) at 44.1 kHz / 512 samples per block. The plan only listed tagger sources; this keeps the hysteresis test aligned with the new breakdown hold.

## Verification

- `cmake --build build --target MetalAccompanimentTests` and `ctest --test-dir build --output-on-failure`: 4/4 passed.

## Self-Check

PASSED
