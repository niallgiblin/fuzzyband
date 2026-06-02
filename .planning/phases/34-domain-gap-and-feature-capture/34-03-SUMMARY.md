---
phase: 34-domain-gap-and-feature-capture
plan: 03
subsystem: training
tags: [proxy-gap, documentation, metrics, training]
requires:
  - phase: 34-01
    provides: Runtime feature capture JSONL schema and real plugin capture path
  - phase: 34-02
    provides: Feature-capture evaluator and capture schema validation
provides:
  - Captured-vs-proxy distribution-shift analyzer
  - Deterministic proxy-gap unit tests
  - Quantitative Phase 34 domain-gap documentation
affects: [phase-35, phase-36, pattern-training, feature-proxy]
tech-stack:
  added: []
  patterns:
    - argparse CLI for local training diagnostics
    - JSON and markdown outputs generated from the same computed report
key-files:
  created:
    - training/analyze_feature_proxy_gap.py
    - training/tests/test_feature_proxy_gap.py
  modified:
    - src/capture/FeatureCapture.cpp
    - CMakeLists.txt
    - training/FEATURE_PROXY.md
    - training/README.md
key-decisions:
  - "Spectral-centroid proxy changes are deferred to Phase 35/36 despite a measured standardized mean delta of 4.3525."
  - "Runtime-only FeatureVector fields are reported as not modeled by pattern proxy rather than fabricated into proxy columns."
patterns-established:
  - "Proxy-gap verdict thresholds: acceptable < 0.5, risky < 1.5, needs proxy change >= 1.5 standardized mean delta."
requirements-completed: [DOMAIN-03]
duration: 17 min
completed: 2026-05-25
---

# Phase 34 Plan 03: Domain Gap and Feature Proxy Summary

**Captured real-guitar feature distributions are now compared against pattern proxy tensors, with quantified residual gaps and per-feature verdicts documented for Phase 35/36.**

## Performance

- **Duration:** 17 min
- **Started:** 2026-05-25T11:53:00Z
- **Completed:** 2026-05-25T12:10:53Z
- **Tasks:** 3 completed
- **Files modified:** 6

## Accomplishments

- Added `training/analyze_feature_proxy_gap.py`, which loads one or more `feature_capture.v1` JSONL captures and one or more `.pt` proxy tensors, then reports count, mean, std, min, p05, p50, p95, max, absolute mean delta, standardized mean delta, and verdicts.
- Added `training/tests/test_feature_proxy_gap.py` covering comparable stats, verdict thresholds, runtime-only fields, markdown output, and required capture-field validation.
- Ran the analyzer on `/Users/ng/Documents/MetalAccompaniment/captures/feature_capture_20260525T111203Z.jsonl` against `training/data/processed/train.pt` and `training/data/lakh/lakh_tensors.pt`.
- Updated `training/FEATURE_PROXY.md` with 13,151 captured rows, 161,474 proxy rows, per-feature verdicts, runtime-only field treatment, and a spectral-centroid decision.

## Task Commits

1. **Task 1: Implement captured-vs-proxy distribution-shift analyzer** - `9ca4051` (feat, recovered existing combined commit)
2. **Task 2: Add proxy-gap tests** - `9ca4051` (feat, recovered existing combined commit)
3. **Task 3: Run analysis on captured data and update FEATURE_PROXY.md with quantitative verdicts** - `bc446f9` (docs)

**Plan metadata:** pending in close-out commit.

## Files Created/Modified

- `training/analyze_feature_proxy_gap.py` - CLI analyzer for captured-vs-proxy distribution-shift reports.
- `training/tests/test_feature_proxy_gap.py` - Unit tests for analyzer statistics, verdicts, runtime-only fields, markdown output, and schema validation.
- `src/capture/FeatureCapture.cpp` - Verifies the capture directory is writable before selecting it, so tests and hosts can fall back safely.
- `CMakeLists.txt` - Bumped plugin version to `0.5.5` for the verification builds.
- `training/FEATURE_PROXY.md` - Quantitative Phase 34 captured-vs-proxy gap section and spectral-centroid defer decision.
- `training/README.md` - Usage instructions for running the proxy-gap analyzer.

## Captured-Vs-Proxy Results

Analyzer command:

```bash
training/.venv/bin/python training/analyze_feature_proxy_gap.py \
  --capture /Users/ng/Documents/MetalAccompaniment/captures/feature_capture_20260525T111203Z.jsonl \
  --markdown-out training/artifacts/feature_proxy_gap.md \
  --json-out training/artifacts/feature_proxy_gap.json
```

