# Metal Accompaniment — v0.8.2

JUCE **8** **VST3 / AU** plugin: listens to guitar audio and outputs **drum + bass MIDI** in real time. Ships with a **Mel-CNN ONNX pipeline** (mel spectrogram → pattern selection via cosine similarity) and a **rule-based fallback** (energy/structure/tempo). Version **0.8.2** — **Unified Mel-CNN Pipeline**.

## Quick start

```bash
# Build with ONNX (default)
cmake -B build-onnx -DCMAKE_BUILD_TYPE=Release -DMA_ENABLE_ONNX=ON \
  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
cmake --build build-onnx --parallel

# Install VST3
cp -R "build-onnx/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3" \
  ~/Library/Audio/Plug-Ins/VST3/
```

**Insert order:** guitar → Metal Accompaniment → amp/cab sims → FX. Dry DI signal before any distortion.

## Architecture (v0.8.x)

```
Guitar Audio (mono, 44.1kHz)
    │
    ├─ AudioRingBuffer (accumulates 22,050 samples = 512ms)
    │      │
    │      ▼
    │  MelSpectrogramExtractor → mel[2048] = [64 bands × 32 frames]
    │      │
    │      ▼  (lock-free queue → background thread)
    │  MetalGrooveInference (ONNX)
    │      │
    │      ├─ CNN Backbone (3 conv blocks → 128-dim bottleneck)
    │      │
    │      └─ Cosine similarity vs 22 precomputed centroids → pattern index
    │
    ├─ EnergyAnalyser → RMS, centroid, HF flux, sub-bass ratio
    ├─ StructureTagger → SILENT / SOFT / LOUD (3-state)
    ├─ PlaybackGate → silence gating, phrase-breath holds
    │
    ▼
PatternPlayer → MIDI out (ch10 drums, ch2 bass)
```

## ML pipeline

| Model | File | Role |
|-------|------|------|
| **Metal Groove** | `assets/metal_groove.onnx` | Mel-CNN: 22-way pattern selection via bottleneck centroids + 5-way style classifier. Active by default. |
| Legacy pattern | `assets/accompaniment_model.onnx` | Scalar-feature ONNX fallback (7-float input → pattern index) |

**Training pipeline** (`training/`):
```bash
# 1. Record labeled guitar audio per pattern class → data/raw/pattern_XX_name/*.wav
# 2. Extract C++-aligned mel spectrograms
python training/scripts/build_mel_groove_dataset.py
# 3. Train 22-way classifier (quality gates: acc ≥60%, top-3 ≥80%)
python training/train_groove_model.py --device mps --epochs 80
# 4. Export centroids (C++ header) + ONNX model
python training/export_centroids.py
```

**Latest training results:** 95.6% test accuracy, 99.6% top-3, all per-class recall ≥0.86.

## Plugin parameters

| Parameter | Description |
|-----------|-------------|
| `outputGain` | Guitar pass-through level (does not scale MIDI) |
| `intensity` | Shifts effective BPM tier (±20 BPM range) |
| `bpm` | Manual BPM (or sync from DAW transport) |
| `songForm` | Section preset (VERSE/CHORUS/BRIDGE/etc.) |
| `loop` | Loop song form |

### On-screen diagnostics

Live readouts: **BPM**, **State** (SILENT/SOFT/LOUD), **Pattern** (0–21), **RMS**, **Centroid**, **HF Flux**.

## Build

Requirements: **CMake 3.22+**, C++20 compiler, **Git** (FetchContent), **ONNX Runtime** (Homebrew: `brew install onnxruntime`).

```bash
cmake -B build-onnx -DCMAKE_BUILD_TYPE=Release -DMA_ENABLE_ONNX=ON \
  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
cmake --build build-onnx --parallel
```

Artifacts:
- VST3: `build-onnx/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3`
- AU: `build-onnx/MetalAccompaniment_artefacts/Release/AU/Metal Accompaniment.component`

## Tests

```bash
# Unit + integration + E2E (101/103 pass in current build)
build-onnx/MetalAccompanimentTests
build-onnx/MetalAccompanimentIntegrationTests

# ONNX latency benchmark (p99 <5ms)
build-onnx/MetalAccompanimentTests "[onnx][latency]"
```

## Reaper setup

1. Install plugin, **re-scan VST3**
2. Insert **Metal Accompaniment** on guitar track (before amp sims)
3. Route MIDI from plugin track → drum instrument track (ch10 drums, ch2 bass)
4. Play — patterns switch reactively based on your playing style

Search "Metal" or "Niall" in the FX browser. Plugin appears under **Tools** category.

## Documentation

| Doc | Content |
|-----|---------|
| [`SIMPLIFY.md`](SIMPLIFY.md) | v0.8.0 architecture & implementation plan |
| [`docs/ONNX_IO.md`](docs/ONNX_IO.md) | ONNX tensor contracts (metal_groove + legacy) |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Component boundaries, threading, data flow |
| [`.planning/ROADMAP.md`](.planning/ROADMAP.md) | GSD milestone & phase tracking |

## Version history

| Version | Milestone |
|---------|-----------|
| **v0.8.2** | Mel queue integration — MetalGrooveInference wired end-to-end |
| **v0.8.1** | Training pipeline + MetalGrooveInference ONNX integration |
| **v0.8.0** | Unified Mel-CNN Pipeline (22-pattern classifier) |
| v0.6.0 | ML Correctness & Evaluation |
| v0.5.0 | Rhythmic Coherence |
| v0.4.0 | ML Playability & Simplification |
| v0.3.0 | Real ML Training Pipeline |
| v0.2.0 | ML + Generative |
| v0.1.0 | Rule-based MVP |
