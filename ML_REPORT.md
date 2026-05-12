# ML Report — Metal Accompaniment Plugin

**Version:** v0.5.0  
**Date:** 2026-05-11  
**Scope:** Full data lifecycle — audio input → feature extraction → inference → MIDI output — plus all training artifacts, model architectures, and current runtime state.

---

## 1. What the Plugin Is Currently Using

The plugin ships with **three bundled ONNX models** (`assets/`) but by default compiles with `MA_ENABLE_ONNX=OFF`. In a standard build, all inference is **rule-based**.

| Component | Default (MA_ENABLE_ONNX=OFF) | With MA_ENABLE_ONNX=ON |
|-----------|------------------------------|------------------------|
| Pattern selection | `RuleBasedInference` | `OnnxInference` (falls back to rule on load failure) |
| Structure classification | `StructureTagger` (rule FSM) | `OnnxStructureInference` shadow (advisory only) |
| Bass line generation | Static library patterns | `OnnxBassInference` piano-roll (if `generativeBassMode` ≠ Off) |

The ONNX models are present on disk and embedded via JUCE BinaryData when the ONNX build flag is set. All three models are small enough to bundle directly (accompaniment: 14.6 KB, bass: 7.6 KB, structure: 32.7 KB).

---

## 2. System Architecture: Threading Model

```
Audio Thread (real-time, ~5 ms deadline @ 48 kHz / 256 buf)
├── OnsetDetector          → BPM, onset flag
├── BeatTracker            → beat phase, confidence
├── EnergyAnalyser         → RMS, spectral centroid, HF flux
├── StructureTagger        → discrete state {SILENT, SOFT, LOUD}
├── PitchEstimator         → MIDI note (float), confidence
├── StablePitchTracker     → semitone offset for bass
├── PlaybackGate           → gate open/close, beat-snap, crash arm
├── TempoStabiliser        → smoothed BPM
└── PatternPlayer          → drum + bass MIDI → DAW

         ↓  lock-free queue (moodycamel::ReaderWriterQueue)
              FeatureVector (10 fields, one per audio block)
         ↓

Background Inference Thread (~50 Hz polling, 20 ms sleep)
├── IInference             → pattern index [0–6]
├── IStructureInference    → shadow structure metrics
└── OnnxBassInference      → bass piano-roll [1×32]
         ↓  atomic int / atomic arrays
         ↓
    PatternPlayer picks up result next audio block
```

**Key invariant:** No inference runs on the audio thread. No heap allocation on the audio thread. All cross-thread handoff is lock-free or atomic.

---

## 3. Feature Extraction Pipeline (Audio Thread)

### 3.1 OnsetDetector — `src/analysis/OnsetDetector.h/.cpp`

Estimates tempo from spectral flux onset detection.

**Algorithm:**
- FFT size: 2048 samples, hop: 512 samples
- Spectral flux: sum of positive spectrum deltas in [100 Hz, 4 kHz]
- Adaptive threshold: `rolling_mean * 1.5 + floor(0.05)`
- Refractory period: 80 ms between valid onsets
- IOI ring buffer: 16 inter-onset intervals → median filter → BPM
- Tempo locking: 8+ consecutive IOIs within ±8% → locked signal
- Octave disambiguation: folds 2× / 0.5× aliases toward current BPM

**Outputs:** `currentBpm` [80, 220], onset flag, tempo lock status  
**Key constants:**
```cpp
static constexpr float kAdaptiveK     = 1.5f;
static constexpr float kFluxFloor     = 0.05f;
static constexpr float kMinBpm        = 80.0f;
static constexpr float kMaxBpm        = 220.0f;
```

### 3.2 BeatTracker — `src/analysis/BeatTracker.h/.cpp`

Tracks beat phase and confidence via comb-filter periodogram on the onset-strength function (OSF).

**Algorithm:**
- Autocorrelation-like lag analysis over rolling OSF window
- BPM search range: [80, 220], peak picked from periodogram
- Confidence gate: high threshold 0.55, low threshold 0.35 (hysteresis)
- Beat phase: linear interpolation between beats

**Outputs:** beat phase [0, 1), beat confidence [0, 1], lock status

### 3.3 EnergyAnalyser — `src/analysis/EnergyAnalyser.h/.cpp`

