---
phase: 19-bass-model
plan: 01
subsystem: training
tags: [pytorch, onnx, bass, BMODEL-01]

requires:
  - phase: 17
    provides: Feature tensor conventions
provides:
  - BassNet 7→32→16→4 MLP with LayerNorm+Dropout
  - BassOnnxExport with baked affine norm and rank-2 Y_bass [1,4]
affects: [phase-20]

tech-stack:
  added: []
  patterns: ["Mirror PatternNet; BassOnnxExport.forward → reshape(1,4) for validate_onnx_contract"]

key-files:
  created:
    - training/models/bass_model.py
  modified: []

key-decisions:
  - "Rank-2 ONNX output via y.reshape(1, 4) to satisfy check_bass() (not reshape(-1))"
  - "from_norm_stats enforces mean/std length 7"

patterns-established:
  - "Bass regression head without argmax; float32 throughout"

requirements-completed: [BMODEL-01]

duration: 15min
completed: 2026-04-19
---

# Phase 19 Plan 01 Summary

**Delivered `BassNet` and `BassOnnxExport` in `training/models/bass_model.py`, matching `PatternNet` structure with 7-D inputs, 4-D regression outputs, and ONNX export constraints from `validate_onnx_contract.check_bass()`.**

## Performance

- **Tasks:** 1
- **Files modified:** 1 created

## Accomplishments

- Implemented 7→32→16→4 MLP with LayerNorm and dropout (no BatchNorm).
- BassOnnxExport loads `bass_norm_stats.json`-style JSON and returns `[1, 4]` float32 for plugin contract.

## Self-Check

- `training/.venv/bin/python -c "..."` from plan: **PASSED** (“All checks passed”).

## Deviations

- None.
