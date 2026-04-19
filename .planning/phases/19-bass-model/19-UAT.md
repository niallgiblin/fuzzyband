---
status: complete
phase: 19-bass-model
source: [19-01-SUMMARY.md, 19-02-SUMMARY.md]
started: 2026-04-19T12:00:00Z
updated: 2026-04-19T16:15:00Z
---

## Current Test

[testing complete]

## Tests

### 1. BassNet / BassOnnxExport import and batch-size guard
expected: From `training/`, `from models.bass_model import …`; N=1 → [1,4]; N=2 → ValueError (batch size 1 / contract). Not `from training.models` (ModuleNotFoundError at repo root).
result: pass
uat_note: First draft command used `training.models`; corrected to match `train_bass.py` sys.path pattern.

### 2. train_bass quick run produces artifacts
expected: `python3 training/train_bass.py --max-epochs 2` (or similar short run) completes without error. The printed output directory contains `bass_trained.onnx`, `bass_norm_stats.json`, and `metrics.jsonl`.
result: pass

### 3. ONNX contract validation for bass
expected: `python3 scripts/validate_onnx_contract.py --bass <path-to-bass.onnx>` exits 0. For a quick check without training, `assets/bass_model.onnx` in the repo may be used if present.
result: pass

### 4. README Phase 19 developer instructions
expected: `training/README.md` has a “Phase 19” section that tells you how to run `train_bass.py` and how to run `validate_onnx_contract.py --bass` with a path to the exported ONNX.
result: pass

## Summary

total: 4
passed: 4
issues: 0
pending: 0
skipped: 0
blocked: 0

## Gaps

[none yet]
