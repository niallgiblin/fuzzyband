# ONNX default readiness checklist (Phase 36)

Tracks the six criteria from `36-CONTEXT.md` (D-19) before flipping `MA_ENABLE_ONNX` default to ON.

| # | Criterion | Threshold | Status | Evidence |
|---|-----------|-----------|--------|----------|
| 1 | Pattern macro-F1 (readiness fixture) | ≥ 0.65 | PASS | `training/fixtures/readiness/` + CI step **Readiness fixture evaluation (ONNXEVAL-02)** |
| 2 | Rule–ONNX agreement (same fixture) | ≥ 80% | PASS | `training/evaluate_feature_capture.py --min-rule-onnx-agreement 0.80` |
| 3 | ORT p99 latency (pattern, structure, bass) | < 5 ms each | PASS | Plan 04 `ctest -L onnx` — all three models < 5 ms p99 on M-series arm64 |
| 4 | Bass quality gate (QGATE-01) | export gate passed | PASS | Phase 33-01 — `assets/bass_model.onnx` + `training/tests/test_onnx_contract.py` |
| 5 | Structure norm baked in graph (QGATE-03) | normalization in ONNX | PASS | Phase 33-03 — `scripts/validate_onnx_contract.py --structure` |
| 6 | ONNX contract tests (all models) | all green | PASS | CI validates `--pattern`, `--structure`, `--bass` |

## Fallback policy (D-12)

Rule-path fallback is **retained** after default ONNX enablement. `OnnxInference::selectPattern` falls back to `PatternRules::rulePatternForState` when ONNX output is unacceptable or inference fails. Do not remove `RuleBasedInference` wiring in `AccompanimentProcessor`.

## Default build

`MA_ENABLE_ONNX` is **ON** by default. Configure the single local tree `build/`:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"
cmake --build build --config Release --parallel
```

## Re-verify commands

```bash
# Readiness gates
python3 training/evaluate_feature_capture.py \
  --capture training/fixtures/readiness/feature_capture.jsonl \
  --annotations training/fixtures/readiness/annotations.csv \
  --min-onnx-macro-f1 0.65 \
  --min-rule-onnx-agreement 0.80

# Contracts
python3 scripts/validate_onnx_contract.py \
  --pattern assets/accompaniment_model.onnx \
  --structure assets/structure_model.onnx \
  --bass assets/bass_model.onnx

# Latency
cmake --build build --target MetalAccompanimentTests --parallel
ctest --test-dir build -L onnx --output-on-failure
```

## Centroid proxy (Plan 36-02)

Spectral-centroid training proxy updated in `training/build_dataset.py` and `training/build_lakh_dataset.py`:

- Idle/sparse rows: **9853 Hz** (capture SILENT p50)
- Active rows: **8750 + (mean_note/127)×2450 Hz**
- Gap analyzer denormalizes merged `.pt` tensors and compares centroid **p50** (see `training/analyze_feature_proxy_gap.py`)

Promote new `assets/accompaniment_model.onnx` after `merge_datasets.py` + `train_gmd.py` complete.
