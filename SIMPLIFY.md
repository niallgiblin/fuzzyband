# SIMPLIFY.md — Metal Accompaniment v0.8.0
## Unified Mel-CNN Pipeline

**Created:** 2026-06-28
**Audience:** Implementing agent (not a human reader — be precise)
**Target:** One ONNX model doing three things from mel spectrograms: groove selection, style classification, fill generation. Audio-driven ML that no hand-crafted rules can replicate.

---

## 0. Architecture Overview

```
Guitar Audio (mono, 44.1kHz)
    │
    ├─ AudioRingBuffer (accumulates 22050 samples = 512ms window)
    │      │
    │      ▼
    │  MelSpectrogramExtractor::process()  →  melOut[2048] = [64 bands × 32 frames]
    │      │                                  (EXISTING, WORKING, REUSE AS-IS)
    │      ▼
    │  MetalGrooveModel (single ONNX session)
    │      │
    │      ├─ CNN Backbone  (3 conv blocks → 128-dim bottleneck)
    │      │
    │      ├─ Groove Head   (128 → 64 L2-norm embedding)
    │      │     │
    │      │     └─ nearest-neighbor → pattern index (cosine sim vs 22 precomputed embeddings)
    │      │
    │      ├─ Style Head    (128 → 5 softmax)  [OPTIONAL, same inference call]
    │      │
    │      └─ Fill Head     (128 + 64 conditioning → LSTM → 16-step drum events)
    │                           [SEPARATE ONNX GRAPH, only called on transitions]
    │
    ▼
PatternPlayer → MIDI out (ch10 drums, ch2 bass)
```

**Single ONNX model file:** `assets/metal_groove.onnx` replaces `accompaniment_model.onnx`
**Two inference modes:** groove (every drain) and fill (transitions only)
**CNN backbone shared:** all three heads use the same convolutional features

---

## 1. What Gets Removed

### 1.1 Source files removed from build

| File | Why |
|------|-----|
| `src/inference/OnnxStructureInference.h/.cpp` | Broken (5-state vs 3-state index mismatch). Replaced by mel CNN style head. |
| `src/inference/OnnxBassInference.h/.cpp` | Dead code (proposals overwritten by RuleBasedBass). Bass stays rule-based. |
| `src/inference/IStructureInference.h` | No longer needed |
| `src/inference/RuleStructureInference.h/.cpp` | Shadow structure path removed |
| `src/analysis/StructureHoldSmoother.h/.cpp` | Only used by ONNX structure path |
| `src/inference/PlayingStyleClassifier.h/.cpp` | Replaced by style head on shared CNN backbone |
| `src/analysis/BeatTracker.h/.cpp` | Tempo from manual BPM knob or DAW transport only |
| `src/analysis/PitchEstimator.h/.cpp` | YIN unreliable on distorted guitar; bass uses fixed root |
| `src/analysis/StablePitchTracker.h/.cpp` | Depends on PitchEstimator |
| `src/capture/FeatureCapture.h/.cpp` | Evaluation tool, not needed in shipping binary |
| `src/midi/BassMidiValidator.h/.cpp` | Was for ONNX bass validation; no longer needed |

### 1.2 Assets unbundled from BinaryData

Remove from `juce_add_binarydata`:
- `assets/structure_model.onnx`
- `assets/bass_model.onnx`
- `assets/classifier.onnx`
- `assets/classifier.onnx.data`

Keep bundled:
- `assets/accompaniment_model.onnx` → replaced by `assets/metal_groove.onnx` (new model)
- `assets/forest.png` (unchanged)

### 1.3 APVTS parameters removed

- `structureBlend` — was for ML vs rules structure mixing
- `generativeBassMode` — simplify to bass always on when playing
- Keep: `outputGain`, `intensity`, `bpm`, `songForm`, `loop`

### 1.4 Processor fields removed

All member variables for BeatTracker, PitchEstimator, StablePitchTracker, OnnxStructureInference, OnnxBassInference, PlayingStyleClassifier, StructureHoldSmoother, FeatureCapture. Plus all related atomic display variables.

---

## 2. New Model: MetalGrooveModel

### 2.1 CNN Architecture

