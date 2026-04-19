---
phase: 19-bass-model
plan: 02
subsystem: training
tags: [onnx, synthetic-data, BMODEL-01, BMODEL-02]

requires:
  - phase: 19-01
    provides: bass_model.py
provides:
  - training/train_bass.py CLI with synthetic E2/A2/B1 data and MSE training
  - Timestamped `training/artifacts/bass-synth-*/` with bass_trained.onnx, bass_norm_stats.json, metrics.jsonl
  - training/README.md Phase 19 section
affects: [phase-20]

tech-stack:
  added: []
  patterns: ["train_gmd.py structure; MSE early stopping on val_loss"]

key-files:
  created:
    - training/train_bass.py
  modified:
    - training/README.md

key-decisions:
  - "N=3000 synthetic rows, 80/20 split, pitch roots balanced via tile+shuffle"
  - "Export uses opset 17, X_bass / Y_bass tensor names"

patterns-established:
  - "In-script synthetic bass corpus (no GMD bass tracks)"

requirements-completed: [BMODEL-01, BMODEL-02]

duration: 45min
completed: 2026-04-19
---

# Phase 19 Plan 02 Summary

**Added `train_bass.py` for synthetic bass training, ONNX export, and README instructions; `scripts/validate_onnx_contract.py --bass` passes on the trained `bass_trained.onnx` under `training/artifacts/bass-synth-*/`.**

## Performance

- **Tasks:** 2
- **Files modified:** 1 created, 1 updated

## Accomplishments

- Full training run with early stopping produced a contract-valid bass ONNX artifact.
- README documents `train_bass.py` and `--bass` validation alongside Phase 18 pattern docs.

## Self-Check

- Quick run: `--max-epochs 5 --out-dir /tmp/bass-test-run` — artifacts present.
- Full default run: `training/artifacts/bass-synth-20260419-122504/bass_trained.onnx`.
- `validate_onnx_contract.py --bass` — **PASSED**.

## Deviations

- None.
