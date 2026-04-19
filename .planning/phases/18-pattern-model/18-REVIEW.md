---
phase: 18-pattern-model
reviewed: 2026-04-19
depth: standard
files_reviewed: 4
files_reviewed_list:
  - training/models/__init__.py
  - training/models/pattern_model.py
  - training/train_gmd.py
  - training/README.md
findings:
  critical: 0
  warning: 2
  info: 3
  total: 5
status: issues_found
---

# Code review — Phase 18 (pattern model)

**Scope (from phase SUMMARYs):** `training/models/__init__.py`, `training/models/pattern_model.py`, `training/train_gmd.py`, `training/README.md`.

## Summary

No critical issues. Two warnings: **BatchNorm** can throw in training mode when an effective batch has **N=1**, and **val loss** uses unweighted CE while training uses **class-weighted** CE, so logged train/val losses are not directly comparable. Info items cover `feature_order` validation, optional CLI bounds, and the usual **torch.load** trust boundary.

---

### WR-001 — `BatchNorm1d` + `train()` with batch size 1

**Severity:** warning  
**File:** `training/train_gmd.py` (training loop), `training/models/pattern_model.py` (`PatternNet`)

PyTorch `BatchNorm1d` in **training** mode rejects a single sample per batch (`Expected more than 1 value per channel when training`). If the train set is very small or **`--batch-size`** is large enough that the only batch has size 1 (or the last batch is size 1 in edge configurations), training can crash. Real GMD splits are usually large enough that this does not fire.

**Mitigation ideas:** Lower default batch size for tiny dev sets, use `BatchNorm` only in `eval` via frozen stats (larger change), or document a minimum train count / max batch-size relationship.

---

### WR-002 — Train loss weighted, validation loss unweighted

**Severity:** warning  
**File:** `training/train_gmd.py`

Training uses `CrossEntropyLoss(weight=weights)`. Validation uses `nn.functional.cross_entropy(logits, by, reduction="mean")` **without** the same weights. **`val_macro_f1`** drives early stopping (correct per plan), but **`val_loss`** in `metrics.jsonl` is not on the same scale as **`train_loss`**, which can confuse monitoring or plots.

**Mitigation:** Use weighted val CE for logging (same `weight` tensor), or rename keys / document the asymmetry in README.

---

### IN-001 — `feature_order` length only

**Severity:** info  
**File:** `training/models/pattern_model.py` (`from_norm_stats`)

`norm_stats.json` **`feature_order`** is checked for length 5 when present, but not compared to the canonical order from `build_dataset.py`. A hand-edited JSON with permuted order would still load. Low risk for the intended local pipeline.

---

### IN-002 — `torch.load` trust boundary

**Severity:** info  
**File:** `training/train_gmd.py` (`_load_split`)

`torch.load(..., weights_only=False)` is required for dict checkpoints but executes unpickling. Acceptable for **trusted** `train.pt`/`val.pt` from your own `build_dataset.py`; do not point `--data-dir` at untrusted paths.

---

### IN-003 — CLI numeric bounds

**Severity:** info  
**File:** `training/train_gmd.py`

`--batch-size`, `--max-epochs`, and `--patience` are not guarded against **0** or negative values. argparse `type` with a small validator (as in `train_pytr_stub._positive_epochs`) would fail fast.

---

## Positive notes

- **`PatternOnnxExport`:** Mean/std as buffers, `std` clamped, `eval()` before export matches ONNX contract expectations.
- **`train_gmd.py`:** Non-finite loss guard, best checkpoint by macro-F1, `onnx.checker` after export, UTC artifact dirs, path warning outside `training/`.
- **README:** Clear venv + absolute path for `validate_onnx_contract.py`.

---

## Verdict

Suitable to ship for the documented GMD workflow. Address **WR-001** if you expect very small train shards or large batch sizes in tests; consider **WR-002** for clearer metrics.
