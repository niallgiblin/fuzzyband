---
phase: 18-pattern-model
plan: 01
subsystem: training
tags: [pytorch, onnx, pattern-classifier]

requires:
  - phase: 17-data-pipeline
    provides: "train.pt, val.pt, norm_stats.json"
provides:
  - "training/models/pattern_model.py with PatternNet and PatternOnnxExport"
affects:
  - "18-02 train_gmd"

tech-stack:
  added: []
  patterns:
    - "Normalization affine baked into export wrapper; trunk uses BatchNorm+Dropout on normalized features"

key-files:
  created:
    - training/models/__init__.py
    - training/models/pattern_model.py
  modified: []

key-decisions:
  - "Registered mean/std as buffers; std clamped with same epsilon floor as dataset pipeline"

patterns-established:
  - "from_norm_stats(Path) builds export wrapper from Phase 17 JSON"

requirements-completed: [PMODEL-01]

duration: 15min
completed: 2026-04-19
---

# Phase 18: Pattern Model — Plan 01 Summary

**PatternNet (5→32→16→7) plus ONNX export path that applies `norm_stats.json` affine before the MLP and emits int64 `Y` per `docs/ONNX_IO.md`.**

## Performance

- **Tasks:** 3
- **Files modified:** 2

## Accomplishments

- Added `training/models/` with `PatternNet` (BatchNorm1d + Dropout 0.2) and `PatternOnnxExport.from_norm_stats`.
- Export forward matches stub: `argmax` → int64 → `reshape(-1)` for batch 1.

## Deviations from Plan

None — plan executed as written.

## Issues Encountered

None.

---
*Phase: 18-pattern-model*
