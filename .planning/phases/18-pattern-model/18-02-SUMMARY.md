---
phase: 18-pattern-model
plan: 02
subsystem: training
tags: [pytorch, sklearn, onnx, gmd]

requires:
  - phase: 18-pattern-model
    provides: "PatternNet, PatternOnnxExport"
provides:
  - "training/train_gmd.py end-to-end training + ONNX export"
  - "training/README.md Phase 18 usage"
affects:
  - "18-03 validation"

tech-stack:
  added: []
  patterns:
    - "Inverse-frequency CE weights; early stopping on val macro-F1"

key-files:
  created:
    - training/train_gmd.py
  modified:
    - training/README.md

key-decisions:
  - "Warn (warnings.warn) when --data-dir resolves outside training/"

patterns-established: []

requirements-completed: [PMODEL-02]

duration: 25min
completed: 2026-04-19
---

# Phase 18: Pattern Model — Plan 02 Summary

**`train_gmd.py` loads Phase 17 shards, trains with weighted CE and macro-F1 early stopping, writes `metrics.jsonl`, and exports opset-17 `pattern_trained.onnx` under `training/artifacts/pattern-gmd-<UTC>/`.**

## Performance

- **Tasks:** 4
- **Files modified:** 2

## Accomplishments

- CLI with defaults aligned to `18-CONTEXT.md` (timestamped artifacts, patience 8, max-epochs cap 200).
- Documented workflow in `training/README.md` with validate command.

## Deviations from Plan

### Auto-fixed Issues

**1. Class weights helper — `torch.stack` on floats**
- **Found during:** First training run
- **Issue:** `torch.stack` requires tensors; initial implementation used Python floats.
- **Fix:** Build weights with `torch.tensor([...], dtype=torch.float32)`.

## Issues Encountered

- System Python 3.13 hit protobuf/onnx mismatch; project **`training/.venv`** must be used (already documented for Phase 15/17).

---
*Phase: 18-pattern-model*
