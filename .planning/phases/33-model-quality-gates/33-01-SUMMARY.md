---
phase: 33-model-quality-gates
plan: "01"
subsystem: training
tags:
  - training
  - bass
  - quality-gate
  - onnx
dependency_graph:
  requires:
    - 32-training-label-correction/32-03-SUMMARY.md
  provides:
    - QGATE-01 baseline MSE gate in train_bass.py
  affects:
    - training/train_bass.py
    - training/artifacts/bass-*/validation.json
tech_stack:
  added: []
  patterns:
    - gate-before-export ordering (D-01, D-02, D-06)
    - constant-mean baseline MSE from training split (D-03)
    - compact gate report + per-step diagnostics in validation.json (D-04, D-05)
key_files:
  modified:
    - training/train_bass.py
decisions:
  - "D-03 baseline uses y_tr.mean(dim=0), not y_va — avoids trivially easy gate"
  - "Per-step forward pass reuses normalized x_va tensor with TensorDataset(x_va, y_va) to avoid tuple-unpacking mismatch"
  - "validation.json written before gate enforcement so failing runs still emit inspection artifact"
metrics:
  duration: "~15 minutes"
  completed: "2026-05-13T09:43:00Z"
  tasks_completed: 1
  tasks_total: 1
  files_modified: 1
---

# Phase 33 Plan 01: Bass QGATE-01 Quality Gate Summary

QGATE-01 implemented: `train_bass.py` now computes a constant-mean baseline MSE from the training split and blocks ONNX export when `best_val_mse > baseline_mse * 0.90`, writing a compact gate report plus per-step diagnostics to `validation.json` on every run.

## What Was Built

`training/train_bass.py` post-training block (lines 213–274) was replaced with:

1. **Baseline MSE computation (D-03):** `baseline_pred = y_tr.mean(dim=0, keepdim=True)` gives the mean 32-step target from the training split. Broadcast over all validation rows, MSE gives `baseline_mse = 7.467` on current data.

2. **Gate threshold (D-02):** `gate_mse = baseline_mse * 0.90` (6.720 on current data). Model must beat the constant-mean baseline by 10%.

3. **Per-step informational metrics (D-05):** A second forward pass over the validation set using the best model state collects predictions, then computes:
   - `per_step_mse` — combined pitch+velocity MSE per step (16 values)
   - `per_step_mae` — combined pitch+velocity MAE per step (16 values)
   - `pitch_offset_histogram` — 25-bin histogram of predicted pitch offsets over `[-12, 12]`

4. **Expanded `validation.json` (D-04, D-05):** Top-level keys `{gate, per_step_mse, per_step_mae, pitch_offset_histogram}`. The `gate` sub-dict contains `{best_val_mse, baseline_mse, gate_mse, passed, baseline_method}` with `baseline_method = "mean_32step_train_target"`.

5. **Gate enforcement (D-01, D-06):** After writing `validation.json` but before `torch.onnx.export`, if `not gate_passed` the script prints:
   ```
   error: QGATE-01 failed — best_val_mse X.XXXXXX > gate_mse Y.YYYYYY (baseline Z.ZZZZZZ * 0.90)
   ```
   and returns 1 without producing `bass_trained.onnx`.

## Observed Gate Values (1-epoch test run)

| Metric | Value |
|--------|-------|
| `best_val_mse` | 9.020957 |
| `baseline_mse` | 7.466778 |
| `gate_mse` | 6.720100 |
| `gate.passed` | false |

The gate correctly fails for a 1-epoch model (as expected). A full training run needs to achieve `best_val_mse ≤ 6.720` to produce a promoted ONNX artifact.

## Line Ranges Modified in train_bass.py

| Lines (new) | Content |
|-------------|---------|
| 213–220 | Baseline MSE computation and gate threshold |
| 222–247 | Per-step metrics forward pass + pitch histogram |
| 249–264 | Expanded `validation.json` write |
| 266–274 | QGATE-01 gate enforcement (return 1 before export) |
| 276–296 | Existing ONNX export block (unchanged, now gated) |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed tuple-unpacking error in DataLoader iteration**
- **Found during:** Task 1 end-to-end test run
- **Issue:** `TensorDataset(x_va)` produces single-element tuples; `for bx, _ in ...` raised `ValueError: not enough values to unpack (expected 2, got 1)`
- **Fix:** Changed to `TensorDataset(x_va, y_va)` so the DataLoader yields `(bx, by)` pairs matching the `for bx, _ in` unpacking pattern
- **Files modified:** `training/train_bass.py` line 230
- **Commit:** 80cf287 (included in same task commit)

## Self-Check

### Files exist

- [x] `training/train_bass.py` — modified (line 230 DataLoader fix, lines 213–274 new post-training block)
- [x] `.planning/phases/33-model-quality-gates/33-01-SUMMARY.md` — this file

### Commits exist

- [x] `80cf287` — `feat(33-01): add QGATE-01 baseline gate + per-step metrics to train_bass.py`

## Self-Check: PASSED
