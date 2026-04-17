# ONNX I/O Contract — Generative Bass (Phase 13)

> **Status:** Phase 13 generative bass graph — **separate** from the frozen pattern selector (`X` / `Y` in this repo’s pattern contract) and from structure (`X_struct` / `Y_struct`).
> **Scope:** Plugin-side tensor contract for `assets/bass_model.onnx` and `src/inference/OnnxBassInference.cpp` (when implemented).
> Requirement refs: **GBASS-01** (contract + asset), **PYTR-03** (Phase 15 export must match).

Training and export pipelines (Phase 15) **must** produce a model that matches this contract. Changing names, shapes, dtypes, or column semantics requires a requirements update.

---

## Input tensor

| Property | Value |
|----------|-------|
| Name | `X_bass` |
| Dtype | `float32` |
| Shape | `[1, 7]` (row-major) |
| Semantics | Single feature window per bass inference step (background thread, same cadence family as other ONNX heads) |

### Feature order (row `X_bass[0]`, columns 0–6)

| Index | Field | Source (`FeatureVector`) | Notes |
|-------|-------|--------------------------|-------|
| 0 | `bpm` | `FeatureVector::bpm` | float |
| 1 | `rmsEnergy` | `FeatureVector::rmsEnergy` | float |
| 2 | `spectralCentroid` | `FeatureVector::spectralCentroid` | float; Hz |
| 3 | `highFreqFlux` | `FeatureVector::highFreqFlux` | float |
| 4 | `float(static_cast<int>(state))` | `StructureState` as numeric cast | Same convention as pattern `X` / ONNX pattern packing |
| 5 | `pitchRootMidi` | Root pitch (MIDI note number) | From Phase 11 pitch path |
| 6 | `pitchConfidence` | Pitch confidence | [0, 1] typical |

The column order aligns with **`FeatureVector`** and the conventions used by **`OnnxInference`** / **`OnnxStructureInference`** for feature packing (extended with pitch fields for conditioning).

---

## Output tensor

| Property | Value |
|----------|-------|
| Name | `Y_bass` |
| Dtype | `float32` |
| Shape | `[1, 4]` |

### Column order (row `Y_bass[0]`, indices 0–3)

| Index | Field | Semantics |
|-------|-------|-----------|
| 0 | `proposal_confidence` | **Gating** — expected in **[0, 1]** (plugin may apply thresholds before rank/select). |
| 1 | `root_midi` | Proposed bass root as MIDI note number (float stored; interpreted as note). |
| 2 | `duration_beats` | Note length in **beats** (bar/clock alignment uses existing `PatternPlayer` semantics). |
| 3 | `margin` | Reserved for **rank/select** vs other candidates; plugin may ignore initially. |

---

## Bundled model asset

| Property | Value |
|----------|-------|
| Asset path | `assets/bass_model.onnx` |
| Delivery | JUCE `BinaryData` symbols `BinaryData::bass_model_onnx` and `bass_model_onnxSize` (plus length pattern consistent with sibling `.onnx` assets) when `MA_ENABLE_ONNX=ON` |
| Regeneration | `training/.venv/bin/python scripts/build_minimal_bass_onnx.py` (see `CONTRIBUTING.md` §ONNX Runtime) |

The repository ships a **minimal stub** graph that satisfies the contract so integration and tests can run before Phase 15 training. If load fails or `Run()` throws, the processor must fall back to **static pattern bass** (no generative rank) — see Phase 13 implementation plans.

---

## Handoff to Phase 15 (PYTR-03)

Any training or export pipeline landing in Phase 15 **must** emit a graph whose
input `X_bass` / output `Y_bass` names, shapes, dtypes, and column semantics match
this document. The spirit matches **`docs/ONNX_IO.md`**: byte-stable contracts for
the plugin, with regeneration scripts and CI posture documented in **`CONTRIBUTING.md`**.