```python
class MelCNNBackbone(nn.Module):
    """Conv backbone: (1, 64, 32) → bottleneck (128,)."""
    def __init__(self):
        super().__init__()
        self.conv = nn.Sequential(
            # Block 1: (1, 64, 32) → (16, 32, 16)
            nn.Conv2d(1, 16, kernel_size=3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),

            # Block 2: (16, 32, 16) → (32, 16, 8)
            nn.Conv2d(16, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),

            # Block 3: (32, 16, 8) → (64, 4, 4)
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d((4, 4)),
        )
        self.flatten = nn.Flatten()
        self.bottleneck = nn.Linear(64 * 4 * 4, 128)

    def forward(self, x):
        return self.bottleneck(self.flatten(self.conv(x)))


class GrooveEmbeddingHead(nn.Module):
    """128 → 64 → L2-normalize. Cosine similarity to precomputed pattern embeddings."""
    def __init__(self):
        super().__init__()
        self.fc = nn.Linear(128, 64)

    def forward(self, bottleneck):
        x = self.fc(bottleneck)
        return nn.functional.normalize(x, p=2, dim=1)


class StyleClassifierHead(nn.Module):
    """128 → 5 softmax (palm_mute, open_chord, single_note, sustain, silence)."""
    def __init__(self, n_classes=5):
        super().__init__()
        self.fc = nn.Linear(128, n_classes)

    def forward(self, bottleneck):
        return self.fc(bottleneck)


class FillLSTMHead(nn.Module):
    """Conditioned step-by-step drum event generation.
    
    Input per step: [bottleneck(128) | target_embedding(64) | prev_event(4)]
    Prev event: one-hot [kick, snare, hat_ride, tom_crash, rest] + velocity + timing_offset
    Actually simplified to: [kit_piece_onehot(6) | velocity_norm(1)] = 7 dims
    Total conditioning: 128 + 64 + 7 = 199 dims
    
    Output per step: [kit_piece_logits(6), velocity(1)] = 7 dims
    """
    def __init__(self, hidden_size=128, n_kit_pieces=6):
        super().__init__()
        self.n_kit_pieces = n_kit_pieces
        self.lstm = nn.LSTM(
            input_size=128 + 64 + n_kit_pieces + 1,  # bottleneck + embedding + prev_onehot + prev_vel
            hidden_size=hidden_size,
            num_layers=2,
            batch_first=True,
        )
        self.kit_head = nn.Linear(hidden_size, n_kit_pieces)
        self.vel_head = nn.Linear(hidden_size, 1)

    def forward(self, bottleneck, target_embedding, prev_events, prev_velocities):
        """
        bottleneck: (B, 128)
        target_embedding: (B, 64)
        prev_events: (B, 16, n_kit_pieces) one-hot
        prev_velocities: (B, 16, 1) normalized [0,1]
        
        Returns: (kit_logits (B, 16, 6), velocities (B, 16, 1))
        """
        B = bottleneck.shape[0]
        # Tile conditioning across 16 steps
        cond = torch.cat([bottleneck.unsqueeze(1).expand(-1, 16, -1),
                          target_embedding.unsqueeze(1).expand(-1, 16, -1)],
                         dim=2)  # (B, 16, 128+64)
        lstm_input = torch.cat([cond, prev_events, prev_velocities], dim=2)  # (B, 16, 199)
        lstm_out, _ = self.lstm(lstm_input)  # (B, 16, 128)
        kit_logits = self.kit_head(lstm_out)  # (B, 16, 6)
        velocities = torch.sigmoid(self.vel_head(lstm_out)) * 127.0  # (B, 16, 1)
        return kit_logits, velocities
```

**Total parameters:** ~95K (backbone ~50K + groove head ~8K + style head ~650 + fill head ~35K)
**Inference time (ONNX):** <2ms for groove, <3ms for fill

### 2.2 ONNX Export — Two Graphs

The fill head uses LSTM which makes the graph autoregressive at runtime. For ONNX export, we create TWO separate .onnx files from the same checkpoint:

**Graph 1: `metal_groove.onnx`** (groove + style, runs every drain)
```
Input:  mel [1, 1, 64, 32]
Output: groove_embedding [1, 64]
        style_logits [1, 5]
```

**Graph 2: `metal_fill.onnx`** (fill generation, runs on transitions only)
```
Input:  mel [1, 1, 64, 32]
        target_embedding [1, 64]
        prev_events [1, 16, 6]
        prev_velocities [1, 16, 1]
Output: kit_logits [1, 16, 6]
        velocities [1, 16, 1]
```

For the fill graph, the LSTM unrolls 16 steps in the ONNX graph (not autoregressive at runtime — the full sequence is generated in one forward pass by conditioning on a learned "start" token for the prev_events input).

**Alternative (simpler):** Export the fill head as an autoregressive LSTM that the C++ code calls 16 times. Each call takes the previous step's output as input. This is actually cleaner for ONNX export.

Single fill graph with 1-step LSTM:
```
Input:  bottleneck [1, 128]
        target_embedding [1, 64]
        prev_kit_onehot [1, 6]
        prev_velocity [1, 1]
        lstm_hidden [1, 2, 128]   # (num_layers=2, hidden=128)
        lstm_cell [1, 2, 128]
Output: kit_logits [1, 6]
        velocity [1, 1]
        lstm_hidden_out [1, 2, 128]
        lstm_cell_out [1, 2, 128]
```

C++ runs this 16 times per fill, feeding hidden/cell state through. This is ~0.3ms total.

### 2.3 Pattern Embedding Precomputation

For each of the 22 patterns in MidiPatternLibrary, compute a 64-dim embedding from drum event features:

```python
def compute_pattern_embedding(pattern: MidiPattern) -> np.ndarray:
    """Compute a 64-dim embedding from a drum pattern's MIDI events.
    
    Features per pattern:
      - Kick onset density (kicks per beat)
      - Snare onset density
      - Hi-hat/ride density  
      - Avg velocity contour (16-step normalized)
      - Swing ratio (even vs dotted 8th ratio)
      - Fill complexity (tom/crash count)
      - Ghost note ratio
      ... plus statistical moments of above
    
    Project to 64-dim via a small learned projector network,
    or use hand-crafted features + PCA, or train the projector
    jointly with the groove head using triplet loss.
    """
```

Simplest approach: use the groove head's weights in reverse. Train the groove head to map CNN features → embedding, then freeze it and train a small "pattern encoder" that maps drum event features → the same embedding space. This is a known technique (student-teacher or cross-modal embedding).

**Even simpler approach (Phase A only):** Skip the learned pattern embeddings. Instead, train the CNN with a classification head (128 → 22 classes), then at export time, take the 128-dim bottleneck before the classification layer and use that as the embedding. Precompute each pattern's "ideal" embedding by feeding synthetic audio that matches each pattern's style through the CNN.

**Simplest approach that works:** The CNN backbone outputs a 128-dim bottleneck. Train it as a 22-way classifier on labeled guitar audio. At runtime, the 128-dim vector is the embedding. For nearest-neighbor lookup, compute the 128-dim centroid for each pattern class from the training set, store those 22 centroids as a static array in C++. This requires zero additional training infrastructure.