Extracts three complementary audio descriptors.

- **RMS Energy:** 100 ms rolling window, normalized ~[0, 1]
- **Spectral Centroid (Hz):** Weighted mean frequency — distinguishes palm-mute from open chord
- **High-Frequency Flux:** Spectral flux in [2 kHz, Nyquist] — attack / presence indicator
- **Peak RMS Envelope:** Slow-decay peak for amplitude normalization

FFT: 1024-sample, 256-sample hop, Hann window.

### 3.4 StructureTagger — `src/analysis/StructureTagger.h/.cpp`

Threshold-based FSM that maps continuous energy features to three discrete structure states.

**States:** `SILENT`, `SOFT`, `LOUD`

**Transitions (hysteresis hold times):**

| From → To | Hold before allowed |
|-----------|-------------------|
| SILENT → any | 0 ms (immediate) |
| SOFT ↔ LOUD | 200 ms |
| SOFT → SILENT | 1500 ms |
| LOUD → SOFT | 1500 ms |
| LOUD → SILENT | 2000 ms |

**Thresholds:**
```cpp
static constexpr float kSilentRms       = 0.012f;
static constexpr float kSilentPeakRatio = 0.02f;
static constexpr float kLoudRms         = 0.075f;
static constexpr float kLoudPeakRatio   = 0.50f;
```

### 3.5 PitchEstimator — `src/analysis/PitchEstimator.h/.cpp`

Monophonic pitch via YIN algorithm.

**Algorithm:** CMNDF (cumulative mean normalized difference function), lag search over sample-rate-dependent [minLag, maxLag], parabolic interpolation for sub-sample accuracy.

**Ring buffer:** 4096 samples, preallocated (real-time safe).  
**Outputs:** MIDI note (fractional float), confidence [0, 1]

### 3.6 StablePitchTracker — `src/analysis/StablePitchTracker.h/.cpp`

Filters noisy pitch estimates for bass transposition.

**Logic:**
- Requires confidence > 0.35
- Holds pitch class for one beat duration (derived from BPM)
- Maps pitch class to semitone offset relative to E (MIDI 40 % 12)
- Returns `INT_MIN` if window not elapsed or confidence too low

### 3.7 PlaybackGate — `src/analysis/PlaybackGate.h/.cpp`

Hysteresis gate controlling when MIDI output fires after silence or structure transitions.

**Gate conditions:**
- Phrase breath hold: 2 s minimum silence before reopening
- Beat snap: one-shot beat-clock alignment when gate opens (2 s window)
- Active fallback: opens after 0.35 s of non-silent content even without tempo lock
- Crash cymbal arm: fires on transition events

**Output struct:**
```cpp
struct GateDecision {
    bool gateOpen;
    bool snapBeatNow;
    bool armCrash;
    bool resetTrackers;
};
```

### 3.8 TempoStabiliser — `src/analysis/TempoStabiliser.h/.cpp`

Prevents rapid BPM bouncing from independent OnsetDetector / BeatTracker estimates.

**Hysteresis:** ±4 BPM deadband before accepting a change; 2 s of agreement required before committing.

---

## 4. FeatureVector — The Data Packet

Produced on the audio thread once per block, enqueued to the lock-free queue for consumption on the background inference thread.

```cpp
// src/analysis/FeatureVector.h
struct FeatureVector {
    float bpm              = 120.0f;   // [80, 220] from OnsetDetector
    float rmsEnergy        = 0.0f;     // ~[0, 1] from EnergyAnalyser
    float spectralCentroid = 0.0f;     // Hz from EnergyAnalyser
    float highFreqFlux     = 0.0f;     // 2 kHz+ flux from EnergyAnalyser
    StructureState state   = SILENT;   // 3-class FSM from StructureTagger
    int64_t sampleTimestamp = 0;       // audio frame counter
    float pitchRootMidi    = 40.0f;    // PITCH-02: monophonic root MIDI note
    float pitchConfidence  = 0.0f;     // [0,1] YIN confidence
    float policyIntensity  = 0.5f;     // PUI-01: user intensity knob [0,1]
    float rmsDelta         = 0.0f;     // (rms - prevRms) / max(prevRms, 1e-6)
};
```

`sampleTimestamp` and `rmsDelta` are not sent to the ONNX models — they are used for internal gate/hold logic in AccompanimentProcessor.

