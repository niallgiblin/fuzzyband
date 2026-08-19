# Metal Accompaniment — v0.9.16

JUCE **8** **VST3 / AU** plugin: listens to guitar audio and outputs **drum + bass MIDI** in real time. Ships with a **Mel-CNN ONNX pipeline** (mel spectrogram → pattern selection via cosine similarity) and a **rule-based fallback** (energy/structure/tempo). Version **0.9.16** — **Musicality & Rock Pivot** (rock-first genre default, groove engine, harmonic bass; drop-C pitch tracking, bass octave control; groove-lock rework, golden-signal tests, editable song form). See [`CHANGELOG.md`](CHANGELOG.md) for the full history.

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
| `bpm` | Fallback tempo only — used when no DAW transport BPM is available (standalone) |
| `genre` | Genre preset (Rock default, Hard Rock, Punk, Metal, Sludge): groove feel, velocity profile, section dynamics |
| `swing` | Swing/shuffle ratio (0–100%) — delays off-8th drum events |
| `songForm` | Section preset (VERSE/CHORUS/BRIDGE/etc.) |
| `loop` | Loop song form |

**Tempo is DAW-transport-authoritative.** The drum/bass clock is anchored to the host
sample position (`getTimeInSamples()`), so patterns stay locked to the project grid across
seeks, loops, and transport start/stop. The `bpm` knob is only a fallback for hosts with
no transport (e.g. the standalone build); audio-derived tempo tracking is not used.

### Musicality (v0.9.0)

- **Bass engine:** the authored bass lines in `MidiPatternLibrary` now actually play,
  transposed to the guitarist's tracked root (harmony: root/fourth/fifth/octave per
  section, beat-1 accents, ±5 humanisation, ~2 ms behind the kick). Patterns without
  authored bass get a harmonic line from the root.
- **Groove engine:** drums render through a velocity hierarchy (downbeat > backbeat >
  8th hats > off-16ths) and structured microtiming (backbeat slightly late, kick
  slightly early) with a bounded gaussian instead of white-noise jitter.
- **Dynamic contrast:** per-section velocity multipliers — chorus backbeat is ≥15 louder
  than verse backbeat.
- **Ghost notes:** off-16th snare ghosts (velocity 30–55) in verse/breakdown sections.
- **Rock-first pattern set:** Rock Backbeat, Rock Half-Time, Rock Shuffle, Punk D-Beat,
  Rock Ballad, Rock 6/8 Feel (indices 22–27) — the metal set is retained as the
  "heavy" pool and via the Metal/Sludge genre presets.

### On-screen diagnostics

Live readouts: **BPM**, **State** (SILENT/SOFT/LOUD), **Pattern** (0–27), **RMS**, **Centroid**, **HF Flux**.

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
| **v0.9.7** | Bass octave control (−12/0/+12) for range-limited bass VSTs; confirmed MIDI 36 = C2 output for drop-C (VSTs using the middle-C=C3 convention display it as C1) |
| **v0.9.6** | Drop-C pitch tracking: pitch estimator low end extended 75→55 Hz so C2 (65.4 Hz) and lower drop tunings are detected — the bass follows your root instead of defaulting to E2 |
| **v0.9.5** | Solid SILENT under hot-input noise: silent-threshold cap raised (0.03→0.06) so the learned floor clears noise up to ~0.05 RMS; phrase learner gated on SILENT (no more noise-locked repeating bass); faster floor adaptation |
| **v0.9.4** | Adaptive noise floor: SILENT is solid under guitar hum (threshold learns your noise floor; holds to SILENT cut to 1s; noise-floor readout in UI) |
| **v0.9.3** | Bass stops during the Silent pattern (was: harmonic fallback droned the root under hum) |
| **v0.9.2** | Frozen-transport fix: jamming with the DAW transport stopped now runs the plugin's own beat clock (was: stuck on Silent + block-rate machine-gun) |
| **v0.9.1** | Bass stuck-note fix (multi-pitch bass note-off bookkeeping) |
| **v0.9.0** | Musicality & Rock Pivot: bass engine, groove engine, dynamic contrast, ghost notes, rock pattern set, genre presets |
| **v0.8.12** | Transport-anchored drum clock; DAW-transport-only tempo; dead-path cleanups |
| **v0.8.2** | Mel queue integration — MetalGrooveInference wired end-to-end |
| **v0.8.1** | Training pipeline + MetalGrooveInference ONNX integration |
| **v0.8.0** | Unified Mel-CNN Pipeline (22-pattern classifier) |
| v0.6.0 | ML Correctness & Evaluation |
| v0.5.0 | Rhythmic Coherence |
| v0.4.0 | ML Playability & Simplification |
| v0.3.0 | Real ML Training Pipeline |
| v0.2.0 | ML + Generative |
| v0.1.0 | Rule-based MVP |
