---
phase: 34-domain-gap-and-feature-capture
type: code-review
status: passed
reviewed_at: "2026-05-25"
reviewer: codex
depth: standard-inline
open_findings: 0
fixed_findings: 1
---

# Phase 34 Code Review

## Scope

Reviewed the Phase 34 runtime capture, offline evaluator, proxy-gap analyzer, tests, and docs:

- `src/capture/FeatureCapture.h`
- `src/capture/FeatureCapture.cpp`
- `src/AccompanimentProcessor.h`
- `src/AccompanimentProcessor.cpp`
- `src/AccompanimentEditor.h`
- `src/AccompanimentEditor.cpp`
- `tests/test_feature_capture.cpp`
- `docs/FEATURE_CAPTURE.md`
- `training/evaluate_feature_capture.py`
- `training/tests/test_feature_capture_eval.py`
- `training/analyze_feature_proxy_gap.py`
- `training/tests/test_feature_proxy_gap.py`
- `training/FEATURE_PROXY.md`
- `training/README.md`

## Result

No open correctness, real-time safety, or security findings remain.

## Fixed During Review

- `training/evaluate_feature_capture.py` wrote `--json-out` directly without creating parent directories, while the documented example uses `training/artifacts/feature_capture_eval.json`. This could turn a successful evaluation into an uncaught `FileNotFoundError` when the artifact directory was absent.
  - Fixed in commit `4b0c8a9` by creating `args.json_out.parent`.
  - Added `test_json_out_creates_parent_directory`.

## Notes

- Runtime capture row construction remains outside `processBlock()` on the inference thread; the audio callback still only enqueues `FeatureVector` snapshots.
- Capture file I/O is isolated to the `FeatureCapture` writer thread.
- The post-merge C++ regression failure exposed a real capture-directory writability bug, fixed in commit `3f7f3d6`.

## Verification

- `training/.venv/bin/python -m py_compile training/evaluate_feature_capture.py training/analyze_feature_proxy_gap.py` passed.
- `training/.venv/bin/python -m pytest training/tests/test_feature_capture_eval.py training/tests/test_feature_proxy_gap.py` passed: 12 tests.
- Earlier post-merge C++ gate after the capture-directory fix passed: `./build-onnx/MetalAccompanimentTests` reported 433 assertions in 112 test cases.