---

## 5. Inference Layer

### 5.1 IInference Interface — `src/inference/IInference.h`

```cpp
class IInference {
    virtual void prepare(double sampleRate) = 0;
    virtual int selectPattern(const FeatureVector& features, int excludeIndex = -1) = 0;
    virtual std::string getName() const = 0;
};
```

Called on the background thread ~50 Hz. The `excludeIndex` parameter implements D-23-04 single-shot rejection: if the result would equal the last pattern, return `(excludeIndex + 1) % 7`.

### 5.2 RuleBasedInference — `src/inference/RuleBasedInference.h/.cpp`

Default implementation. Delegates entirely to `pattern_rules.h`.

**Pattern selection table** (from `src/inference/pattern_rules.h`):

| State | Effective BPM (`bpm + (intensity - 0.5) * 40`) | Pattern Index |
|-------|------------------------------------------------|---------------|
| SILENT | any | 0 |
| SOFT | < 120 | 1 |
| SOFT | 120–160 | 2 |
| SOFT | ≥ 160 | 3 |
| LOUD | < 160 | 4 |
| LOUD | ≥ 160 | 5 |

Pattern 6 (Breakdown) is available in the library but not targeted by the rule path — it is a transition/crash pattern.

**Intensity offset:** `policyIntensity` shifts effective BPM by ±20 BPM (range [0,1] → [−20, +20]).

`pattern_rules.h` is also the single source of truth for `OnnxInference`'s compatibility check and exclusion logic (ARCH-03 — no threshold drift between paths).

### 5.3 OnnxInference — `src/inference/OnnxInference.h/.cpp`

ONNX-backed pattern selector. Active when `MA_ENABLE_ONNX=ON` and model loads successfully.

**Tensor packing** (7 columns sent to model):
```
X[0] = bpm
X[1] = rmsEnergy
X[2] = spectralCentroid
X[3] = highFreqFlux
X[4] = (state == SILENT) ? 1.0 : 0.0   // one-hot encoding
X[5] = (state == SOFT)   ? 1.0 : 0.0
X[6] = (state == LOUD)   ? 1.0 : 0.0
```

Note: `pitchRootMidi`, `pitchConfidence`, `policyIntensity`, `rmsDelta` are **not** passed to the pattern model. The one-hot encoding converts the discrete state to three columns (matching training format from `build_dataset.py`).

**ONNX session config:**
```cpp
opts.SetIntraOpNumThreads(1);
opts.SetInterOpNumThreads(1);
opts.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
```

**Fallback chain:** If ONNX load fails → `RuleBasedInference`. If output violates pattern/state compatibility check (`isPatternCompatibleWithState`) → rule-based override for that call.

**Frozen I/O contract (ONNX-04):**

| | Value |
|--|--|
| Input name | `X` |
| Input shape | `[1, 7]` |
| Input dtype | `float32` |
| Output name | `Y` |
| Output shape | `[1]` |
| Output dtype | `int64` |

### 5.4 OnnxStructureInference — `src/inference/OnnxStructureInference.h/.cpp`

Shadow structure classifier running in parallel with the authoritative rule-based `StructureTagger`. Output is advisory — it shapes non-silent sections when `structureBlend > 0.5`, but the rule path always drives gating (silence detection, MIDI fire/mute).

**Sliding window:** 12 snapshots × 7 features each. Feature layout per snapshot:
```
[bpm, rmsEnergy, spectralCentroid, highFreqFlux, state_numeric, pitchRootMidi, pitchConfidence]
```
`state_numeric` is a direct numeric cast of `StructureState` enum (SILENT=0, SOFT=1, LOUD=2).

**Post-processing:** softmax → confidence threshold + margin threshold → `StructureHoldSmoother` hysteresis before final class decision.

**Metrics exposed** (for debugging/display):
```cpp
struct StructureShadowMetrics {
    StructureState ruleState;
    bool           mlValid;
    StructureState smoothedState;
    float          softmaxTop1;
    float          margin;
    bool           agreeRule;
};
```

**Frozen I/O contract (ONNX-04):**

