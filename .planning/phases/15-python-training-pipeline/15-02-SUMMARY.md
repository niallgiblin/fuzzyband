---
phase: 15-python-training-pipeline
plan: 02
subsystem: training
tags: [pytorch, onnx, export]

requires:
  - phase: 15-python-training-pipeline
    provides: 15-01 pinned deps + docs
provides:
  - training/train_pytr_stub.py synthetic train + dual ONNX export
  - scripts/validate_onnx_contract.py vs ONNX_IO / BASS_ONNX_IO
affects: [plugin-onnx-assets-dev]

tech-stack:
  added: []
  patterns: [metrics.jsonl per epoch; timestamped training/artifacts/pytr-stub-*]

key-files:
  created:
    - training/train_pytr_stub.py
    - scripts/validate_onnx_contract.py
  modified:
    - training/README.md

key-decisions:
  - "Pattern export uses argmax → int64 Y[1] aligned with OnnxInference.cpp"
  - "Stub trains PatternStub (CE) and BassStub (SmoothL1) on synthetic batches"

patterns-established:
  - "Local validation: python3 scripts/validate_onnx_contract.py --pattern … --bass …"

requirements-completed: [PYTR-02, PYTR-03]

duration: 35min
completed: 2026-04-17
---

# Phase 15 plan 02 — Stub train + ONNX export + contract validation

**One-shot synthetic training run logs per-epoch losses, exports `pattern_trained.onnx` and `bass_trained.onnx`, and validates names/shapes against frozen docs.**

## Performance

- **Tasks:** 4
- **Files created:** 2

## Accomplishments

- `training/train_pytr_stub.py`: synthetic data, Adam loop, `metrics.jsonl`, `torch.onnx.export` + `onnx.checker` for pattern and bass graphs.
- `scripts/validate_onnx_contract.py`: argparse, shape/type checks for `X`/`Y` and `X_bass`/`Y_bass`.
- README subsection for stub run and validate commands.

## Verification

- Ran: `train_pytr_stub.py --epochs 1 --out-dir /tmp/pytr-test-out`; `validate_onnx_contract.py` on outputs — exit 0.
- Repo `ctest`: 22/22 passed (no C++ changes).

## Self-Check: PASSED
