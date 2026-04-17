---
phase: 07-integration-stability
plan: 05
status: complete
completed: 2026-04-17
requirements:
  - STAB-01
  - STAB-02
  - STAB-03
  - STAB-04
---

# Plan 07-05 Summary

Closed Phase 7 by producing a Release + RelWithDebInfo build, tuning `StructureTagger` constants from a live jam session, and running the four STAB verifications plus a ThreadSanitizer check.

## Task-by-task outcome

| # | Task | Status | Evidence |
|---|------|--------|----------|
| 1 | Release build + tests | Pass | `build-release/` VST3 + AU artefacts; all 7 ctest cases pass |
| 2 | 10-min jam session (STAB-01) | Pass | `07-05-JAM-NOTES.md` |
| 3 | Apply tuned constants (D-01) | Pass | commit `46213ea` — `StructureTagger.h` |
| 4 | CPU profile (STAB-02) | Pass | `07-05-CPU-PROFILE.md` — Activity Monitor delta ~7% on M2 |
| 5 | ThreadSanitizer (D-05) | Pass | `07-05-TSAN-REPORT.md` — 96 assertions, 0 warnings |
| 6 | Edge cases (STAB-03) | Pass | `07-05-EDGE-CASE-REPORT.md` — A-E all pass |
| 7 | 20-min stability (STAB-04) | Pass | `07-05-STABILITY-REPORT.md` — 0 crashes, 0 xruns |

## Final tuned constants (`StructureTagger.h`)

| Constant | Before | After |
|---|---|---|
| `kSilentRms` | 0.05 | 0.05 (unchanged) |
| `kBreakdownCentroidHz` | 1200 | **1000** |
| `kVerseCentroidHz` | 2400 | **2200** |
| `kHoldSilentSec` | 0.0 | 0.0 (D-07 locked) |
| `kHoldVerseSec` | 1.5 | **2.0** |
| `kHoldChorusSec` | 2.0 | **2.5** |
| `kHoldBreakdownSec` | 3.0 | **2.5** |

## Headline numbers

- **CPU cost**: ~7% Activity Monitor delta on MacBook Air M2 @ 256 samples / 48 kHz (budget < 15%).
- **Crashes**: 0 over the 20-minute soak.
- **xruns**: 0 over the 20-minute soak.
- **TSan**: 0 warnings across 7 test cases / 96 assertions.

## Deviations and follow-ups

- **Initial "cpu fail" reading** (~39.5% `processBlock` Time Profiler Weight) was a misread of Instruments — Weight is inclusive time within the trace, not system CPU. Re-profiled against Activity Monitor delta using a RelWithDebInfo build; result is comfortably under budget. Methodology note recorded in `07-05-CPU-PROFILE.md` for future measurements.
- **Musicality / tempo-follow** flagged by the user during STAB-04 (drums often fail to lock to guitar tempo, beat "falls apart"). This is a Phase-1 rule-based inference limitation, not a stability gap — carried forward as Phase 2 work (already covered by the Phase 2 ONNX roadmap).
- **`build-release/MetalAccompanimentTests`** binary rebuilt during the re-profile but left uncommitted — build artefact, not source.

## Residual risk for Phase 8

- None blocking. Release VST3 + AU artefacts installed at `~/Library/Audio/Plug-Ins/` are the candidate v0.1.0-phase1 binaries. Tagging is Phase 8 (DOCS-04).