| | Value |
|--|--|
| Input name | `X_struct` |
| Input shape | `[1, 12, 7]` |
| Input dtype | `float32` |
| Output name | `Y_struct` |
| Output shape | `[1, 3]` |
| Output dtype | `float32` (logits, pre-softmax) |
| Class order | idx 0=SILENT, 1=SOFT, 2=LOUD |

### 5.5 OnnxBassInference — `src/inference/OnnxBassInference.h/.cpp`

Generative bass head. Produces a 16-step piano-roll conditioned on the current feature vector.

**Tensor packing** (7 columns):
```
X_bass[0] = bpm
X_bass[1] = rmsEnergy
X_bass[2] = spectralCentroid
X_bass[3] = highFreqFlux
X_bass[4] = float(state)            // numeric cast, 0/1/2
X_bass[5] = pitchRootMidi
X_bass[6] = pitchConfidence
```

**Output decoding** (32 columns = 16 steps × [pitch_offset, velocity] interleaved):
```
for step in 0..15:
    pitch_offset = Y_bass[2*step]       // relative semitones from root
    velocity     = Y_bass[2*step + 1]   // 0.0 = rest
    if velocity > 0:
        midi_note = clamp(pitchRootMidi + pitch_offset, 0, 127)
        emit note-on at beat_position + step * (1_beat / 4)
```

Bass proposals are held for 2 bars before the next update (hold guard per ARCH-02). Silenced if `pitchConfidence < threshold` or `state == SILENT`.

**Frozen I/O contract (ONNX-05):**

| | Value |
|--|--|
| Input name | `X_bass` |
| Input shape | `[1, 7]` |
| Input dtype | `float32` |
| Output name | `Y_bass` |
| Output shape | `[1, 32]` |
| Output dtype | `float32` |

---

## 6. MIDI Output Layer

### 6.1 MidiPatternLibrary — `src/midi/MidiPatternLibrary.h/.cpp`

Static library of 7 hardcoded drum + bass patterns indexed [0, 6]:

| Index | Name | Context |
|-------|------|---------|
| 0 | Silent | No MIDI output |
| 1 | Verse slow | SOFT, BPM < 120 |
| 2 | Verse mid | SOFT, BPM 120–160 |
| 3 | Verse fast | SOFT, BPM ≥ 160 |
| 4 | Chorus mid | LOUD, BPM < 160 |
| 5 | Chorus fast | LOUD, BPM ≥ 160 |
| 6 | Breakdown | Transition/crash |

Each pattern contains `MidiEvent` structs: `note`, `velocity`, `beatOffset`, `durationBeats`.  
Drum events → MIDI channel 10. Bass events → MIDI channel 2.

### 6.2 PatternPlayer — `src/midi/PatternPlayer.h/.cpp`

Beat-synchronized MIDI renderer. Maintains a continuous beat clock:
```
beatPosition += (numSamples / sampleRate) * (bpm / 60.0)
```

Three bass rendering modes:
1. **Static library:** Emits `bassEvents` from the selected `MidiPattern`
2. **Simple generative:** Repeating single note for a given duration
3. **Piano-roll generative (ONNX):** 16-step sequence from `OnnxBassInference`

Humanization: ±8 samples timing jitter, ±8 velocity variation (JUCE `Random`).  
Pattern transitions occur at bar boundaries. Crash cymbal arm/disarm is one-shot per transition.

### 6.3 BassMidiValidator — `src/midi/BassMidiValidator.h/.cpp`

Sanity-checks generated bass MIDI: pitch range [0, 127], velocity [0, 127], no overlapping notes on the same channel, duration bounds.

---

## 7. Model Architectures

### 7.1 PatternNet (pattern selection)

```
Input: [N, 5]   (core features: bpm, rms, centroid, hf_flux, state_float)
  Normalization: baked z-score (mean/std from training data, stored in ONNX graph)
  One-hot state: decoded to scalar before normalization inside PatternOnnxExport

  fc1: Linear(5 → 64)  + LayerNorm(64) + ReLU + Dropout(p=0.12)
  fc2: Linear(64 → 32) + LayerNorm(32) + ReLU + Dropout(p=0.12)
  fc3: Linear(32 → 7)  → logits

  PatternOnnxExport wrapper: accepts [N, 7] (4 core + 3 one-hot state),
    collapses one-hot to scalar, normalizes 5-vector, runs net, returns argmax as int64
Output: [N] int64
```

