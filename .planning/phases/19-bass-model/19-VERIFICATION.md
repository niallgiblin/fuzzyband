---
status: passed
phase: 19-bass-model
verified: 2026-04-19
---

# Phase 19 — Verification

## Must-haves (from plans)

| Criterion | Evidence |
|-----------|----------|
| BMODEL-01: BassNet + BassOnnxExport in `training/models/bass_model.py` | File exists; 7→32→16→4, LayerNorm, no BatchNorm; `reshape(1, 4)` in export |
| BMODEL-01: Training pipeline without external bass corpus | `train_bass.py` uses `_generate_synthetic()` only |
| BMODEL-02: `validate_onnx_contract.py --bass` on trained artifact | Exit 0 on `training/artifacts/bass-synth-*/bass_trained.onnx` |
| README Phase 19 | `training/README.md` section before `## References` |

## Automated checks run

1. Plan 19-01 inline Python shape/dtype checks — PASSED  
2. `training/train_bass.py --max-epochs 5 --patience 3 --out-dir /tmp/bass-test-run` — PASSED  
3. `python scripts/validate_onnx_contract.py --bass <artifact>` — PASSED  
4. `metrics.jsonl` lines include `epoch`, `train_loss`, `val_loss` — PASSED  
5. `bass_norm_stats.json` has `mean`/`std` length 7 — PASSED  

## Human verification

- None required for this phase.

## Gaps

- None.