```python
# After training, compute centroids:
centroids = torch.zeros(22, 128)
counts = torch.zeros(22)
for x, y in train_loader:
    with torch.no_grad():
        bottleneck = model.backbone(x)
        for i, label in enumerate(y):
            centroids[label] += bottleneck[i]
            counts[label] += 1
centroids /= counts.unsqueeze(1)

# Export as C++ header:
print("static constexpr float kPatternEmbeddings[22][128] = {")
for i in range(22):
    vals = ", ".join(f"{v:.6f}f" for v in centroids[i].tolist())
    print(f"    {{{vals}}},")
print("};")
```

### 2.4 Training Data

**Required:** Labeled guitar audio recordings.

**Directory structure:**
```
data/raw/
  pattern_00_silent/      # Guitar silence or very quiet (~10 files × 5s)
  pattern_01_verse/       # Clean arpeggio playing (~20 files × 10s)
  pattern_02_half_time/   # Slow, sparse playing (~15 files × 10s)
  ...
  pattern_21_chorus_blast/ # Aggressive palm-muted chugging (~15 files × 10s)
```

**Total recording needed:** ~30-60 minutes of labeled guitar playing across all 22 pattern classes. Each file is a short WAV clip in the style that matches one pattern. The operator (you) records these once.

**Training script:** `training/train_groove_model.py` — reuses `build_mel_dataset.py` for mel extraction (already C++-aligned), trains a 22-way classifier with the CNN backbone, exports `metal_groove.onnx`.

**Training hyperparameters:**
- Optimizer: Adam, lr=1e-3
- Scheduler: CosineAnnealingLR, 80 epochs
- Batch size: 32
- Augmentation: Gaussian noise (±1dB), random gain (±12dB), time masking, frequency masking (same as existing train_classifier.py)
- Loss: CrossEntropyLoss with class weights
- Split: 70/15/15 stratified

**Quality gates:**
- Test accuracy ≥ 60% (22 classes is hard — the embedding approach compensates)
- Top-3 accuracy ≥ 80% (nearest neighbors in embedding space will pick a musically reasonable pattern even if exact class is wrong)
- Per-class recall ≥ 0.30 (no class completely ignored)

### 2.5 Training Script

New file: `training/train_groove_model.py`

```python
#!/usr/bin/env python3
"""Train MetalGrooveModel: MelCNN backbone + 22-way classifier.

Phase A: classification training
Phase B: bottleneck extraction + centroid computation
Phase C: ONNX export (groove head only)
"""

# Reuses:
#  - build_mel_dataset.py for C++-aligned mel extraction
#  - PlayingStyleCNN architecture as starting point (expand to 22 classes)

# Key differences from existing train_classifier.py:
#  1. 22 output classes instead of 5
#  2. After training, freeze backbone, compute per-class centroids
#  3. Export ONNX with backbone + embedding head (not classifier head)
#  4. Also export centroids as C++ header or JSON for embedding into plugin

# The embedding head for export:
#   bottleneck(128) → Linear(128, 64) → L2-normalize → output(64)
# Train this head separately using triplet loss after classification pretraining.
```

---

## 3. C++ Changes

### 3.1 New Files

**`src/inference/MetalGrooveInference.h`** — replaces `OnnxInference.h`
```cpp
#pragma once
#include "IInference.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

/**
 * @brief ONNX-based groove selector using mel spectrogram CNN backbone.
 *
 * Loads metal_groove.onnx. Runs groove embedding inference every drain (~50Hz).
 * Uses precomputed pattern embeddings (cosine similarity nearest-neighbor)
 * for pattern selection instead of classification logits.
 */
class MetalGrooveInference final : public IInference {
public:
    MetalGrooveInference();
    ~MetalGrooveInference() override;

    bool tryLoadModel();

    void prepare(double sampleRate) override;
    int selectPattern(const FeatureVector& features, int excludeIndex = -1) override;
    std::string getName() const override;

    /** @brief Mel-spectrogram-driven inference — the real method.
     *  @param melData 2048 floats [64 bands × 32 frames] row-major mel-dB
     *  @param excludeIndex pattern index to exclude (-1 = none)
     *  @return pattern index [0, 21] */
    int selectPatternFromMel(const float* melData, int excludeIndex = -1);

    /** @brief Generate a fill from mel context + target pattern.
     *  @param melData current mel spectrogram [2048]
     *  @param targetPatternIndex the pattern we're transitioning TO
     *  @param fillKind transition direction (Entry/BuildUp/Release/Breakdown)
     *  @param drumOut 16×(kit_piece, velocity) output
     *  @return true if fill was generated */
    bool generateFill(const float* melData, int targetPatternIndex,
                      int fillKind, float* drumOut);

    /** @brief Style classification from mel data (free, from same inference).
     *  @return style index 0-4 (palm_mute, open_chord, single_note, sustain, silence) */
    int classifyStyle(const float* melData);

    uint64_t getLoadErrorCount() const noexcept;
    uint64_t getRunErrorCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<uint64_t> loadErrorCount{0};
    std::atomic<uint64_t> runErrorCount{0};
};
```

**`src/inference/MetalGrooveInference.cpp`** — Implementation using ORT C++ API

