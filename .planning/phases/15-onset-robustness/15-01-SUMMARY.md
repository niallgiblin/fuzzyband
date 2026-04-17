---
phase: 15-onset-robustness
plan: 01
subsystem: analysis
tags:
  - onset-detection
  - tempo-tracking
  - distortion-robustness
  - energy-analysis
dependency_graph:
  requires: []
  provides:
    - adaptive-flux-threshold
    - band-limited-onset-flux
    - peak-rms-envelope
    - relative-silent-threshold
  affects:
    - src/analysis/OnsetDetector.h
    - src/analysis/OnsetDetector.cpp
    - src/analysis/EnergyAnalyser.h
    - src/analysis/EnergyAnalyser.cpp
    - src/analysis/StructureTagger.h
    - src/analysis/StructureTagger.cpp
    - src/AccompanimentProcessor.cpp
    - tests/test_onset_detector.cpp
tech_stack:
  added: []
  patterns:
    - rolling-mean adaptive threshold
    - slow-decay peak envelope follower
    - band-limited spectral flux
key_files:
  created: []
  modified:
    - src/analysis/OnsetDetector.h
    - src/analysis/OnsetDetector.cpp
    - src/analysis/EnergyAnalyser.h
    - src/analysis/EnergyAnalyser.cpp
    - src/analysis/StructureTagger.h
    - src/analysis/StructureTagger.cpp
    - src/AccompanimentProcessor.cpp
    - tests/test_onset_detector.cpp
decisions:
  - "Band-limited flux restricted to 100 Hz–4 kHz; fluxBinLo/Hi precomputed in prepare() to avoid per-frame division"
  - "Adaptive threshold uses kAdaptiveK=1.5 x rolling mean over 43 frames (~0.5s at 512-hop/44100), with kFluxFloor=0.05 minimum"
  - "IOI ring expanded to 16 slots; minimum-count guard raised from 3 to 8 onsets before BPM is trusted"
  - "Peak RMS envelope uses fast attack (0.99) and slow release (0.9995) to track session loudness; SILENT threshold becomes max(kSilentRms, peakRms*0.15)"
  - "StructureTagger::update() peakRms parameter defaults to 0.0f for backward compatibility with existing tests"
metrics:
  duration: "~25 minutes"
  completed: "2026-04-17"
  tasks_completed: 4
  files_modified: 8
---

# Phase 15 Plan 01: Onset-Robustness Summary

**One-liner:** Band-limited adaptive flux threshold + peak-RMS relative SILENT detection to fix tempo tracking under heavy distortion signal chains.

## Tasks Completed

| Task | Name | Commit |
|------|------|--------|
| 1+2 | Band-limited + adaptive flux threshold; 16-slot IOI ring | 4610524 |
| 3 | Relative SILENT threshold via peak-RMS envelope | a5c47ca |
| 4 | Update onset tests: warmup comments + noise-immunity test | b14bdcd |

## What Was Built

### Task 1: Band-limited + adaptive flux threshold (OnsetDetector)

Replaced the hardcoded `fluxThreshold = 0.35f` with an adaptive mechanism:

- **Band-limited flux**: Flux is now summed only over bins in the 100 Hz–4 kHz range (`fluxBinLo` to `fluxBinHi`). DC is always skipped. These bin indices are precomputed in `prepare()` — no per-frame division. All bins still update `prevMagnitudes` to maintain spectral history accuracy.
- **Rolling mean adaptive threshold**: A 43-slot ring buffer (`fluxRollingBuf`) tracks the recent mean flux. The onset threshold becomes `max(kFluxFloor=0.05, kAdaptiveK=1.5 * mean)`. When distortion produces steady shimmer, the mean rises to match the noise floor and the threshold rises with it, suppressing false onsets. Only transients that spike above 1.5x the recent mean fire as onsets.

### Task 2: Larger IOI ring + stricter convergence (OnsetDetector)

- `ioiHistory` expanded from `array<float,8>` to `array<float,16>`
- `pushIoi` and `medianIoiBpm` ring index arithmetic updated to modulo 16
- Minimum-count guard in `medianIoiBpm()` raised from `< 3` to `< 8`
- With 16 slots and a minimum of 8, the tracker needs 8 genuine onsets before committing to a BPM — making it robust against an initial burst of 3–4 noise onsets

