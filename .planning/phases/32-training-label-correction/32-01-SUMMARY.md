---
phase: 32-training-label-correction
plan: "01"
subsystem: training
tags: [python, dataset, oracle, labels, pattern-rules, GMD]

# Dependency graph
requires:
  - phase: 17-gmd-training-pipeline
    provides: build_dataset.py with GMD loading, proxy feature computation, GroupShuffleSplit
  - phase: 23-onnx-inference-pattern-rules
    provides: src/inference/pattern_rules.h with kSoftMidBpmThreshold=120.0 and kSoftLoudBpmThreshold=160.0
provides:
  - _rule_pattern_for_state(): Python port of PatternRules::rulePatternForState with neutral policyIntensity=0.5
  - _oracle_label(): Breakdown heuristic override (D-03, D-04, D-06) applied on top of rule oracle
  - _K_SOFT_MID_BPM and _K_SOFT_LOUD_BPM constants mirroring pattern_rules.h
  - Y labels in build_dataset.py are now oracle-derived class indices [0..6] not quantile bins
  - 9 unit tests covering all oracle branches including GMD state=3.0 clamping
affects:
  - plan: 32-02 (Lakh-side and merge_datasets.py label passthrough — depends on this oracle)
  - training/train_gmd.py (trains PatternNet on these oracle labels)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Rule oracle mirror pattern: Python constants prefixed _K_ + UPPER_CASE mirror C++ constexpr names"
    - "GMD state=3.0 clamping: raw_state <= 2 passes through, >= 3 maps to SOFT(1) not LOUD(2)"
    - "Oracle-then-override pattern: rule label computed first, Breakdown heuristic applied only to non-SILENT"

key-files:
  created: []
  modified:
    - training/build_dataset.py
    - training/tests/test_dataset_build.py

key-decisions:
  - "GMD state=3.0 (sparse/breakdown) clamped to SOFT(1) not LOUD(2): GMD has no true BREAKDOWN category; sparse GMD patterns are quiet SOFT grooves"
  - "_K_SOFT_MID_BPM=120.0 and _K_SOFT_LOUD_BPM=160.0 defined as underscore-prefixed module constants mirroring pattern_rules.h lines 18-19 to prevent threshold drift"
  - "Breakdown override (class 6) guarded by label != 0 (D-04): SILENT rows can never be reassigned to Breakdown"
  - "policyIntensity=0.5 neutral at label-generation time (D-06); Phase 35 will pass adjustedBpm into X[0] as explicit feature"

patterns-established:
  - "Oracle mirror: Python training constants mirror C++ inference constants by name and value"
  - "State clamping: GMD 4-class state mapped to plugin 3-class state via raw_state if raw_state <= 2 else 1"

requirements-completed: [LABEL-01]

# Metrics
duration: 4min
completed: 2026-05-12
---

# Phase 32 Plan 01: Training Label Correction Summary

**Rule oracle (`_rule_pattern_for_state` + `_oracle_label`) replaces quantile-bin labeling in build_dataset.py, making GMD training labels a distillation of PatternRules::rulePatternForState with a Breakdown class-6 heuristic**

## Performance

- **Duration:** 4 min
- **Started:** 2026-05-12T11:53:54Z
- **Completed:** 2026-05-12T11:57:42Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- Ported `PatternRules::rulePatternForState` from `src/inference/pattern_rules.h` to Python as `_rule_pattern_for_state()` with mirrored BPM threshold constants
- Added `_oracle_label()` function with D-03 Breakdown override (class 6 for sparse, slow-BPM non-SILENT rows) and D-04 SILENT guard
- Deleted `_activity_score` and `_labels_from_train_quantiles` (quantile-bin approach removed per LABEL-01)
- Updated `main()` to build `Y_all_list` via `_oracle_label` per row, then slice by train/val index after GroupShuffleSplit
- Added 9 unit tests (7 for `_rule_pattern_for_state` covering all branches, 2 for `_oracle_label` covering Breakdown override and SILENT guard); full 21-test suite passes

