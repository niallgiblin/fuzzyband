---
phase: 19-bass-model
reviewed: 2026-04-19
depth: standard
files_reviewed: 3
files_reviewed_list:
  - training/models/bass_model.py
  - training/train_bass.py
  - training/README.md
findings:
  critical: 0
  warning: 1
  info: 3
  total: 4
status: issues_found
---

# Code review — Phase 19 (bass model)

**Scope (from phase SUMMARYs):** `training/models/bass_model.py`, `training/train_bass.py`, `training/README.md`.

## Summary

No critical issues. One **warning**: `BassOnnxExport.forward` always returns `y.reshape(1, 4)` and is only correct when the normalized input batch has **N = 1** (the ONNX trace path). Batched `x` with **N > 1** will raise a reshape error or would be incorrect if reshaping were relaxed. Info items cover argparse validation, `feature_order` trust, and alignment with the phase threat model for `--out-dir`.

---

### WR-001 — `BassOnnxExport.forward` assumes batch size 1

**Severity:** warning  
**File:** `training/models/bass_model.py` (`BassOnnxExport.forward`)

After `y = self.bass_net(x_norm)` with shape `[N, 4]`, the code does `return y.reshape(1, 4)`. For **`N == 1`** (the intended ONNX dummy and plugin contract), this yields `[1, 4]` and satisfies `validate_onnx_contract.check_bass()`. For **`N > 1`**, `reshape(1, 4)` is invalid (element count mismatch) or would be wrong if changed naively.

**Mitigation:** Treat as a **batch-1-only** export wrapper: `assert x.shape[0] == 1` (or `y[:1]` with a comment) in `forward`, and/or document in the class docstring that only single-row `X_bass` is supported for export/inference.

---

### IN-001 — CLI numeric arguments unbounded

**Severity:** info  
**File:** `training/train_bass.py`

`--batch-size`, `--max-epochs`, `--patience`, and `--lr` are not validated for **≤ 0** or nonsensical combinations. Same pattern as Phase 18 `train_gmd.py`; fail-fast validators improve UX for local tooling.

---

### IN-002 — `feature_order` in `bass_norm_stats.json`

**Severity:** info  
**File:** `training/models/bass_model.py` (`from_norm_stats`)

Length **7** is enforced when `feature_order` is present, but order is not compared to the canonical column list used in `train_bass.py`. A hand-edited JSON with permuted features would still load; risk is low for the trusted local pipeline.

---

### IN-003 — Trust boundaries (matches phase threat model)

**Severity:** info  
**File:** `training/train_bass.py`

`--out-dir` writes ONNX and JSON under user control; `from_norm_stats` reads JSON produced by the same run. No network or elevated privilege — acceptable for developer-only training (see plan STRIDE table).

---

## Positive notes

- **`BassNet`:** LayerNorm + dropout, no BatchNorm — avoids N=1 batch crash in train mode (contrast Phase 18 `PatternNet` note).
- **`train_bass.py`:** Non-finite train/val loss guards, early stopping on val MSE, `onnx.checker` after export, opset 17 and `X_bass` / `Y_bass` names aligned with `check_bass()`.
- **README:** Phase 19 section mirrors Phase 18 structure; contract check command is explicit.

---

## Verdict

Suitable to ship for the documented bass training + ONNX workflow. Consider **WR-001** if `BassOnnxExport` is ever reused outside the single-row export path.