**Training loss:** Cross-entropy with inverse-frequency class weights  
**Early stopping:** patience=12 on val macro-F1  
**Quality gate:** `best_macro_f1 ≥ 0.55` or training script exits with error  
**Opset:** ONNX opset 17

### 7.2 BassNet (generative bass)

```
Input: [1, 7]   (bpm, rms, centroid, hf_flux, state_float, pitchRootMidi, pitchConfidence)
  Normalization: baked z-score (mean/std from bass training data)

  fc1: Linear(7 → 32)  + LayerNorm(32) + ReLU + Dropout(p=0.2)
  fc2: Linear(32 → 16) + LayerNorm(16) + ReLU + Dropout(p=0.2)
  fc3: Linear(16 → 32) → piano-roll regression

Output: [1, 32] float32
  Layout: [pitch_0, vel_0, pitch_1, vel_1, ..., pitch_15, vel_15]
  pitch_i: relative semitones from pitchRootMidi
  vel_i: 0.0 = rest
```

**Training loss:** MSE  
**Early stopping:** patience=12 on val MSE  
**Batch size constraint:** N=1 only (enforced in `BassOnnxExport.forward`)  
**Opset:** ONNX opset 17

### 7.3 StructureNet (structure shadow)

```
Input: [1, 12, 7]   (12-snapshot rolling window, 7 features per snapshot)
  Features per snapshot: bpm, rms, centroid, hf_flux, state_numeric, pitchRootMidi, pitchConfidence
  No baked normalization (note: training script handles this offline)

  Flatten → [1, 84]

  fc1: Linear(84 → 64) + LayerNorm(64) + ReLU + Dropout(p=0.2)
  fc2: Linear(64 → 32) + LayerNorm(32) + ReLU + Dropout(p=0.2)
  fc3: Linear(32 → 3)  → logits (no softmax baked in)

Output: [1, 3] float32 logits
  Class order: 0=SILENT, 1=SOFT, 2=LOUD
```

**Post-softmax gating:** confidence threshold + margin threshold applied in `OnnxStructureInference` before committing to a class.  
**Opset:** ONNX opset 17

---

## 8. Bundled Model Files

| File | Size | Contract | Active by default |
|------|------|----------|-------------------|
| `assets/accompaniment_model.onnx` | 14.6 KB | ONNX-04: X[1,7]→Y[1] int64 | No (MA_ENABLE_ONNX=ON required) |
| `assets/bass_model.onnx` | 7.6 KB | ONNX-05: X_bass[1,7]→Y_bass[1,32] float32 | No |
| `assets/structure_model.onnx` | 32.7 KB | ONNX-04: X_struct[1,12,7]→Y_struct[1,3] float32 | No |

All three are embedded via JUCE BinaryData when `MA_ENABLE_ONNX=ON` and accessed as `BinaryData::accompaniment_model_onnx` etc.

---

## 9. Training Pipeline

### 9.1 Data Sources

| Source | Script | Purpose |
|--------|--------|---------|
| Google Magenta **Groove MIDI Dataset** (GMD) v2.0.1 | `download_gmd.py` | Primary drum performance data (TFDS) |
| **Lakh MIDI Dataset** | `download_lakh.py` + `filter_lakh.py` | Supplementary MIDI corpus |

GMD: `groove/full-midionly:2.0.1` via TensorFlow Datasets. Each example provides: MIDI bytes, BPM metadata, session ID.

### 9.2 Feature Proxy — The Domain Gap

Training data is **symbolic MIDI** (drum kit performances). The live plugin processes **guitar audio**. The proxy mapping is documented in `training/FEATURE_PROXY.md` and implemented in `build_dataset.py`:

| Feature | Audio-thread source (plugin) | Training proxy (MIDI) |
|---------|-----------------------------|-----------------------|
| `bpm` | OnsetDetector (STFT onset peaks) | GMD example BPM metadata, clamped [80, 220] |
| `rmsEnergy` | Rolling 100 ms RMS of audio | `sqrt(mean(velocity²)) / 127` over all note-ons |
| `spectralCentroid` | Weighted mean frequency (Hz) | `400 + (mean_midi_note / 127) * 6000` Hz |
| `highFreqFlux` | 2 kHz+ band spectral flux | Std-dev of inter-onset gaps (beats) for notes ≥ MIDI 42, capped at 1.0 |
| `state` (float) | StructureTagger enum cast | Heuristic: SILENT if <3 onsets or RMS<0.02; LOUD if density≥10 and HF≥0.35; else SOFT |