## Task Commits

Each task was committed atomically:

1. **Task 1: Add rule oracle functions and replace quantile-bin labeling** - `f118ef8` (feat)
2. **Task 2: Add oracle unit tests for _rule_pattern_for_state and _oracle_label** - `03bdc37` (test)

## Files Created/Modified

- `training/build_dataset.py` - Added `_K_SOFT_MID_BPM`, `_K_SOFT_LOUD_BPM`, `_rule_pattern_for_state`, `_oracle_label`; removed `_activity_score`, `_labels_from_train_quantiles`, `scores`/`scores_all`; updated `main()` to use `Y_all_list`
- `training/tests/test_dataset_build.py` - Added `TestRulePatternForState` (7 tests) and `TestOracleLabel` (2 tests)

## Decisions Made

- GMD state=3.0 maps to SOFT (1) not LOUD (2): The formula `raw_state if raw_state <= 2 else 1` correctly handles the GMD 4-class encoding where state=3 is sparse/quiet, not a true LOUD state
- Used underscore-prefixed module constants `_K_SOFT_MID_BPM` / `_K_SOFT_LOUD_BPM` (vs. `k` prefix used in C++) to follow Python convention while remaining recognizable as mirrors of the C++ names

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed incorrect GMD state=3.0 clamping formula**

- **Found during:** Task 1 verification (spot check assertion)
- **Issue:** Plan specification said `min(int(state_float), 2)` but this maps state=3 → 2 (LOUD), not 1 (SOFT). The plan explicitly requires state=3.0 → SOFT(1) per the research finding that GMD state=3 is sparse/quiet, not a true LOUD pattern
- **Fix:** Changed to `raw_state if raw_state <= 2 else 1` which passes 0/1/2 through unchanged and clamps 3+ to 1 (SOFT)
- **Files modified:** training/build_dataset.py
- **Verification:** `_rule_pattern_for_state(100.0, 3.0) == 1` assertion passes; all 9 behavior spec assertions pass
- **Committed in:** f118ef8 (Task 1 commit)

**2. [Rule 1 - Bug] Fixed curly quotes in triple-quote docstring delimiters**

- **Found during:** Task 1 initial verification
- **Issue:** Write tool inserted typographic curly quotes (U+201C/U+201D) as triple-quote delimiters, causing `SyntaxError: invalid character`
- **Fix:** Replaced all curly quotes with straight ASCII double quotes via binary replacement
- **Files modified:** training/build_dataset.py
- **Verification:** Python import succeeds, syntax check passes
- **Committed in:** f118ef8 (Task 1 commit)

---

**Total deviations:** 2 auto-fixed (both Rule 1 - Bug)
**Impact on plan:** Both fixes essential for correctness. No scope creep.

## Issues Encountered

- Virtual environment path: `.venv/bin/python` in the worktree did not exist; resolved by using the main repo's `.venv` at `/Users/ng/Desktop/fuzzyband/training/.venv/bin/python` (same venv, different path)

## Next Phase Readiness

- `build_dataset.py` now generates oracle labels Y in [0,6] via rule oracle + Breakdown heuristic; ready for Plan 02 (Lakh-side changes and merge_datasets.py label passthrough)
- GMD state=3.0 clamping, Breakdown override, and SILENT guard all have named test coverage
- No blockers

## Self-Check

- [x] `training/build_dataset.py` exists and imports cleanly
- [x] `training/tests/test_dataset_build.py` contains `TestRulePatternForState` and `TestOracleLabel`
- [x] Commit f118ef8 exists (Task 1)
- [x] Commit 03bdc37 exists (Task 2)
- [x] 21/21 tests pass in `test_dataset_build.py`

## Self-Check: PASSED

---
*Phase: 32-training-label-correction*
*Completed: 2026-05-12*