### Task 3: Reachable SILENT threshold (EnergyAnalyser + StructureTagger)

- **EnergyAnalyser**: Added `peakRmsEnvelope` member with `getPeakRms()` accessor. Updated per-block with fast attack (0.99) / slow release (0.9995) envelope follower tracking `rmsEnergy`. Clamped below by `kSilentRmsFloor = 0.01f`. Reset in `prepare()`.
- **StructureTagger**: `computeDesiredState()` now takes `peakRms` and computes `silentFloor = max(kSilentRms, peakRms * 0.15f)`. This means SILENT fires when energy drops to 15% of the session peak — achievable even with a loud amp sim idle floor.
- `StructureTagger::update()` has a new `float peakRms = 0.0f` parameter (default preserves backward compatibility).
- **AccompanimentProcessor::processBlock()** updated to pass `energyAnalyser.getPeakRms()` to the `structureTagger.update()` call.

### Task 4: Updated onset detector tests

- Added comment to existing 160 BPM convergence test explaining that 24 periods (24 onsets) is well above the new 8-onset minimum guard
- Added new `TEST_CASE("OnsetDetector: rejects sustained noise bursts")` that feeds identical constant-value blocks for ~2 seconds and asserts BPM remains at the default 120.0f

## Build and Test Results

- Build: `cmake --build build --target MetalAccompanimentTests` — succeeded with no new warnings
- Tests: 21/21 unit tests pass (22nd test is a pre-existing "Not Run" — `MetalAccompanimentIntegrationTests` binary absent from this build configuration; unrelated to these changes)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Test file named differently than plan specified**

- **Found during:** Task 4
- **Issue:** Plan referenced `tests/OnsetDetectorTests.cpp` but the actual file is `tests/test_onset_detector.cpp` (follows `test_*.cpp` convention). `OnsetDetectorTests.cpp` did not exist.
- **Fix:** Applied changes to the existing `tests/test_onset_detector.cpp` file instead.
- **Files modified:** `tests/test_onset_detector.cpp`
- **Commit:** b14bdcd

**2. [Rule 2 - Missing critical functionality] StructureTagger.cpp needed `#include <algorithm>` for `std::max`**

- **Found during:** Task 3
- **Issue:** `computeDesiredState` now uses `std::max` which requires `<algorithm>`.
- **Fix:** Added `#include <algorithm>` to `StructureTagger.cpp`.
- **Files modified:** `src/analysis/StructureTagger.cpp`
- **Commit:** a5c47ca

**3. [Rule 2 - Missing critical functionality] `StructureTagger::computeDesiredState` signature update required**

- **Found during:** Task 3
- **Issue:** Plan described passing `peakRms` through `update()` and into `computeDesiredState()`, but only the `update()` signature was specified explicitly. `computeDesiredState` is a private helper and also needed updating.
- **Fix:** Updated both the header declaration and `.cpp` definition of `computeDesiredState` to accept `float peakRms`.
- **Files modified:** `src/analysis/StructureTagger.h`, `src/analysis/StructureTagger.cpp`
- **Commit:** a5c47ca

## Known Stubs

None. All changes are fully wired.

## Threat Flags

None. All changes are local to existing audio-thread components; no new network endpoints, auth paths, file access, or schema changes introduced.

## Self-Check: PASSED

- `src/analysis/OnsetDetector.h` — exists, contains `fluxRollingBuf`, `kAdaptiveK`, `fluxBinLo`
- `src/analysis/OnsetDetector.cpp` — exists, contains `fluxRollingSum`, adaptive threshold logic
- `src/analysis/EnergyAnalyser.h` — exists, contains `getPeakRms()`
- `src/analysis/StructureTagger.cpp` — exists, contains `peakRms * 0.15f`
- `tests/test_onset_detector.cpp` — exists, contains "rejects sustained noise"
- Commits 4610524, a5c47ca, b14bdcd — all verified in git log
