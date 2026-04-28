# ONNX I/O Contract — Metal Accompaniment

> **Status:** Frozen for milestone **v0.4.0** (Phase 22).
> **Scope:** Plugin-side tensor contract consumed by `src/inference/OnnxInference.cpp`.
> Requirement refs: **ONNX-04** (structure contract), **PYTR-03** (Phase 15 export must target this graph).

This document defines the **exact** ONNX graph inputs and outputs that the plugin
expects when built with `-DMA_ENABLE_ONNX=ON`. Training and export pipelines
**must** produce a model that matches this contract byte-for-byte.

**See also:** Phase 13 generative bass uses a separate graph and tensor names **`X_bass` / `Y_bass`** — [`docs/BASS_ONNX_IO.md`](BASS_ONNX_IO.md). The **`X` / `Y` pattern-selector tables below are frozen** and are not modified for bass.

---

## Input tensor

| Property | Value |
|----------|-------|
| Name | `X` |
| Dtype | `float32` |
| Shape | `[1, 7]` (row-major) |
| Semantics | Single feature window per `selectPattern()` call |

### Feature order (row `X[0]`, columns 0–6)

| Index | Field | Source (`FeatureVector`) | Notes |
|-------|-------|--------------------------|-------|
| 0 | `bpm` | `FeatureVector::bpm` | float; clamped by `OnsetDetector` to [80, 220] |
| 1 | `rmsEnergy` | `FeatureVector::rmsEnergy` | float; rolling 100 ms RMS, ~[0, 1] |
| 2 | `spectralCentroid` | `FeatureVector::spectralCentroid` | float; Hz |
| 3 | `highFreqFlux` | `FeatureVector::highFreqFlux` | float; 2 kHz+ band flux |
| 4 | `stateSILENT` | One-hot: `1.0` when `FeatureVector::state == SILENT`, else `0.0` | Three-class one-hot encoding |
| 5 | `stateSOFT` | One-hot: `1.0` when `FeatureVector::state == SOFT`, else `0.0` | See class order below |
| 6 | `stateLOUD` | One-hot: `1.0` when `FeatureVector::state == LOUD`, else `0.0` | Mutually exclusive (exactly one is 1.0) |

**Class order:** `SILENT`, `SOFT`, `LOUD` — matches `enum class StructureState` in `src/analysis/StructureTagger.h`.

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

## Structure model (Phase 12)

> **Independent graph:** This head is **not** the pattern selector above. Tensor names **`X` / `Y`** and their tables remain frozen (**D-10-06**). Structure classification uses **`X_struct` / `Y_struct`** only.

| Property | Value |
|----------|-------|
| Name | `X_struct` |
| Dtype | `float32` |
| Shape | `[1, 12, 7]` (batch **N**=12 snapshots, **K**=7 features per snapshot) |

**Layout:** Row-major with dimension order `[batch][snapshot_index][feature]`. **`snapshot_index` 0 is the oldest** sample in the 12-frame window; index **11** is the **newest** (most recent `FeatureVector` pushed before the run).

### Feature order per snapshot (K = 7)

| Index | Field | Source (`FeatureVector`) |
|-------|-------|--------------------------|
| 0 | `bpm` | `FeatureVector::bpm` |
| 1 | `rmsEnergy` | `FeatureVector::rmsEnergy` |
| 2 | `spectralCentroid` | `FeatureVector::spectralCentroid` |
| 3 | `highFreqFlux` | `FeatureVector::highFreqFlux` |
| 4 | `float(static_cast<int>(state))` | Numeric cast of `StructureState` (`SILENT=0`, `SOFT=1`, `LOUD=2`) |
| 5 | `pitchRootMidi` | `FeatureVector::pitchRootMidi` |
| 6 | `pitchConfidence` | `FeatureVector::pitchConfidence` |

### Output tensor (structure head)

| Property | Value |
|----------|-------|
| Name | `Y_struct` |
| Dtype | `float32` |
| Shape | `[1, 3]` **logits** (pre-softmax) |

**Class order (logits index → label):** `SILENT`, `SOFT`, `LOUD` — same order as `enum class StructureState` in `src/analysis/StructureTagger.h`.

The plugin applies **softmax** in C++ and applies confidence / margin gates before hold smoothing. Phase 15 training export **must** emit logits consistent with this head (not probabilities).

| Property | Value |
|----------|-------|
| Asset path | `assets/structure_model.onnx` |
| Delivery | JUCE `BinaryData` symbol `BinaryData::structure_model_onnx` (and `structure_model_onnxSize`) when `MA_ENABLE_ONNX=ON` |
| Regeneration | `training/.venv/bin/python scripts/build_minimal_structure_onnx.py` (see `CONTRIBUTING.md` §ONNX Runtime) |

---

## Handoff to Phase 15 (PYTR-03)

Any training or export pipeline landing in Phase 15 **must** emit a graph whose
input `X` / output `Y` names, shapes, dtypes, and value ranges match the table
above. Changing this contract requires a breaking-phase update to
`.planning/REQUIREMENTS.md` (ONNX-04).

---

## References

- Code: `src/inference/OnnxInference.cpp` (input packing, `Run()` call, clamp)
- Feature struct: `src/analysis/FeatureVector.h`
- Structure enum: `src/analysis/StructureTagger.h` (`enum class StructureState`)
- Build flags: `CMakeLists.txt` (`MA_ENABLE_ONNX`, `ONNXRUNTIME_ROOT`)
- Contributor setup: `CONTRIBUTING.md` §ONNX Runtime (optional — Phase 10)