Key implementation details:
- `tryLoadModel()`: Loads `metal_groove.onnx` from BinaryData, validates input shape [1, 1, 64, 32] and output shapes (groove_embedding [1, 64], style_logits [1, 5])
- `selectPatternFromMel()`: Runs ONNX session, gets 64-dim embedding, computes cosine similarity against 22 precomputed pattern centroids, returns best match
- Pattern centroids stored as `static constexpr std::array<std::array<float, 64>, 22> kPatternEmbeddings`
- `generateFill()`: Loads `metal_fill.onnx` (separate session), runs 16-step autoregressive LSTM
- `classifyStyle()`: Free — style logits come from same inference as groove embedding
- All catch blocks route to `PatternRules::rulePatternForState()` for graceful degradation

**`src/inference/pattern_embeddings.h`** — Auto-generated header with precomputed centroids
```cpp
#pragma once
#include <array>

namespace PatternEmbeddings {
    static constexpr int kEmbeddingDim = 64;
    static constexpr int kNumPatterns = 22;
    
    // Precomputed CNN bottleneck centroids per pattern class.
    // Generated by training/train_groove_model.py --export-centroids
    static constexpr std::array<std::array<float, kEmbeddingDim>, kNumPatterns> kCentroids = {{
        // 0: Silent
        {{0.01f, 0.02f, /* ... 62 more ... */}},
        // 1: Verse Groove  
        {{0.15f, -0.08f, /* ... */}},
        // ... 20 more patterns ...
    }};
}
```

### 3.2 Modified Files

**`src/inference/IInference.h`** — unchanged (interface stays the same)

**`src/inference/OnnxInference.h/.cpp`** — KEPT as fallback. The old scalar-feature ONNX model still works for the 3-state case (after fixing §6.1). The `makeInference()` factory tries MetalGrooveInference first, falls back to OnnxInference, then RuleBasedInference.

**`src/inference/pattern_rules.h`** — simplify for 3-state, remove AMBIENT/BREAKDOWN from rulePatternForState, isPatternCompatibleWithState, diversifyPattern. Keep sectionPatternPool. Keep transition fill logic. Keep applyExclusion.

