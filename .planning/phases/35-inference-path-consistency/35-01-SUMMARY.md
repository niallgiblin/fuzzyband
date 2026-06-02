---
phase: 35-inference-path-consistency
plan: 01
subsystem: inference
tags: [cpp, pattern-rules, onnx, catch2, exclusion-cycling]

requires:
  - phase: 32-training-label-correction
    provides: Corrected rule-oracle label semantics and PatternRules thresholds
provides:
  - Compatibility-aware applyExclusion shared by RuleBasedInference and OnnxInference
  - ONNX pattern input column 0 packed with PatternRules::adjustedBpm(f)
  - Exhaustive 3×7 state/exclude matrix tests with intensity boundary cases
affects: [35-02-retrain, inference-path-consistency]

tech-stack:
  added: []
  patterns:
    - "Modulo-7 forward scan for state-compatible rejection cycling"
    - "Single 4-arg applyExclusion signature in header-only PatternRules"

key-files:
  created: []
  modified:
    - src/inference/pattern_rules.h
    - src/inference/RuleBasedInference.cpp
    - src/inference/OnnxInference.cpp
    - src/analysis/FeatureVector.h
    - tests/test_pattern_rules.cpp
    - tests/test_rule_based_inference.cpp

key-decisions:
  - "LOUD exclude=5 returns 4 (first compatible after scan), not plan's illustrative 0"
  - "SILENT exclude=0 may return fallback 0 when no alternative pattern exists"

patterns-established:
  - "Both inference backends call applyExclusion(result, excludeIndex, f.state, fallback)"
  - "ONNX X[0] uses adjustedBpm incorporating policyIntensity offset"

requirements-completed: [INFC-01, INFC-02]

duration: 15min
completed: 2026-05-25
---

# Phase 35 Plan 01: Runtime Inference Path Alignment Summary

**Shared compatibility-aware exclusion cycling and ONNX column-0 adjusted BPM aligned with PatternRules rule path**

## Performance

- **Duration:** ~15 min
- **Tasks:** 3
- **Files modified:** 6

## Accomplishments

- Replaced blind `(exclude+1)%7` with modulo-7 scan until `isPatternCompatibleWithState` passes; fallback to rule/state default when none found
- Updated all three `OnnxInference::selectPattern` call sites and `RuleBasedInference` to use 4-arg `applyExclusion`
- Packed `PatternRules::adjustedBpm(f)` into ONNX tensor column 0; updated stale `FeatureVector::policyIntensity` comment
- Added exhaustive 3×7 exclusion matrix tests plus LOUD Breakdown skip and SOFT intensity threshold cases

## Task Commits

1. **Task 1: Compatibility-aware applyExclusion** - `e8d97ba` (feat)
2. **Task 2: Pack adjustedBpm into ONNX X[0]** - `147dc35` (feat)
3. **Task 3: Exhaustive exclusion matrix tests** - `f4d9c45` (test)

## Files Created/Modified

- `src/inference/pattern_rules.h` - 4-arg `applyExclusion` with state-compatible scan
- `src/inference/RuleBasedInference.cpp` - Passes `f.state` and result as fallback
- `src/inference/OnnxInference.cpp` - `adjustedBpm(f)` in inputData; 4-arg exclusion on success and error paths
- `src/analysis/FeatureVector.h` - Comment reflects ONNX column 0 uses adjusted BPM
- `tests/test_pattern_rules.cpp` - Matrix, LOUD Breakdown, intensity threshold tests
- `tests/test_rule_based_inference.cpp` - Compatibility assertions and intensity cases

## Decisions Made

- LOUD `applyExclusion(5,5,...)` returns **4** (first LOUD-compatible index after skipping 6), not 0 as plan text suggested
- SILENT with `exclude=0` has no compatible alternative; tests allow fallback 0 (compatible) without requiring `!= exclude`

## Deviations from Plan

### Test expectation adjustments

**1. LOUD exclude=5 expected return value**
- **Found during:** Task 3 (LOUD Breakdown skip test)
- **Issue:** Plan stated return **0**; modulo-7 scan yields **4** as first LOUD-compatible candidate after skipping 6
- **Fix:** Assert `out == 4`, `out != 6`, and state compatibility
- **Files modified:** `tests/test_pattern_rules.cpp`
- **Committed in:** `f4d9c45`

**2. SILENT exclude=0 matrix edge case**
- **Found during:** Task 3 (exclusion matrix)
- **Issue:** Only pattern 0 is SILENT-compatible; when excluded, fallback is still 0 — cannot satisfy both `!= exclude` and compatibility
- **Fix:** Skip `out != exclude` assertion when `state == SILENT && exclude == 0`; always assert compatibility
- **Files modified:** `tests/test_pattern_rules.cpp`, `tests/test_rule_based_inference.cpp`
- **Committed in:** `f4d9c45`

---

**Total deviations:** 2 (test expectations only; implementation matches D-09/D-10 algorithm)
**Impact on plan:** Runtime INFC-02 behavior correct; training/docs half of INFC-01 deferred to Plan 02

## Issues Encountered

None — all PatternRules and RuleBasedInference tests pass in `build-onnx`.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Runtime half of INFC-01 complete (ONNX X[0] uses adjustedBpm)
- INFC-02 complete at runtime with exhaustive tests green
- Plan 02 can proceed with dataset expansion, retrain, and doc updates for training proxy alignment

## Self-Check: PASSED

- FOUND: src/inference/pattern_rules.h
- FOUND: src/inference/OnnxInference.cpp
- FOUND: tests/test_pattern_rules.cpp
- FOUND: e8d97ba
- FOUND: 147dc35
- FOUND: f4d9c45

---
*Phase: 35-inference-path-consistency*
*Completed: 2026-05-25*