**This is a meaningful domain gap.** The proxy approximates statistical properties but is not pointwise equivalent to live audio features. No domain adaptation is currently implemented.

### 9.3 Label Generation (Pattern Model)

Labels Y ∈ [0, 6] are assigned by **train-split quantile binning** on a scalar activity score:
```python
activity_score = 0.5*note_density + 2.0*rms + 0.5*hf_flux + 0.2*state_float
quantile_edges = np.quantile(scores_train, [1/7, 2/7, 3/7, 4/7, 5/7, 6/7])
Y = np.searchsorted(quantile_edges, score, side='right')
```

This is **ordinal** (0 = lowest activity, 6 = highest) — not a direct semantic mapping to pattern names. Pattern 0 does not reliably correspond to the "Silent" pattern from `MidiPatternLibrary`; it corresponds to the lowest-activity seventh of training examples.

**Train/val split:** `GroupShuffleSplit(test_size=0.2)` on session IDs to prevent leakage between splits.

**DATA-06 histogram gate:** If any of the 7 classes has fewer than 50 training examples, `build_dataset.py` exits with code 3. This prevents degenerate datasets from proceeding to training.

**Normalization:** z-score (mean/std fit on train split only). Stats saved to `norm_stats.json` and baked into the ONNX graph via `PatternOnnxExport`.

### 9.4 Pattern Model Training — `training/train_gmd.py`

```
optimizer:     Adam, lr=1e-3
loss:          CrossEntropyLoss with inverse-frequency class weights
batch_size:    64
max_epochs:    200
patience:      12 (on val macro-F1)
quality_gate:  best_macro_f1 ≥ 0.55 (PMODEL-04)
seed:          42
```

Output artifacts (timestamped dir `training/artifacts/pattern-merged-<UTC>/`):
- `pattern_trained.onnx` — final ONNX model
- `best_model.pt` — PyTorch state dict
- `metrics.jsonl` — per-epoch train_loss / val_loss / val_macro_f1
- `validation.json` — best macro-F1, per-class F1, confusion matrix
- `norm_stats.json` — feature normalization parameters

### 9.5 Bass Model Training — `training/train_bass.py`

```
optimizer:     Adam, lr=1e-3
loss:          MSELoss
batch_size:    64
max_epochs:    200
patience:      12 (on val MSE)
seed:          42
```

No quality gate (unlike pattern model). Normalization is fit inline during training and saved as `bass_norm_stats.json`.

Output artifacts (`training/artifacts/bass-<UTC>/`):
- `bass_trained.onnx`
- `bass_norm_stats.json`
- `metrics.jsonl`
- `validation.json` (best val MSE)

### 9.6 Structure Model Training — `training/train_structure.py`

Trains `StructureNet` on labeled structure windows from `build_structure_dataset.py`.

### 9.7 Quickstart Stub — `training/train_pytr_stub.py`

Generates minimal synthetic models without downloading real datasets. Used for CI validation and testing the ONNX export pipeline:
```bash
python3 training/train_pytr_stub.py --epochs 3
# → training/artifacts/pytr-stub-<timestamp>/pattern_trained.onnx + bass_trained.onnx
```

### 9.8 Full Pipeline Commands

```bash
# 1. Download data
python3 training/download_gmd.py

# 2. Build train/val tensors (exits code 3 if any class < 50 examples)
python3 training/build_dataset.py

# 3. Build bass dataset
python3 training/build_bass_dataset.py

# 4. Train pattern model
python3 training/train_gmd.py
#    → training/artifacts/pattern-merged-<UTC>/pattern_trained.onnx

# 5. Train bass model
python3 training/train_bass.py
#    → training/artifacts/bass-<UTC>/bass_trained.onnx

# 6. Validate ONNX contracts
python3 scripts/validate_onnx_contract.py --pattern <dir> --bass <dir>

# 7. Install to plugin assets
./scripts/install-model-local.sh --pattern-dir <dir> --bass-dir <dir>
```

