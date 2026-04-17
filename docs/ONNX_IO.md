# ONNX I/O Contract — Metal Accompaniment

> **Status:** Frozen for milestone **v0.2.0** (Phase 10).
> **Scope:** Plugin-side tensor contract consumed by `src/inference/OnnxInference.cpp`.
> Requirement refs: **ONNX-02** (handoff), **PYTR-03** (Phase 15 export must target this graph).

This document defines the **exact** ONNX graph inputs and outputs that the plugin
expects when built with `-DMA_ENABLE_ONNX=ON`. Training and export pipelines
(Phase 15) **must** produce a model that matches this contract byte-for-byte.

---

## Input tensor

| Property | Value |
|----------|-------|
| Name | `X` |
| Dtype | `float32` |
| Shape | `[1, 5]` (row-major) |
| Semantics | Single feature window per `selectPattern()` call |

### Feature order (row `X[0]`, columns 0–4)

| Index | Field | Source (`FeatureVector`) | Notes |
|-------|-------|--------------------------|-------|
| 0 | `bpm` | `FeatureVector::bpm` | float; clamped by `OnsetDetector` to [80, 220] |
| 1 | `rmsEnergy` | `FeatureVector::rmsEnergy` | float; rolling 100 ms RMS, ~[0, 1] |
| 2 | `spectralCentroid` | `FeatureVector::spectralCentroid` | float; Hz |
| 3 | `highFreqFlux` | `FeatureVector::highFreqFlux` | float; 2 kHz+ band flux |
| 4 | `float(state)` | `static_cast<float>(static_cast<int>(FeatureVector::state))` | Numeric cast of `StructureState` enum (`SILENT=0`, `VERSE=1`, `CHORUS=2`, `BREAKDOWN=3`) |

The packing order is canonical and **must match** the C++ packing in
`OnnxInference::selectPattern` (see `src/inference/OnnxInference.cpp`).

---

## Output tensor

| Property | Value |
|----------|-------|
| Name | `Y` |
| Dtype | `int64` |
| Shape | `[1]` (scalar-like) |
| Semantics | Pattern index into `MidiPatternLibrary` |

### Runtime clamp

The plugin reads `Y[0]` as `int64_t` and clamps to **[0, 6]** (`std::clamp`)
before handing the value to `PatternPlayer` via the atomic pattern index.
Values outside `[0, 6]` are not an error — they are silently clamped. Exporters
should still target `[0, 6]` because any broader output range is equivalent to
the saturated endpoints.

---

## Bundled model asset

| Property | Value |
|----------|-------|
| Asset path | `assets/accompaniment_model.onnx` |
| Delivery | JUCE `BinaryData` symbol `BinaryData::accompaniment_model_onnx` (built in via CMake when `MA_ENABLE_ONNX=ON`) |
| Regeneration | `training/.venv/bin/python scripts/build_minimal_pattern_onnx.py` (see `CONTRIBUTING.md` §ONNX Runtime) |

Phase 10 ships a **minimal pattern stub** that satisfies the contract so
`tryLoadModel()` succeeds on ONNX-enabled builds without waiting for Phase 15.
If load fails or `Run()` throws, the processor falls back to
`RuleBasedInference` — see `src/AccompanimentProcessor.cpp` `makeInference()`.

---

## Handoff to Phase 15 (PYTR-03)

Any training or export pipeline landing in Phase 15 **must** emit a graph whose
input `X` / output `Y` names, shapes, dtypes, and value ranges match the table
above. Changing this contract requires a breaking-phase update to
`.planning/REQUIREMENTS.md` (ONNX-02).

---

## References

- Code: `src/inference/OnnxInference.cpp` (input packing, `Run()` call, clamp)
- Feature struct: `src/analysis/FeatureVector.h`
- Structure enum: `src/analysis/StructureTagger.h` (`enum class StructureState`)
- Build flags: `CMakeLists.txt` (`MA_ENABLE_ONNX`, `ONNXRUNTIME_ROOT`)
- Contributor setup: `CONTRIBUTING.md` §ONNX Runtime (optional — Phase 10)