**`src/analysis/StructureTagger.h/.cpp`** — Revert to 3-state SILENT/SOFT/LOUD. Keep sub-bass ratio discrimination (it's the best feature in the system). Remove AMBIENT and BREAKDOWN from enum, computeDesiredState, holdRequiredForTransition.

**`src/AccompanimentProcessor.h`** — remove fields for all deleted modules. Replace `std::unique_ptr<IInference> inference` with new factory that prefers MetalGrooveInference. Add `AudioRingBuffer` and `MelSpectrogramExtractor` (already exist, were used by PlayingStyleClassifier — keep them). Remove feature capture, beat tracker, pitch tracker, stable pitch tracker, ONNX structure, ONNX bass, style classifier fields.

**`src/AccompanimentProcessor.cpp`** — major rewrite of processBlock and drainFeatureQueueAndRunInference (see §3.3 and §3.4).

**`CMakeLists.txt`** — update source file lists, BinaryData, test targets (see §4).

### 3.3 Simplified processBlock() — Audio Thread

```cpp
void AccompanimentProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;
    float* in = buffer.getWritePointer(0);
    if (in == nullptr) return;

    // Scrub non-finite samples
    for (int i = 0; i < numSamples; ++i)
        if (!std::isfinite(in[i])) in[i] = 0.0f;
    juce::FloatVectorOperations::clip(in, in, -2.0f, 2.0f, numSamples);

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;

    // ── 1. Energy analysis (for StructureTagger + FeatureVector) ────────
    energyAnalyser.process(in, numSamples);
    const float rms = energyAnalyser.getRmsEnergy();
    const float centroid = energyAnalyser.getSpectralCentroid();
    const float hfFlux = energyAnalyser.getHighFreqFlux();
    
    structureTagger.setSubBassRatio(energyAnalyser.getSubBassRatio());
    const StructureState st = structureTagger.update(rms, centroid, hfFlux, numSamples, energyAnalyser.getPeakRms());

    // ── 2. Audio ring buffer → mel spectrogram (for ONNX) ──────────────
    audioRingBuffer.write(in, numSamples);
    
    // ── 3. Gate logic ───────────────────────────────────────────────────
    const double sr = cachedSampleRate.load(std::memory_order_relaxed);
    const bool digitalSilence = (rms < 1.0e-6f);
    
    const GateDecision gd = playbackGate.update(st, 0.0f, false, false, 0.0f, numSamples, sr);
    if (gd.resetTrackers) {
        playbackGate.reset();
        resetDrumHoldRequested.store(true, std::memory_order_release);
    }
    if (gd.snapBeatNow) {
        patternPlayer.snapBpm(bpmForPlayer);
        patternPlayer.snapToBarStart();
    }

    // ── 4. BPM ──────────────────────────────────────────────────────────
    float bpmForPlayer = 120.0f;
    if (auto* rawBpm = apvts.getRawParameterValue("bpm"))
        bpmForPlayer = rawBpm->load();
    // Also try DAW transport
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto t = pos->getBpm())
                bpmForPlayer = static_cast<float>(*t);
        }
    }

    // ── 5. Enqueue FeatureVector for inference thread ───────────────────
    FeatureVector fv;
    fv.bpm = bpmForPlayer;
    fv.rmsEnergy = rms;
    fv.spectralCentroid = centroid;
    fv.highFreqFlux = hfFlux;
    fv.state = st;
    fv.sampleTimestamp = hostSampleTime;
    fv.pitchRootMidi = 40.0f;  // fixed E2 — bass root handled separately
    fv.pitchConfidence = 0.0f;
    fv.rmsDelta = (rms - prevBlockRms) / std::max(prevBlockRms, 1.0e-6f);
    fv.subBassRatio = energyAnalyser.getSubBassRatio();
    prevBlockRms = rms;
    if (auto* rawIntensity = apvts.getRawParameterValue("intensity"))
        fv.policyIntensity = rawIntensity->load();
    
    // Also push mel data when ready (separate queue or same queue with flag)
    if (audioRingBuffer.isWindowReady()) {
        std::vector<float> melWindow(MelSpectrogramExtractor::kOutputSize);
        audioRingBuffer.readWindow(...); // read full window
        melExtractor.process(window.data(), melWindow.data());
        // Enqueue mel data for inference thread
        melQueue.try_enqueue(melWindow); // or pack into FeatureVector
    }
    
    featureQueue.try_enqueue(fv);

    // ── 6. Read latest pattern + apply song form if playActive ──────────
    const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
    patternPlayer.setBpm(bpmForPlayer);
    
    int effectivePatternIdx = patternIdx;
    const bool playOn = playActive.load(std::memory_order_acquire);
    
    if (playOn) {
        structureSequencer.advance(numSamples, bpmForPlayer, sr);
        if (structureSequencer.isComplete()) {
            playActive.store(false, std::memory_order_release);
        } else {
            const auto* secName = structureSequencer.getCurrentSectionName();
            auto pool = PatternRules::sectionPatternPool(secName);
            if (pool.count > 0) {
                const int bar = structureSequencer.getGlobalBarCount();
                const bool isLast = structureSequencer.isLastBar();
                effectivePatternIdx = isLast
                    ? PatternRules::selectFillPattern(0)
                    : pool.indices[bar % pool.count];
            }
        }
    }
    
    patternPlayer.setPatternIndex(effectivePatternIdx);

    // ── 7. Silence gating ───────────────────────────────────────────────
    const bool audioActive = (rms > 0.001f);
    const bool trulySilent = (!playOn && !audioActive) || digitalSilence;
    patternPlayer.setStructureSilent(trulySilent);

    // ── 8. Dequeue groove commit + bass ─────────────────────────────────
    PatternPlayer::GrooveCommit commit{};
    bool gotCommit = false;
    while (grooveCommitQueue.try_dequeue(commit)) gotCommit = true;
    
    if (playOn && !gotCommit) {
        commit.patternIndex = effectivePatternIdx;
        gotCommit = true;
    }
    
    // ── 9. Bass generation (fixed root, section-aware) ──────────────────
    const float bassRoot = 36.0f;  // C2 — could be APVTS parameter
    const char* bassSection = playOn ? structureSequencer.getCurrentSectionName() : "VERSE";
    PatternPlayer::GrooveCommit bassCommit = RuleBasedBass::generate(
        bassRoot, bassSection, playOn && structureSequencer.isFirstBar(), bpmForPlayer);
    
    // Merge: keep drum pattern from inference, bass from RuleBasedBass
    if (gotCommit) {
        bassCommit.patternIndex = commit.patternIndex;
        bassCommit.fillKind = commit.fillKind;
    } else {
        bassCommit.patternIndex = effectivePatternIdx;
    }
    commit = bassCommit;
    gotCommit = true;
    
    if (gotCommit) {
        patternPlayer.setGenerativeBassActive(true, static_cast<int>(bassRoot), 1.0f);
        patternPlayer.queueGrooveCommit(commit);
    }

    // ── 10. Process MIDI ─────────────────────────────────────────────────
    int64_t hostPos = hostSampleTime;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (auto t = pos->getTimeInSamples())
                hostPos = *t;
        }
    }
    patternPlayer.process(midi, numSamples, hostPos);

    // ── 11. Gain passthrough ─────────────────────────────────────────────
    float gain = 1.0f;
    if (auto* raw = apvts.getRawParameterValue("outputGain"))
        gain = raw->load();
    for (int i = 0; i < numSamples; ++i) {
        const float s = in[i] * gain;
        outL[i] = s;
        if (outR != outL) outR[i] = s;
    }

    hostSampleTime += numSamples;
    
    // Display updates
    displayBpm.store(bpmForPlayer, std::memory_order_relaxed);
    displayRms.store(rms, std::memory_order_relaxed);
    displayStateIndex.store(static_cast<int>(st), std::memory_order_relaxed);
    displayPatternIndex.store(effectivePatternIdx, std::memory_order_relaxed);
}
```

### 3.4 Simplified drainFeatureQueueAndRunInference() — Background Thread

```cpp
void AccompanimentProcessor::drainFeatureQueueAndRunInference() {
    // ── 1. Drain feature queue ───────────────────────────────────────────
    FeatureVector latest{};
    bool gotFeatures = false;
    while (true) {
        FeatureVector tmp{};
        if (!featureQueue.try_dequeue(tmp)) break;
        latest = tmp;
        gotFeatures = true;
    }
    
    // ── 2. Drain mel queue ──────────────────────────────────────────────
    std::vector<float> latestMel(MelSpectrogramExtractor::kOutputSize);
    bool gotMel = false;
    while (true) {
        std::vector<float> tmp(MelSpectrogramExtractor::kOutputSize);
        if (!melQueue.try_dequeue(tmp)) break;
        latestMel = std::move(tmp);
        gotMel = true;
    }
    
    if (!gotFeatures) return;
    
    // ── 3. Run ONNX groove inference (mel-driven) ────────────────────────
    int idx = 0;
    if (auto* groove = dynamic_cast<MetalGrooveInference*>(inference.get())) {
        if (gotMel)
            idx = groove->selectPatternFromMel(latestMel.data());
        else
            idx = groove->selectPattern(latest);  // fallback to scalar features
    } else {
        idx = inference->selectPattern(latest);
    }
    
    // ── 4. Diversify ─────────────────────────────────────────────────────
    const double sr = cachedSampleRate.load(std::memory_order_acquire);
    const int64_t samplesPerBar = static_cast<int64_t>(4.0 * 60.0 / static_cast<double>(latest.bpm) * sr);
    const int barMod8 = (samplesPerBar > 0) ? static_cast<int>((latest.sampleTimestamp / samplesPerBar) % 8) : 0;
    const int diversifiedIdx = PatternRules::diversifyPattern(idx, latest, barMod8);
    
    // ── 5. Apply exclusion ───────────────────────────────────────────────
    const int currentPat = latestPatternIndex.load(std::memory_order_acquire);
    const int rejectionCount = patternRejectionCount.load(std::memory_order_acquire);
    int excludeParam = (rejectionCount > 0) ? currentPat : -1;
    if (rejectionCount > 0)
        patternRejectionCount.store(rejectionCount - 1, std::memory_order_release);
    
    const int fallback = PatternRules::rulePatternForState(latest);
    const int result = PatternRules::applyExclusion(diversifiedIdx, excludeParam, latest.state, fallback);
    
    // ── 6. Commit if hold expired ────────────────────────────────────────
    const float bpmNow = latest.bpm > 0.0f ? latest.bpm : 120.0f;
    const int64_t twoBarsInSamples = static_cast<int64_t>(8.0 * 60.0 / static_cast<double>(bpmNow) * sr);
    const bool drumHoldExpired = (lastDrumPatternChangeSample < 0) ||
        (latest.sampleTimestamp - lastDrumPatternChangeSample >= twoBarsInSamples);
    
    if (drumHoldExpired || excludeParam >= 0) {
        PatternPlayer::GrooveCommit commit{};
        commit.patternIndex = result;
        commit.fillKind = chooseTransitionFillKind(
            lastCommittedStructureState, latest.state, result);
        commit.hasBassFrame = false;  // bass handled by audio thread RuleBasedBass
        
        if (grooveCommitQueue.try_enqueue(commit)) {
            latestPatternIndex.store(result, std::memory_order_release);
            lastDrumPatternChangeSample = latest.sampleTimestamp;
            lastCommittedStructureState = latest.state;
        }
    }
    
    displayPatternIndex.store(result, std::memory_order_relaxed);
}
```

### 3.5 Factory Function

```cpp
namespace {

std::unique_ptr<IInference> makeInference() {
#if defined(MA_ENABLE_ONNX)
    // Try new mel CNN model first
    auto groove = std::make_unique<MetalGrooveInference>();
    if (groove->tryLoadModel())
        return groove;
    
    // Fall back to old scalar ONNX model
    auto onnx = std::make_unique<OnnxInference>();
    if (onnx->tryLoadModel())
        return onnx;
#endif
    // Rule-based fallback
    return std::make_unique<RuleBasedInference>();
}

} // namespace
```

---

## 4. Build System Changes

### 4.1 CMakeLists.txt modifications

**BinaryData:** Replace old model list with single new model
```cmake
if(MA_ENABLE_ONNX)
    list(APPEND BINARY_DATA_SOURCES
        "${CMAKE_SOURCE_DIR}/assets/metal_groove.onnx"
    )
endif()
```

**Plugin sources — REMOVE:**
```
src/inference/OnnxStructureInference.cpp
src/inference/RuleStructureInference.cpp
src/inference/OnnxBassInference.cpp
src/inference/PlayingStyleClassifier.cpp
src/analysis/BeatTracker.cpp
src/analysis/StructureHoldSmoother.cpp
src/analysis/PitchEstimator.cpp
src/analysis/StablePitchTracker.cpp
src/capture/FeatureCapture.cpp
src/midi/BassMidiValidator.cpp
```

**Plugin sources — ADD:**
```
src/inference/MetalGrooveInference.cpp
```

**Plugin sources — KEEP:**
```
src/AccompanimentProcessor.cpp
src/AccompanimentEditor.cpp
src/analysis/EnergyAnalyser.cpp
src/analysis/StructureTagger.cpp
src/analysis/StructureSequencer.cpp
src/analysis/AudioRingBuffer.cpp
src/analysis/MelSpectrogramExtractor.cpp
src/analysis/PlaybackGate.cpp
src/inference/RuleBasedInference.cpp
src/inference/OnnxInference.cpp
src/midi/MidiPatternLibrary.cpp
src/midi/PatternPlayer.cpp
src/midi/RuleBasedBass.cpp
```

**Test sources — REMOVE:**
```
tests/test_beat_tracker.cpp
tests/test_pitch_estimator.cpp
tests/test_stable_pitch_tracker.cpp
tests/test_structure_hold_smoother.cpp
tests/test_structure_hold_smoother_complete.cpp
tests/test_bass_generative.cpp
tests/test_bass_validator_boundaries.cpp
tests/test_feature_capture.cpp
src/capture/FeatureCapture.cpp (from test target)
src/analysis/BeatTracker.cpp (from test target)
src/analysis/PitchEstimator.cpp (from test target)
src/analysis/StablePitchTracker.cpp (from test target)
src/analysis/StructureHoldSmoother.cpp (from test target)
src/inference/RuleStructureInference.cpp (from test target)
src/midi/BassMidiValidator.cpp (from test target)
```

**Test sources — ADD:**
```
tests/test_metal_groove_inference.cpp
```

### 4.2 Version bump

Line 4: `project(MetalAccompaniment VERSION 0.8.0)`

---

## 5. Test Plan

### 5.1 Tests to update (existing, need assertion changes)

| Test file | Changes |
|-----------|---------|
| `test_structure_tagger.cpp` | Remove AMBIENT/BREAKDOWN test cases. Update hold time assertions to match simplified 3-state constants. |
| `test_structure_tagger_extended.cpp` | Remove all 5-state tests (AMBIENT→*, BREAKDOWN→*). Update SOFT→LOUD, LOUD→SOFT hold time assertions. |
| `test_pattern_rules.cpp` | Remove AMBIENT/BREAKDOWN from isPatternCompatibleWithState tests. Update rulePatternForState expected values. Remove stylePatternPool tests. Remove diversifyPattern AMBIENT tests. |
| `test_onnx_inference.cpp` | Add test for MetalGrooveInference. Keep old OnnxInference test. |
| `test_pattern_player.cpp` | Update bass semitone offset test (simplified bass path). Update generative bass revert test. |
| `test_midi_pattern_library.cpp` | Unchanged (22 patterns, already correct). |
| `test_energy_analyser.cpp` | Unchanged. |
| `test_pattern_player_generative_bass.cpp` | Simplify for fixed-root bass. |
| `test_playback_gate.cpp` | Unchanged. |
| `test_rule_based_inference.cpp` | Update expected pattern indices for simplified 3-state rules. |

### 5.2 Tests to delete

| Test file | Why |
|-----------|-----|
| `test_beat_tracker.cpp` | Module removed |
| `test_pitch_estimator.cpp` | Module removed |
| `test_stable_pitch_tracker.cpp` | Module removed |
| `test_structure_hold_smoother.cpp` | Module removed |
| `test_structure_hold_smoother_complete.cpp` | Module removed |
| `test_bass_generative.cpp` | Was ONNX bass validation |
| `test_bass_validator_boundaries.cpp` | Module removed |
| `test_feature_capture.cpp` | Module removed |
| `test_structure_rule_agreement.cpp` | Was ONNX vs rule agreement; concept superseded |
| `test_structure_shadow_integration.cpp` | Shadow structure removed |

### 5.3 Integration/E2E tests — update

| Test | Changes |
|------|---------|
| `test_processor_pipeline.cpp` | Remove BeatTracker/PitchTracker setup. Simplify to new pipeline. |
| `test_e2e_bpm_tracking.cpp` | Rewrite: BPM now comes from knob, test that it stays stable. |
| `test_e2e_silent_section.cpp` | Update for simplified pipeline. |
| `test_e2e_structure_transitions.cpp` | Update for 3-state transitions. |
| `test_e2e_groove_variety.cpp` | Unchanged (diversifyPattern still produces variety). |
| `test_long_duration_stability.cpp` | Simplify: no beat tracker to drift. |
| `test_performance_benchmark.cpp` | Update: measure ONNX inference time for new model. |

### 5.4 New tests

| Test | Purpose |
|------|---------|
| `test_metal_groove_inference.cpp` | Test MetalGrooveInference loads and selects patterns. Test cosine similarity nearest-neighbor. Test fallback path. Test edge cases (invalid mel data, all-zeros input). |
| `test_sub_bass_energy.cpp` | Already exists — keep. |

---

## 6. Bug Fixes

### 6.1 OnnxInference 5-state → one-hot encoding (FIXED BY REVERTING TO 3-STATE)

After simplifying StructureTagger to 3-state, the existing OnnxInference::selectPattern() one-hot switch is correct again. Verify:

```cpp
switch (f.state) {
    case StructureState::SILENT: stateSILENT = 1.0f; break;
    case StructureState::SOFT:   stateSOFT   = 1.0f; break;
    case StructureState::LOUD:   stateLOUD   = 1.0f; break;
}
```

### 6.2 OnnxInference output clamping (EXISTING BUG, FIX)

Current code clamps ONNX output to [0, 6]:
```cpp
const int clamped = static_cast<int>(std::clamp(raw, int64_t(0), int64_t(6)));
```
But the library now has 22 patterns. Change to:
```cpp
const int clamped = static_cast<int>(std::clamp(raw, int64_t(0), int64_t(21)));
```

### 6.3 Test assertion mismatch: getPattern(999) clamps to 10

Test name says "clamps to 10" but now clamps to 21 (patternCount=22). The assertion is already correct (`p.name == "Chorus Blast"` = index 21) — just rename the test.

---

## 7. Implementation Order (Waves)

### Wave 0: Training Data Collection (HUMAN)

Before any code changes, record the labeled guitar audio:
1. Create `data/raw/pattern_00_silent/` through `data/raw/pattern_21_chorus_blast/`
2. Record 30-60 minutes of playing across all 22 pattern styles
3. Each file: 5-15 seconds, mono WAV, 44.1kHz

### Wave 1: Training Pipeline (Python)

1. Create `training/train_groove_model.py`:
   - Reuse `build_mel_dataset.py` for C++-aligned mel extraction
   - Train MelCNN backbone + 22-way classifier
   - Compute per-class bottleneck centroids
   - Export `metal_groove.onnx` (backbone + embedding head + style head)
   - Export centroid JSON for C++ header generation
   
2. Create `training/export_centroids.py`:
   - Read centroids JSON
   - Generate `src/inference/pattern_embeddings.h`

3. Run training pipeline end-to-end:
   ```
   python build_mel_dataset.py --raw-dir data/raw --out-dir data/processed
   python train_groove_model.py --data-dir data/processed
   python export_centroids.py
   ```
   Output: `assets/metal_groove.onnx`, `src/inference/pattern_embeddings.h`

### Wave 2: C++ Simplification (no new ONNX)

4. **StructureTagger:** Revert to 3-state. Update .h and .cpp. Update tests.
5. **PatternRules:** Remove AMBIENT/BREAKDOWN. Update tests.
6. **Remove dead modules:** Delete source files from build. Remove BinaryData entries for old models.
7. **AccompanimentProcessor:** Remove fields, update prepareToPlay, processBlock, drainFeatureQueue.
8. **Bass:** Hard-code C2 root. Remove pitch tracking. Simplify RuleBasedBass if needed.
9. **Editor:** Remove structureBlend slider, ONNX error count, feature capture toggle.
10. **Build and fix compilation.** Run tests — all should pass (except new model test which needs the .onnx).

### Wave 3: New ONNX Model Integration

11. **Create `MetalGrooveInference.h/.cpp`** — ORT loading, groove inference, cosine similarity lookup.
12. **Add `MetalGrooveInference` to CMake** — source files, ONNX link.
13. **Update `makeInference()` factory** — prefer MetalGrooveInference, fall back to OnnxInference.
14. **Integrate mel queue** into processor (melQueue alongside featureQueue).
15. **Build** with `MA_ENABLE_ONNX=ON`. Place `metal_groove.onnx` in assets/.
16. **Run all tests.** Debug ONNX loading issues.
17. **Smoke test** in Reaper with clean DI guitar.

### Wave 4: Polish

18. Update `test_metal_groove_inference.cpp` with real tests.
19. CI: update GitHub Actions to test with ONNX.
20. Docs: update README, ONNX_IO.md, PROJECT.md.

---

## 8. File Manifest

### Files to CREATE:
```
training/train_groove_model.py
training/export_centroids.py
training/models/groove_model.py
src/inference/MetalGrooveInference.h
src/inference/MetalGrooveInference.cpp
src/inference/pattern_embeddings.h         (auto-generated)
tests/test_metal_groove_inference.cpp
assets/metal_groove.onnx                    (output of training)
```

### Files to MODIFY:
```
CMakeLists.txt
src/AccompanimentProcessor.h
src/AccompanimentProcessor.cpp
src/AccompanimentEditor.h
src/AccompanimentEditor.cpp
src/inference/IInference.h                 (minor — may need new virtual for mel path)
src/inference/OnnxInference.cpp            (fix output clamping to [0,21])
src/inference/pattern_rules.h
src/analysis/StructureTagger.h
src/analysis/StructureTagger.cpp
src/analysis/FeatureVector.h               (unchanged — still 7 fields)
src/midi/RuleBasedBass.h                  (simplify signature)
src/midi/RuleBasedBass.cpp                (remove pitch dependency)
```

### Files to DELETE (from build, keep on disk):
```
src/inference/OnnxStructureInference.h
src/inference/OnnxStructureInference.cpp
src/inference/OnnxBassInference.h
src/inference/OnnxBassInference.cpp
src/inference/IStructureInference.h
src/inference/RuleStructureInference.h
src/inference/RuleStructureInference.cpp
src/inference/PlayingStyleClassifier.h
src/inference/PlayingStyleClassifier.cpp
src/analysis/BeatTracker.h
src/analysis/BeatTracker.cpp
src/analysis/StructureHoldSmoother.h
src/analysis/StructureHoldSmoother.cpp
src/analysis/PitchEstimator.h
src/analysis/PitchEstimator.cpp
src/analysis/StablePitchTracker.h
src/analysis/StablePitchTracker.cpp
src/capture/FeatureCapture.h
src/capture/FeatureCapture.cpp
src/midi/BassMidiValidator.h
src/midi/BassMidiValidator.cpp
tests/test_beat_tracker.cpp
tests/test_pitch_estimator.cpp
tests/test_stable_pitch_tracker.cpp
tests/test_structure_hold_smoother.cpp
tests/test_structure_hold_smoother_complete.cpp
tests/test_bass_generative.cpp
tests/test_bass_validator_boundaries.cpp
tests/test_feature_capture.cpp
tests/test_structure_rule_agreement.cpp
tests/test_structure_shadow_integration.cpp
```

### Files to KEEP AS-IS:
```
src/analysis/EnergyAnalyser.h/.cpp       (sub-bass ratio already implemented)
src/analysis/AudioRingBuffer.h/.cpp      (feeds mel extractor)
src/analysis/MelSpectrogramExtractor.h/.cpp (C++-aligned, working)
src/analysis/PlaybackGate.h/.cpp         (gate logic, unchanged)
src/analysis/StructureSequencer.h/.cpp   (song forms, working)
src/midi/MidiPatternLibrary.h/.cpp       (22 patterns, working)
src/midi/PatternPlayer.h/.cpp            (MIDI output, working)
src/inference/RuleBasedInference.h/.cpp  (fallback, working)
src/inference/IInference.h               (interface, stable)
```

---

## 9. Success Criteria

1. **Build:** `cmake --build` with `MA_ENABLE_ONNX=ON` succeeds with zero warnings
2. **Tests:** All tests pass (target: ~120 tests, 100% pass rate)
3. **Plugin loads:** VST3/AU loads in Reaper without errors
4. **Groove selection:** Playing different styles produces audibly different drum patterns
5. **Song form:** Play button runs through full song structure with section-appropriate patterns
6. **Bass:** Bass plays root notes in rhythm for all sections
7. **Stability:** 5-minute session with no crashes, xruns, or pattern flickering
8. **ML demo:** ONNX model loads from BinaryData, runs <5ms per inference call
9. **Fallback:** Disabling ONNX (build without MA_ENABLE_ONNX) still produces music via rules