| Feature | Standardized mean delta | Verdict |
|---|---:|---|
| `bpm` | 0.9929 | risky |
| `rmsEnergy` | 0.4277 | acceptable |
| `spectralCentroid` | 4.3525 | needs proxy change |
| `highFreqFlux` | 0.4230 | acceptable |
| `state_float` | 0.5395 | risky |
| `pitchRootMidi` | - | not modeled by pattern proxy |
| `pitchConfidence` | - | not modeled by pattern proxy |
| `policyIntensity` | - | not modeled by pattern proxy |
| `rmsDelta` | - | not modeled by pattern proxy |

## Decisions Made

- Deferred spectral-centroid proxy formula changes to Phase 35/36. It is the worst divergence, but formula changes should be evaluated together with inference-path consistency and default-readiness checks before retraining or model promotion.
- Kept raw capture data outside the repository and committed only aggregate statistics.
- Left `training/artifacts/feature_proxy_gap.md` and `.json` as ignored local generated outputs; `training/FEATURE_PROXY.md` is the committed decision record.

## Deviations from Plan

### Recovery

**1. Existing production commit without SUMMARY.md**
- **Found during:** Safe-resume gate before executing Plan 34-03.
- **Issue:** `feat(34-03): add feature proxy gap analyzer` already existed as commit `9ca4051`, but `.planning/phases/34-domain-gap-and-feature-capture/34-03-SUMMARY.md` was missing.
- **Fix:** Inspected the existing commit, verified Tasks 1 and 2, then completed Task 3 and wrote this summary instead of duplicating the analyzer/test work.
- **Files modified:** `.planning/phases/34-domain-gap-and-feature-capture/34-03-SUMMARY.md`, `training/FEATURE_PROXY.md`
- **Verification:** `py_compile`, targeted pytest, analyzer run, and acceptance-content checks passed.
- **Committed in:** `bc446f9` for Task 3; summary will be committed in the metadata commit.

**2. [Rule 3 - Blocking] FeatureCapture selected an unwritable capture directory**
- **Found during:** Post-merge C++ test gate.
- **Issue:** `FeatureCapture` accepted an existing Documents capture directory without proving the test process could write there. The writer thread then failed to create the JSONL file and three `FeatureCapture` tests failed.
- **Fix:** Added a write probe in `resolveCaptureDirectory()` so the capture path falls back to application data or temp directories when the preferred directory exists but is not writable.
- **Files modified:** `src/capture/FeatureCapture.cpp`
- **Verification:** `cmake --build build-onnx --target MetalAccompanimentTests` and `./build-onnx/MetalAccompanimentTests` passed after the fix.
- **Committed in:** pending post-merge fix commit.

---

**Total deviations:** 2 deviations (1 recovery, 1 blocking auto-fix).
**Impact on plan:** No scope expansion. The final artifacts satisfy the plan, but Tasks 1 and 2 are represented by one prior combined commit rather than separate task commits.

## Issues Encountered

- Local Git metadata was corrupt: `HEAD`, stale Claude worktree refs, and stash refs pointed at missing objects. Corrupt refs/worktree metadata were quarantined under `.git/repair-backup-20260525-130811`, `origin/main` was refetched, and the local `main` ref was restored before committing.
- Normal `git status` still recurses into broken tracked `.claude/worktrees/*` gitlinks. Status checks used `-c submodule.recurse=false --ignore-submodules=all` to avoid those stale worktree paths.
- Initial post-merge C++ tests failed in `FeatureCapture`; the capture directory writability fix resolved the failure.

## Verification

- `training/.venv/bin/python -m py_compile training/analyze_feature_proxy_gap.py` - passed
- `training/.venv/bin/python -m pytest training/tests/test_feature_proxy_gap.py` - passed, 5 tests
- `training/.venv/bin/python training/analyze_feature_proxy_gap.py --capture /Users/ng/Documents/MetalAccompaniment/captures/feature_capture_20260525T111203Z.jsonl --markdown-out training/artifacts/feature_proxy_gap.md --json-out training/artifacts/feature_proxy_gap.json` - passed
- `cmake --build build-onnx --target MetalAccompanimentTests` - passed
- `./build-onnx/MetalAccompanimentTests` - passed, 433 assertions in 112 test cases
- Acceptance content checks for `standardized_mean_delta`, `spectralCentroid`, `pitchRootMidi`, `not modeled by pattern proxy`, `Phase 34 captured-vs-proxy gap`, and `defer to Phase 35/36` - passed

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 35/36 should treat real captures as the authority for spectral-centroid behavior. The measured Phase 34 centroid gap is large enough to require either proxy redesign or explicit model-readiness acceptance before retraining or promotion.

---
*Phase: 34-domain-gap-and-feature-capture*
*Completed: 2026-05-25*
