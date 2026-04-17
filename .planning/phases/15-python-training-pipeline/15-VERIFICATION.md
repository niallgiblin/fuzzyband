---
status: passed
phase: 15-python-training-pipeline
verified: 2026-04-17
---

# Phase 15 verification — Python training pipeline

## Must-haves

| ID | Check | Evidence |
|----|--------|----------|
| PYTR-01 | Pinned deps + macOS-oriented venv install | `training/requirements.txt` (`==` pins); `training/README.md` Phase 15 section; `CONTRIBUTING.md` pointer |
| PYTR-02 | Training run with logged metrics | `training/train_pytr_stub.py` writes `metrics.jsonl` (epoch, pattern_loss, bass_loss) |
| PYTR-03 | Export ONNX matching frozen I/O | `pattern_trained.onnx` (`X`/`Y` per `docs/ONNX_IO.md`); `bass_trained.onnx` (`X_bass`/`Y_bass` per `docs/BASS_ONNX_IO.md`); `scripts/validate_onnx_contract.py` |

## Automated (local)

- `python3 training/train_pytr_stub.py --epochs 1 --out-dir /tmp/pytr-test-out` — exit 0; produces `metrics.jsonl`, `pattern_trained.onnx`, `bass_trained.onnx`
- `python3 scripts/validate_onnx_contract.py --pattern /tmp/pytr-test-out/pattern_trained.onnx --bass /tmp/pytr-test-out/bass_trained.onnx` — exit 0
- `ctest --test-dir build --output-on-failure` — 22/22 passed (regression gate)

## Human

- Full `pip install -r training/requirements.txt` on a clean macOS machine — recommended before release; stub verified with Python 3.13 + torch 2.6.0 in a temp venv.

## Notes

- `gsd-sdk` unavailable; execute-phase orchestration and `phase.complete` were applied manually in ROADMAP / STATE / REQUIREMENTS.
