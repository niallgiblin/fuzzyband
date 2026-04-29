# Phase 26 — WIP absorption (D-26-12)

Traceability remap from the prior unfinished agent run onto Phase 26 execution plans.

| file | disposition | covered_by_plan | concrete_action |
|------|-------------|-----------------|----------------|
| `scripts/build_minimal_pattern_onnx.py` | verify-as-baseline | 26-02 (contract checks) | Confirms ONNX stub emits `X [1,7]` for pattern path. Baseline runnable check recorded below. |
| `scripts/validate_onnx_contract.py` | verify-as-baseline | 26-02 (`check_pattern`, `check_bass`, `check_structure`) | Canonical shape gate for `--pattern/--bass/--structure`; unchanged here. Baseline rg below. |
| `src/inference/IInference.h` | verify-as-baseline | Phase 23 (INF-02) absorbed | `excludeIndex` + `FeatureVector`-based selection API documented in Phase 26 context. rg below. |
| `src/inference/OnnxInference.h` | verify-as-baseline | Phase 23 (INF-01/02) | ONNX pattern path + exclusions. rg below. |
| `src/inference/RuleBasedInference.h` | verify-as-baseline | Phase 23 (INF-02) | Rule fallback `excludeIndex` parity. rg below. |
| `src/inference/OnnxBassInference.h` | verify-as-baseline | Phase 23 (INF-01) | Piano-roll bass `[1,32]` contract. rg below. |
| `src/inference/OnnxBassInference.cpp` | verify-as-baseline | Phase 23 (INF-01) | Load-time assertion + `[1,32]` decode loop. rg below. |
| `src/midi/PatternPlayer.h` | verify-as-baseline | Phase 23 + 26-01 UI precursor | Generative bass step API wired for piano-roll. rg below. |
| `src/midi/PatternPlayer.cpp` | verify-as-baseline | Phase 23 | `emitGenerativeBassSteps` implementation. rg below. |
| `src/AccompanimentProcessor.h` | verify-as-baseline | Phase 23 (INF-02) + Phase 26-01 inference label hook | `patternRejectionCount` + optional `activeInferenceName` accessor. rg below. |
| `training/merge_datasets.py` | verify-as-baseline | Phase 25 / 26-02 data ingest | Safe `weights_only=True` loads; absorbs prior WIP. rg below. |
| `training/train_gmd.py` | execute in `26-02` | 26-02-PLAN — PatternNet macro-F1 + `val_gmd` gate | Retrain sweep for PatternNet macro-F1 ≥ 0.55 on fixed `val_gmd.pt`; class-wise metrics retained. |

## Baseline `verify_command` runs (frozen 2026-04-29)

Each block: `verify_command` label → command → `result: pass` when exit 0 **and** at least one match.

### `scripts/build_minimal_pattern_onnx.py`

verify_command:

```bash
rg '\[1,\s*7\]' scripts/build_minimal_pattern_onnx.py
```
result: pass

### `scripts/validate_onnx_contract.py`

verify_command:

```bash
rg "check_pattern|\[1,7\]|\[1,32\]|\[1,12,7\]" scripts/validate_onnx_contract.py
```
result: pass

### Inference headers — `excludeIndex`

verify_command:

```bash
rg "excludeIndex" src/inference/IInference.h src/inference/OnnxInference.h src/inference/RuleBasedInference.h
```
result: pass

### ONNX bass inference — piano-roll / 32 / interleaved

verify_command:

```bash
rg "32|piano|interleaved|BassPianorollStep" src/inference/OnnxBassInference.h src/inference/OnnxBassInference.cpp
```
result: pass

### PatternPlayer generative bass

verify_command:

```bash
rg "emitGenerativeBassSteps|setGenerativeBassSteps" src/midi/PatternPlayer.h src/midi/PatternPlayer.cpp
```
result: pass

### AccompanimentProcessor — rejection + inference name plumbing

verify_command:

```bash
rg "patternRejectionCount|getActiveInferenceName" src/AccompanimentProcessor.h
```
result: pass

### `training/merge_datasets.py`

verify_command:

```bash
rg "weights_only" training/merge_datasets.py
```
result: pass

### `training/train_gmd.py`

verify_command:

```bash
rg "class-wise|confusion|best_model|val_gmd" training/train_gmd.py
```
result: pass
