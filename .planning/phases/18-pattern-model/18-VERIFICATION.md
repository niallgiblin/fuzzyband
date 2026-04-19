---
phase: 18-pattern-model
status: passed
verified: 2026-04-19
---

# Phase 18 — Verification

**Goal:** Trained pattern MLP with normalization in ONNX passes frozen `docs/ONNX_IO.md` contract.

## Must-haves

| Criterion | Evidence |
|-----------|----------|
| PMODEL-01: `PatternNet` 5→32→16→7 + BN + Dropout; `PatternOnnxExport` bakes mean/std then argmax → int64 | `training/models/pattern_model.py`; import smoke test with `.eval()` |
| PMODEL-02: `train_gmd.py` loads `train.pt`/`val.pt`, weighted CE, macro-F1 early stop, `metrics.jsonl`, opset 17 export | Script run completed; `metrics.jsonl` + `pattern_trained.onnx` under `training/artifacts/pattern-gmd-*/` |
| PMODEL-03: `validate_onnx_contract.py --pattern` exit 0 | Command run on artifact from training session; exit 0 |

## Automated checks run

- `training/.venv/bin/python training/train_gmd.py` (early-stopped run on repo `training/data/processed`)
- `training/.venv/bin/python scripts/validate_onnx_contract.py --pattern <artifact_path>`

## Human verification

None required for this phase.

## Gaps

None.
