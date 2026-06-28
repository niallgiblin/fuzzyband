# ONNX I/O Contract — Metal Accompaniment (v0.8.x)

> **Status:** Updated for **v0.8.0** unified Mel-CNN pipeline.
> **Scope:** Plugin-side tensor contracts consumed by `src/inference/OnnxInference.cpp` (legacy) and `src/inference/MetalGrooveInference.cpp` (current).
> Requirement refs: SIMPLIFY.md §2.1–2.2.

This document defines the **exact** ONNX graph inputs and outputs that the plugin
expects when built with `-DMA_ENABLE_ONNX=ON`.

---

## Current model: `metal_groove.onnx` (v0.8.x, Mel-CNN pipeline)

### Overview

A single ONNX model with a shared MelCNN backbone (3 conv blocks → 128-dim bottleneck) and three output heads. Replaces the previous three separate models (pattern, structure, bass).

### Input tensor

| Property | Value |
|----------|-------|
| Name | `mel` |
| Dtype | `float32` |
| Shape | `[batch, 1, 64, 32]` |
| Semantics | 64 mel bands × 32 time frames (~370ms context), mel-dB values in [-80, 0]. C++-aligned via `MelSpectrogramExtractor`. |

### Output tensors

| Name | Dtype | Shape | Semantics |
|------|-------|-------|-----------|
| `bottleneck` | `float32` | `[batch, 128]` | 128-dim CNN bottleneck — used with precomputed centroids for cosine-similarity nearest-neighbor pattern lookup |
| `style_logits` | `float32` | `[batch, 5]` | Playing-style logits (palm_mute, open_chord, single_note, sustain, silence) — free with groove inference |
| `groove_embedding` | `float32` | `[batch, 64]` | 64-dim L2-normalized embedding (reserved for future triplet-loss refinement; not used in current nearest-neighbor path) |

### Runtime inference flow

1. Audio thread accumulates 22,050 samples in `AudioRingBuffer` (~512ms @ 44.1kHz)
2. `MelSpectrogramExtractor::process()` produces 2048 mel-dB floats
3. Mel data enqueued to `melQueue` (lock-free SPSC)
4. Background thread drains `melQueue`, calls `MetalGrooveInference::selectPatternFromMel()`
5. ONNX session runs, returns `bottleneck` [1, 128]
6. Cosine similarity computed against 22 precomputed centroids (`pattern_embeddings.h`)
7. Best-match pattern index [0–21] stored in `latestPatternIndex` atomic
8. `PatternPlayer` emits MIDI on next bar boundary

### Pattern embeddings (centroids)

| Property | Value |
|----------|-------|
| File | `src/inference/pattern_embeddings.h` (auto-generated) |
| Format | `std::array<std::array<float, 128>, 22>` — 22 patterns × 128-dim |
| Generation | `training/export_centroids.py` — runs training data through trained backbone, computes per-class mean bottleneck |
| Lookup | Cosine similarity nearest-neighbor (argmax dot product / norms) |

### Bundled model asset

| Property | Value |
|----------|-------|
| Asset path | `assets/metal_groove.onnx` |
| Size | ~685 KB (internal data, no external weights) |
| Delivery | JUCE `BinaryData` symbol `BinaryData::metal_groove_onnx` |
| Regeneration | `training/train_groove_model.py` → `training/export_centroids.py` |
| Opset | 18 |

### Inference latency

| Metric | Value |
|--------|-------|
| p99 latency | <1ms per ONNX `Run()` call |
| Thread | Background inference thread (~50Hz drain) |
| Fallback | `OnnxInference` (legacy scalar-feature) → `RuleBasedInference` |

---

## Legacy model: `accompaniment_model.onnx` (v0.6.x, scalar-feature)

> **Still bundled as fallback.** If `metal_groove.onnx` fails to load, the factory falls back to this model via `OnnxInference`.

### Input tensor

| Property | Value |
|----------|-------|
| Name | `X` |
| Dtype | `float32` |
| Shape | `[1, 7]` |
| Semantics | Single feature window per `selectPattern()` call; column 0 is **adjusted BPM** |

### Feature order (row `X[0]`, columns 0–6)

| Index | Field | Source |
|-------|-------|--------|
| 0 | `adjustedBpm` | `PatternRules::adjustedBpm(f)` |
| 1 | `rmsEnergy` | `FeatureVector::rmsEnergy` |
| 2 | `spectralCentroid` | `FeatureVector::spectralCentroid` |
| 3 | `highFreqFlux` | `FeatureVector::highFreqFlux` |
| 4 | `stateSILENT` | One-hot: `1.0` when SILENT |
| 5 | `stateSOFT` | One-hot: `1.0` when SOFT |
| 6 | `stateLOUD` | One-hot: `1.0` when LOUD |

### Output tensor

| Property | Value |
|----------|-------|
| Name | `Y` |
| Dtype | `int64` |
| Shape | `[1]` |
| Clamp | `[0, 21]` (was `[0, 6]` before pattern library expansion) |

---

## Removed models (v0.8.0)

These models are **no longer bundled** and their source files are removed from the build:

| Model | File | Why |
|-------|------|-----|
| Structure classifier | `assets/structure_model.onnx` | Replaced by style head on shared CNN backbone |
| Generative bass | `assets/bass_model.onnx` | Bass stays rule-based (fixed root, section-aware) |
| Playing-style classifier | `assets/classifier.onnx` | Replaced by style head on shared CNN backbone |

Source files removed from build (kept on disk):
- `src/inference/OnnxStructureInference.*`
- `src/inference/OnnxBassInference.*`
- `src/inference/IStructureInference.h`
- `src/inference/RuleStructureInference.*`
- `src/inference/PlayingStyleClassifier.*`
- `src/analysis/BeatTracker.*`
- `src/analysis/StructureHoldSmoother.*`
- `src/analysis/PitchEstimator.*`
- `src/analysis/StablePitchTracker.*`
- `src/capture/FeatureCapture.*`
- `src/midi/BassMidiValidator.*`

---

## Training pipeline

| Step | Script | Output |
|------|--------|--------|
| 1. Record guitar | (human) | `data/raw/pattern_XX_name/*.wav` |
| 2. Extract mels | `training/scripts/build_mel_groove_dataset.py` | `data/processed/X_groove.npy`, `y_groove.npy` |
| 3. Train classifier | `training/train_groove_model.py` | `training/models/best_groove_model.pt` |
| 4. Export centroids + ONNX | `training/export_centroids.py` | `src/inference/pattern_embeddings.h`, `assets/metal_groove.onnx` |

Quality gates (train_groove_model.py):
- Test accuracy ≥ 60%
- Top-3 accuracy ≥ 80%
- Per-class recall ≥ 0.30

---

## References

- Code: `src/inference/MetalGrooveInference.cpp` (mel input, cosine similarity, fallback)
- Code: `src/inference/OnnxInference.cpp` (legacy scalar-feature path)
- Code: `src/AccompanimentProcessor.cpp` (factory, mel queue, drain loop)
- Centroid header: `src/inference/pattern_embeddings.h`
- Mel extractor: `src/analysis/MelSpectrogramExtractor.h`
- Architecture: `SIMPLIFY.md` §0 (full data-flow diagram)
- Training: `training/train_groove_model.py`, `training/export_centroids.py`
