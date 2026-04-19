---
phase: 18-pattern-model
status: clean
reviewed: 2026-04-19
depth: standard
---

# Phase 18 — Code review

**Scope:** `training/models/pattern_model.py`, `training/train_gmd.py`, `training/README.md` (Phase 18 section).

## Findings

None blocking.

## Notes

- `PatternNet` in train mode with batch size 1 would trip BatchNorm; training uses shuffled batches from real GMD tensors (typically >1 per batch). Export uses `.eval()`.
- `--data-dir` outside `training/` emits a `warnings.warn` (not `UserWarning` subclass) — acceptable for developer tooling.

## Summary

No security or correctness issues identified in scope; follow-up testing is covered by ONNX contract validation.