### 9.9 Python Dependencies

```
torch==2.6.0
onnx==1.16.2
onnxruntime==1.20.1
tensorflow==2.21.0
tensorflow-datasets==4.9.9
numpy==2.1.3
scikit-learn==1.8.0
mido==1.3.2
```

---

## 10. Test Coverage

### 10.1 C++ Unit Tests (Catch2, `tests/`)

| Test File | What's Covered |
|-----------|----------------|
| `test_onset_detector.cpp` | Spectral flux, IOI filtering, tempo locking, peak-picking |
| `test_onset_detector_extended.cpp` | Adaptive threshold, octave folding, refractory period |
| `test_beat_tracker.cpp` | Comb filter, confidence, phase tracking |
| `test_energy_analyser.cpp` | RMS windowing, spectral centroid, HF flux |
| `test_structure_tagger.cpp` | State machine, hold timers, hysteresis |
| `test_structure_tagger_extended.cpp` | Threshold precision, peak ratio edge cases |
| `test_structure_hold_smoother.cpp` | Hold smoother for ONNX shadow path |
| `test_structure_hold_smoother_complete.cpp` | Edge cases, all transitions |
| `test_pitch_estimator.cpp` | YIN algorithm, confidence calculation |
| `test_stable_pitch_tracker.cpp` | Pitch stability window, offset mapping |
| `test_rule_based_inference.cpp` | Pattern selection, exclusion mechanism |
| `test_pattern_rules.cpp` | BPM thresholds, compatibility checks |
| `test_pattern_player.cpp` | Beat clock, bar transitions, MIDI rendering |
| `test_pattern_player_generative_bass.cpp` | Piano-roll decoding, step timing |
| `test_bass_generative.cpp` | Generative bass state transitions |
| `test_bass_validator_boundaries.cpp` | MIDI range validation |
| `test_tempo_stabiliser.cpp` | BPM hysteresis, deadband |
| `test_playback_gate.cpp` | Gate logic, phrase breath, beat snap |
| `test_e2e_silent_section.cpp` | E2E: silence handling and gate behavior |
| `test_e2e_bpm_tracking.cpp` | E2E: onset + beat tracker + TempoStabiliser |
| `test_e2e_structure_transitions.cpp` | E2E: structure FSM across blocks |
| `test_processor_pipeline.cpp` | Full AccompanimentProcessor pipeline |
| `test_structure_shadow_integration.cpp` | Rule vs ONNX shadow agreement |
| `test_structure_rule_agreement.cpp` | Agreement metrics from shadow path |

### 10.2 Python Tests (pytest, `training/tests/`)

| Test File | What's Covered |
|-----------|----------------|
| `test_dataset_build.py` | GMD tensor build, histogram gate, GroupShuffleSplit |
| `test_onnx_contract.py` | ONNX input/output shape and dtype contracts |
| `test_prep_midi.py` | MIDI event parsing from `prep_midi.py` |

### 10.3 CI/CD (`.github/workflows/ci.yml`)

Every push/PR to main runs:
1. CMake Release configure + parallel build
2. C++ unit tests (Catch2, labeled `unit`)
3. C++ integration tests (labeled `integration`)
4. C++ E2E tests (labeled `e2e`)
5. Python pytest on `training/`
6. ONNX contract validation (Phase 22)
7. ONNX-enabled build smoke test (Phase 22)
8. MIDI prep smoke test (Phase 9)

---

## 11. Key Design Decisions and Trade-offs

### Domain Gap (MIDI training → Guitar audio inference)
The pattern model is trained on symbolic MIDI drum data (GMD). At runtime it receives audio-derived features from a guitar signal. The feature proxy document (`training/FEATURE_PROXY.md`) maps one to the other statistically but explicitly disclaims pointwise identity. This is the most significant ML risk in the system: a model trained on drum velocities and note density is being asked to generalize to STFT-derived guitar features.

### ARCH-03 — Single source of pattern-rule truth
`src/inference/pattern_rules.h` is the sole definition of BPM thresholds and pattern compatibility. Both `RuleBasedInference` and `OnnxInference` import it. This prevents the rule-based fallback from drifting away from the ONNX path over time, ensuring the fallback is predictable even after model updates.

