---
status: partial
phase: 18-pattern-model
source: [18-01-SUMMARY.md, 18-02-SUMMARY.md, 18-03-SUMMARY.md]
started: 2026-04-19T00:00:00Z
updated: 2026-04-19T09:00:00Z
---

## Current Test

[testing paused — 2 items blocked on prior-phase (Phase 17 data shards)]

## Tests

### 1. PatternNet model file and architecture
expected: training/models/pattern_model.py contains PatternNet (5→32→16→7 MLP with BatchNorm1d + Dropout 0.2) and PatternOnnxExport with from_norm_stats(). The export wrapper reads norm_stats.json, applies the affine, then argmax → int64 reshape(-1). Import should work: python -c "from training.models.pattern_model import PatternNet, PatternOnnxExport; print('ok')" (run from repo root with training/.venv).
result: pass

### 2. Training run produces artifacts
expected: Running training/.venv/bin/python training/train_gmd.py --data-dir training/data (or wherever Phase 17 shards live) completes without error. Under training/artifacts/pattern-gmd-<timestamp>/ you find pattern_trained.onnx and metrics.jsonl. Metrics log should show per-epoch train_loss and val_f1. Early stopping fires on val macro-F1 plateau (patience 8).
result: blocked
blocked_by: prior-phase
reason: "error: missing /Users/ng/Desktop/fuzzyband/training/data/train.pt or /Users/ng/Desktop/fuzzyband/training/data/val.pt — Phase 17 shards not yet generated"

### 3. ONNX contract validation passes
expected: Running training/.venv/bin/python scripts/validate_onnx_contract.py --pattern <path-to-pattern_trained.onnx> exits 0. The validator confirms opset 17, input X is float32 [1,5], output Y is int64 scalar-like (shape [1] or scalar).
result: blocked
blocked_by: prior-phase
reason: "Depends on pattern_trained.onnx from Test 2, which is blocked on Phase 17 data shards"

## Summary

total: 3
passed: 1
issues: 0
pending: 0
skipped: 0
blocked: 2

## Gaps

[none yet]
