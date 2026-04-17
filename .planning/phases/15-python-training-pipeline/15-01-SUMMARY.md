---
phase: 15-python-training-pipeline
plan: 01
subsystem: training
tags: [python, pip, reproducibility]

requires:
  - phase: 09-data-training-strategy
    provides: prep_midi.py, training/README baseline
provides:
  - Pinned training/requirements.txt (mido, numpy, onnx, onnxruntime, torch)
  - Phase 15 (PYTR) install docs in training/README.md
  - Gitignore rules for training/artifacts and checkpoints
affects: [phase-15-plan-02]

tech-stack:
  added: [torch==2.6.0, numpy==2.1.3, onnx==1.16.2, onnxruntime==1.20.1]
  patterns: [single requirements.txt for prep + train + export]

key-files:
  created: []
  modified:
    - training/requirements.txt
    - training/README.md
    - .gitignore
    - CONTRIBUTING.md

key-decisions:
  - "torch pinned to 2.6.0 for Python 3.13+ wheel availability (2.2.x not published for this runtime)"

patterns-established:
  - "Repo-root venv at training/.venv with pip install -r training/requirements.txt"

requirements-completed: [PYTR-01]

duration: 25min
completed: 2026-04-17
---

# Phase 15 plan 01 — PYTR reproducibility

**Pinned Python stack and docs so macOS developers can recreate the training environment without committing generated weights.**

## Performance

- **Tasks:** 4
- **Files modified:** 4

## Accomplishments

- Pinned `mido`, `numpy`, `onnx`, `onnxruntime`, and `torch` with `==` in `training/requirements.txt`.
- Documented venv + install commands and Phase 15 artifact layout in `training/README.md`; linked ONNX I/O contracts.
- Ignored `training/artifacts/` and `training/**/*.pt` / `.pth` in `.gitignore`.
- Pointed CONTRIBUTING’s ONNX section at `training/README.md` as the single pin source.

## Self-Check: PASSED

- Grep acceptance criteria from plan all exit 0.