### ONNX-03 — No inference on audio thread
All three ONNX models run exclusively on the background thread. The audio thread only does arithmetic on pre-allocated buffers. This protects the <30 ms end-to-end latency requirement.

### PUI-01 — policyIntensity as user control
The intensity knob [0,1] shifts effective BPM by ±20 in the rule path. It is carried in the `FeatureVector` but not forwarded to any ONNX model tensor — ONNX sees the raw BPM, not the adjusted value. This means user intensity has no effect on ONNX outputs (only affects rule path and rule-path fallback within OnnxInference).

### D-23-04 — Single-shot pattern exclusion
Prevents the same pattern from repeating when the inference output would otherwise stick. Modulo wraps to `(last + 1) % 7`. This is a blunt mechanism — pattern 6 (Breakdown) could be selected by exclusion even when inappropriate.

### Ordinal labels vs semantic pattern names
Training labels 0–6 are quantile bins on activity score, not semantic pattern indices. Class 4 in training does not correspond to "Chorus mid" in `MidiPatternLibrary`. The model learns a low→high activity mapping; the rule path maps the same direction. At runtime both paths should agree on relative ordering but may disagree on specific boundaries.

---

## 12. Known Gaps and Risks

| Issue | Severity | Notes |
|-------|----------|-------|
| **Label/pattern mismatch** | Medium | Training labels are activity quantiles, not `MidiPatternLibrary` indices. The model and rule path agree on ordering but not on boundaries or specific pattern semantics. |
| **Domain gap** | High | MIDI velocity/density proxies ≠ guitar audio STFT features. No domain adaptation implemented. ONNX model performance on live guitar is untested beyond stub validation. |
| **policyIntensity not passed to ONNX** | Low | Intensity knob has no effect on ONNX-path outputs. Rule path and ONNX path respond differently to the same knob position. |
| **Pattern 6 reachable via exclusion wrap** | Low | Breakdown pattern (index 6) is never directly selected by either inference path, but D-23-04 modulo exclusion can land on it. |
| **Structure model normalization** | Medium | `StructureNet` has no baked z-score normalization in the ONNX graph (unlike PatternNet). The runtime `OnnxStructureInference` must match whatever normalization was applied during training. |
| **Bass model quality gate absent** | Low | `train_bass.py` has no minimum-MSE gate, unlike the pattern model's `macro_f1 ≥ 0.55`. A poorly converged bass model would be exported silently. |
| **No real audio evaluation** | High | All CI validation is synthetic (MIDI proxies or unit tests). No benchmark of model accuracy on real guitar recordings exists. |
| **CPU profiling absent for ONNX** | Low | The 20 ms background loop cadence is assumed sufficient. Actual ONNX inference time on target hardware has not been measured in CI. |

---

## 13. Component Summary Table

| Component | Thread | Input | Output | Cadence |
|-----------|--------|-------|--------|---------|
| OnsetDetector | Audio | Raw audio | BPM [80,220], onset flag | Per block |
| BeatTracker | Audio | Flux samples | Phase [0,1), confidence, lock | Per FFT hop |
| EnergyAnalyser | Audio | Raw audio | RMS, centroid Hz, HF flux | Per block |
| StructureTagger | Audio | Energy features | State {SILENT,SOFT,LOUD} | Per block |
| PitchEstimator | Audio | Mono audio | MIDI note (float), confidence | Per block |
| StablePitchTracker | Audio | Pitch+confidence | Semitone offset or INT_MIN | Per block |
| PlaybackGate | Audio | State, phase, locks | GateDecision | Per block |
| TempoStabiliser | Audio | BPM candidate | Stable BPM | Per block |
| FeatureVector | Queue | Analyzed features | Inference input | Per block |
| RuleBasedInference | Background | FeatureVector | Pattern index [0–6] | ~50 Hz |
| OnnxInference | Background | FeatureVector (7-col) | Pattern index [0–6] | ~50 Hz |
| OnnxStructureInference | Background | Feature window [12×7] | Shadow metrics + logits | ~50 Hz |
| OnnxBassInference | Background | FeatureVector (7-col) | Piano-roll [1×32] | ~50 Hz |
| PatternPlayer | Audio | Pattern index, BPM, bass steps | MIDI buffer | Per block |
| BassMidiValidator | Offline | MIDI notes | Validation pass/fail | On demand |
