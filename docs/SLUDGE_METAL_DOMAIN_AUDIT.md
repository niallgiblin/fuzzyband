# Sludge Metal Domain Audit — Metal Accompaniment v0.7.0

**Date:** 2026-06-03 | **Scope:** Full-plugin audit from sludge metal domain perspective
**Includes:** Guitar→decision pipeline, ML pipeline, UX analysis, pattern library, training data deep-dive

---

## Executive Summary

The Metal Accompaniment plugin has strong engineering fundamentals (lock-free threading, clean architecture, ONNX readiness) but is **not musically functional for sludge metal** in its current form. Three root causes:

1. **Domain blind spot** — The plugin was designed for generic "metal" with tempo ranges and pattern idioms drawn from thrash/death metal, not the slow, drone-heavy, dynamic-extreme characteristics of sludge.
2. **Training proxy gap** — The ONNX models are trained on MIDI-derived feature proxies whose distributions differ profoundly from real guitar audio features. The normalization statistics baked into the ONNX models are invalid for live input.
3. **Decision rigidity** — Pattern selection is fundamentally rule-based (three states, six BPM thresholds), and the ONNX model only learns to imitate those same rules via a proxy oracle — it can never exceed the rule baseline.

---

## 1. Sludge Metal Domain Analysis

### 1.1 Defining Characteristics of Sludge Metal

| Attribute | Sludge Metal | Current Plugin Assumption | Gap |
|-----------|-------------|--------------------------|-----|
| **Tempo range** | 40–140 BPM (often 50–85) | 80–220 BPM (`kMinBpm=80`, `kMaxBpm=220`) | **Critical**: Sub-80 BPM rejected entirely |
| **Guitar texture** | Wall of distortion, feedback drones, palm-muted chug | Clean onset detection via spectral flux peaks | Feedback/drone sections have no discrete onsets → **tempo lock lost** |
| **Dynamics** | Extreme (near-silent ambient → crushing wall), gradual builds | Three discrete states (SILENT/SOFT/LOUD) with hysteresis | Missing: drone, ambient, tension-build, feedback-outro |
| **Drum feel** | Half-time grooves, massive crashes, sparse kick patterns, swung 8ths | Straight 4/4 patterns oriented to thrash/death | No half-time core, no swing, no doom pacing |
| **Song structure** | Long sections (8–32 bars), drone intros, gradual transitions | 2-bar hold guard, bar-quantized state changes | Too fast for sludge's slow-burn transitions |
| **Bass role** | Often unison with guitar, octave-down, fuzz-saturated, drone roots | Independent 16-step piano-roll generative bass | Bass should track guitar root with minimal variation |
| **Riff character** | Repetitive, hypnotic, single-note/chord drones, chromatic | Expects varied note attacks for IOI-based BPM | Monotonous riffing produces sparse/misleading onsets |
| **Drum tones** | Low-tuned, roomy, big crashes, ride bell accents | GM kit (kick 36, snare 38, hats 42/46, ride 51) | Functional but no genre-specific articulation |

### 1.2 Reference Sludge Bands & Tempos

| Band | Typical BPM | Signature Traits |
|------|------------|------------------|
| Eyehategod | 55–85 | Sludgy swing, d-beat variants |
| Melvins (early) | 45–70 | Ultra-slow, massive drum spacing |
| Crowbar | 65–90 | Half-time crush, big snare on 3 |
| Acid Bath | 60–120 | Dynamic extremes, clean→heavy transitions |
| Neurosis | 50–80 | Drone builds, tribal drumming, gradual intensification |
| Thou | 50–100 | Feedback walls, dense distortion, slow-burn |

**The plugin's 80 BPM floor excludes the core tempo range of 5 of 6 reference bands.**

---

## 2. Guitar Input → Decision Pipeline Audit

### 2.1 Signal Chain (Audio Thread)

```
Guitar → OnsetDetector ──→ BPM (80–220 clamp)
       → EnergyAnalyser ──→ RMS, Centroid, HF Flux
       → StructureTagger ──→ SILENT/SOFT/LOUD
       → PitchEstimator ──→ rootMidi + confidence
       → BeatTracker ──→ beat phase + lock status

FeatureVector{bpm, rms, centroid, hfFlux, state, pitch, confidence, intensity, rmsDelta}
       │
       ▼ [lock-free queue → inference thread]
       
IInference::selectPattern() → pattern index [0–10]
       │
       ▼ [atomic → audio thread]
       
PatternPlayer → MIDI ch.10 (drums) + ch.2 (bass)
```

### 2.2 OnsetDetector — BPM Foundation (CRITICAL ISSUES)

**Issue 2.2.1: BPM floor at 80 excludes sludge core range**

`src/analysis/OnsetDetector.h:109-110`:
```cpp
static constexpr float kMinBpm = 80.0f;
static constexpr float kMaxBpm = 220.0f;
```

Sludge metal frequently operates at 50–75 BPM. The IOI filter in `pushIoi()` (lines 119–127 of OnsetDetector.cpp) rejects intervals outside `60/kMaxBpm` to `60/kMinBpm`. This means any inter-onset interval corresponding to a valid sludge tempo is silently discarded. The plugin will either:
- Report 2× the actual tempo (octave aliasing) if the guitarist plays 8th notes
- Report 120 BPM default if it can't lock

**Fix priority: CRITICAL.** Lower `kMinBpm` to 40.0 and `kMaxBpm` to 300.0. The octave disambiguation in `medianIoiBpm()` (1.7×/0.6× folding) should handle sub-80 detection if given valid IOIs.

**Issue 2.2.2: Onset refractory period kills sludge groove detection**

`src/analysis/OnsetDetector.cpp:32`:
```cpp
minOnsetIntervalSamples = juce::jmax(1, static_cast<int>(0.08 * sampleRate));
```

80ms refractory period means onsets closer than 80ms are rejected. At 60 BPM, 8th notes are 125ms apart — fine. But 16th-note double-kick patterns or fast snare fills (common even in slow sludge) will be rejected, causing tempo estimate to drift toward half-time or lose lock entirely. More critically, distorted guitar chugs create multiple spectral flux peaks per pick attack.

**Recommendation:** Reduce refractory to 50ms and/or implement a "burst detector" that recognizes closely-spaced onsets as articulations of a single musical event.

**Issue 2.2.3: Tempo lock requires 8+ consistent IOIs — fragile for drone sections**

`OnsetDetector.cpp` requires 8 consecutive IOIs within ±8% before `tempoLocked = true`. During drone sections (common in sludge), no onsets occur, causing lock to persist but the BeatTracker's phase to drift. When the riff returns, the phase may be misaligned.

**Recommendation:** Add a "coasting" mode where BPM is held during drone sections without requiring continuous onsets.

### 2.3 EnergyAnalyser — Feature Extraction (MODERATE ISSUES)

**Issue 2.3.1: RMS window too short for sludge dynamics**

`EnergyAnalyser.cpp:28`: 100ms RMS window. Sludge dynamics evolve over 4–16 bars, not milliseconds. The RMS reading is noisy and reacts to pick attacks rather than section energy.

**Recommendation:** Add a parallel 1–2 second RMS envelope for section-level energy classification.

**Issue 2.3.2: Spectral centroid in distorted guitar is ceiling-limited**

Highly distorted guitar has abundant high-frequency harmonics regardless of playing intensity. `spectralCentroid` may saturate near 8–11 kHz for any non-silent playing, making it a poor discriminator between SOFT and LOUD in sludge. The capture data confirms: p50 centroid is ~9791 Hz even during SOFTER playing.

**Recommendation:** Add a sub-bass energy band (40–120 Hz) measurement — sludge's "weight" lives in the low end, not the high end. A "heaviness ratio" (low-energy / total-energy) would better discriminate sludge dynamics.

**Issue 2.3.3: HF flux (2 kHz+) is dominated by pick noise, not cymbal activity**

In sludge, the primary high-frequency content comes from guitar distortion harmonics, not the drummer's cymbals. The flux reading conflates pick attack density with actual drum complexity.

### 2.4 StructureTagger — Three-State Limitation (MAJOR ISSUE)

**Issue 2.4.1: Three states insufficient for sludge dynamics**

Current: `SILENT` → `SOFT` → `LOUD`

Sludge requires at minimum:
- `SILENT` — no playing (keep)
- `AMBIENT` — feedback trails, single-note drones, sparse clean picking
- `BUILDING` — increasing intensity, more notes, palm-muted chug entry
- `HEAVY` — full distortion, maximum density (current LOUD)
- `BREAKDOWN` — slow, sparse, maximum weight, half-time

The three-state model forces everything between silence and full-blast into "SOFT," which triggers verse patterns inappropriate for sludge ambience.

**Issue 2.4.2: RMS-only thresholding misses sludge's spectral signatures**

`StructureTagger::computeDesiredState()` uses only `rms` vs peak ratio. Sludge heaviness is defined more by low-frequency density than broadband RMS. A palm-muted chug at 60 BPM can have lower RMS than a clean arpeggio but sound infinitely heavier.

**Issue 2.4.3: Hold times too short for sludge transitions**

| Transition | Current Hold | Sludge-Appropriate |
|-----------|-------------|-------------------|
| SOFT→LOUD | 0.20s | 2–4 bars (~8–16s at 60 BPM) |
| LOUD→SOFT | 1.50s | 2–4 bars |
| LOUD→SILENT | 2.00s | 3–5 bars (feedback tail) |

The current hold times will cause state oscillation during gradual sludge builds.

### 2.5 PlaybackGate — Gate Logic (MODERATE ISSUES)

**Issue 2.5.1: Phrase breath hold too short**

`PlaybackGate.h`: `kPhraseBreathHoldSec = 3.0` — 3 seconds of silence before gating off. Sludge frequently has 4–8 second gaps between sections (feedback ringing out). At 60 BPM, 3 seconds = 3 beats, which would cut off mid-feedback-decay.

### 2.6 TempoStabiliser (MODERATE ISSUES)

**Issue 2.6.1: Deadband too narrow for sludge tempo variation**

`TempoStabiliser.h`: `kTempoChangeDeadbandBpm = 4.0f` — intentional tempo changes in sludge (e.g., 60→75 BPM between sections) take 2+ seconds to propagate through the 4 BPM deadband. Meanwhile, the plugin is playing at the wrong tempo.

**Issue 2.6.2: Hold time too short**

`kTempoChangeHoldSec = 2.0` — a 2-second BPM fluctuation confirmation window means a single DJENT palm-mute passage that confuses the onset detector for 2 seconds will shift the playback tempo.

**Recommendation:** Increase hold to 4–6 seconds or require bar-boundary confirmation.

---

## 3. Pattern Library — Drum Patterns

### 3.1 Current Pattern Inventory

| Index | Name | State | BPM | Sludge Fit |
|-------|------|-------|-----|-----------|
| 0 | Silent | SILENT | — | ✓ Appropriate |
| 1 | Verse slow | SOFT | <120 | ⚠ Half-time needed |
| 2 | Verse mid | SOFT | 120–160 | ✗ Too fast for sludge |
| 3 | Verse fast | SOFT | ≥160 | ✗ Not sludge |
| 4 | Chorus mid | LOUD | <160 | ⚠ Needs half-time option |
| 5 | Chorus fast | LOUD | ≥160 | ✗ Thrash territory |
| 6 | Breakdown | special | <110 | ⚠ Overly busy for sludge |
| 7 | Half-Time | SOFT only | any | ⚠ Should be available in LOUD |
| 8 | Blast Beat | LOUD | ≥160+centroid>800 | ✗ Death/black metal, not sludge |
| 9 | Sparse Breakdown | LOUD | <140, low RMS | ✓ Best sludge pattern |
| 10 | Thrash | LOUD | ≥140 | ✗ Wrong genre |

**Sludge-appropriate count: 2 out of 11 patterns (indices 0 and 9).**

### 3.2 Missing Sludge Drum Patterns

| Pattern | Description | Kick Pattern | Snare | Cymbals |
|---------|-------------|-------------|-------|---------|
| **Doom Crawl** | Ultra-slow 40–70 BPM, massive spacing | 1, (2&), 3 | 3 | Ride bell on 1, crash on section changes |
| **Sludge Groove** | Mid-slow with swing, 60–90 BPM | 1, 3, (4&) | 3 (heavy) | Open hat wash, crash accents |
| **Half-Time Crush** | LOUD half-time, 50–85 BPM | 1, 3 | 3 (massive) | China/ride on quarters |
| **D-Beat Sludge** | Sludgy d-beat variant, 70–100 BPM | 1, 3, 4& | 2, 4 | Hat barks between kicks |
| **Drone Pulse** | Minimal, AMBIENT state | 1 (subdued) | — | Soft ride taps, no hats |
| **Feedback Ritual** | Sparse, ceremonial | 1, (3) | Occasional floor tom | Swelling crashes, bell hits |
| **Tension Build** | Graduating fill density | crescendo 8ths → 16ths | snare roll entry | Open hat → crash ride |

### 3.3 Existing Pattern Issues

**"Half-Time" (index 7):** Only available in SOFT state via `diversifyPattern()`. It should also be available in LOUD state — half-time at high energy is the defining sludge drum feel.

**"Breakdown" (index 6):** Has 12 drum events in 2 bars — much busier than sludge breakdowns which often have 4–6 hits per 2 bars. Compare: Crowbar breakdowns are k---k---s--- with massive space between hits.

**"Sparse Breakdown" (index 9):** Has exactly 3 hits in 2 bars — this is the closest to sludge but should have a floor tom or china hit for articulation.

**Bass patterns:** All library bass patterns play root notes (E2=40) with static rhythm. In sludge, bass often follows the guitar riff in unison or octave-down, with sustained notes, not rhythmic patterns.

---

## 4. ML Pipeline Audit

### 4.1 Pattern Model (`accompaniment_model.onnx`)

**Architecture:** `5→64→32→7` MLP with LayerNorm + Dropout (0.12)

**Input:** `X` [1,7] = [adjustedBpm, rmsEnergy, spectralCentroid, highFreqFlux, stateSILENT_onehot, stateSOFT_onehot, stateLOUD_onehot]

**Output:** `Y` [1] int64 ∈ [0,6]

**Training data:** GMD + Lakh MIDI → proxy features → rule oracle labels

#### CRITICAL: Normalization Statistics Mismatch

`assets/norm_stats.json` bakes these values into the ONNX graph:

```
mean: [119.6,  0.719,  11200.0, 0.760,  1.631]
std:  [28.32,  0.114,  1e-08,   0.206,  0.484]
```

Real guitar capture statistics (from 13,151 frames):

```
capture mean: [120.69, 0.017,  8004.8,  0.033,  0.202]
capture std:  [1.57,   0.043,  3665.9,  0.226,  0.434]
```

**When live guitar audio flows through this model:**

| Feature | Effect of normalization | Impact |
|---------|------------------------|--------|
| `bpm` | Mean matches (~120), std is 18× too large — **blunted sensitivity** | Moderate |
| `rmsEnergy` | Mean 0.017 vs 0.719 → normalized to `(0.017-0.719)/0.114 = -6.15` — **massively negative** | **Critical** |
| `spectralCentroid` | std=1e-8 (clamped) → `(8000-11200)/1e-8 = -3.2e11` — **astronomically large, saturates network** | **Critical** |
| `highFreqFlux` | Mean 0.033 vs 0.760 → normalized to `-3.54` — **strongly negative** | **Critical** |
| `state_float` | Mean 0.202 vs 1.631 → normalized to `-2.95` — **strongly negative** | **Critical** |

**The spectral centroid std=1e-08 is particularly catastrophic.** This was clamped from a near-zero proxy std (the Phase 36 fix formula produces values clustered around 9500–11000 Hz with low variance). When live audio with centroid 8000 Hz passes through, the normalized value explodes to ~-3e11, saturating any ReLU activations and making the model's output effectively random.

**The model is operating on features normalized with statistics from a completely different distribution. It cannot make meaningful predictions on live input.**

#### Additional Issues

- **Label oracle problem:** The model learns to imitate `PatternRules::rulePatternForState()`, not actual musical decisions. It cannot exceed the rule baseline.
- **5-feature bottleneck:** The model discards `pitchRootMidi` (crucial for bass compatibility), `pitchConfidence`, and `rmsDelta` (critical for transition detection).
- **Macro-F1 0.639** against rule-labeled GMD/Lakh data — means the model only agrees with the rules ~64% of the time even on in-distribution proxy data.
- **Breakdown class (6):** Only triggered by raw BPM < 110 in the oracle. No spectral or dynamic criteria. Sludge breakdowns can happen at any tempo when the feel changes.

### 4.2 Structure Model (`structure_model.onnx`)

**Architecture:** `(12×7)→64→32→3` temporal MLP with LayerNorm + Dropout (0.2)

**Input:** `X_struct` [1,12,7] — 12-frame window of raw FeatureVector snapshots

**Output:** `Y_struct` [1,3] logits → softmax → SILENT/SOFT/LOUD

#### Issues:

- **Same normalization problem:** The structure model also bakes training-set mean/std into the ONNX graph. Live audio features get normalized incorrectly.
- **Confidence gate thresholds:** `kSoftmaxConfidenceThreshold` and `kMarginThreshold` (in OnnxStructureInference) are tuned on proxy data where state distribution is 81% non-silent vs 20% in captures. The gate may be too strict or too loose in practice.
- **12-frame window (~240ms at 50Hz inference rate):** Too short for sludge. A gradual build over 8 bars (32 seconds at 60 BPM) cannot be captured in a 240ms window. The model has no long-term memory.

### 4.3 Bass Model (`bass_model.onnx`)

**Architecture:** Unknown feed-forward, `X_bass` [1,7] → `Y_bass` [1,32] (16 step × 2 values)

**Issues (noting another agent is fixing bass):**
- Independent 16-step piano roll is inappropriate for sludge bass (should track guitar root)
- No droptune/detune awareness — sludge bass is often in drop-C or lower
- The model uses `static_cast<float>(static_cast<int>(f.state))` as input (0.0/1.0/2.0), not one-hot — different encoding from pattern model, potential inconsistency

### 4.4 Training Pipeline

```
GMD/Lakh MIDI → build_dataset.py → proxy features (.pt) → merge → train_gmd.py → .onnx
                                           │
                                    rule oracle labels
```

**Fundamental circularity:** The training labels come from `PatternRules::rulePatternForState()` — the same C++ rules used for fallback. The "ML model" is therefore a function approximator for a function we already have. The ONNX path adds latency and error without providing new decision capability.

**To break this circularity, the model needs:**
1. Training on **real guitar audio captures** with **human annotations** of appropriate patterns
2. Or: reinforcement from user feedback (user rejects pattern → negative training signal)
3. Or: training on actual multi-track recordings where drum stems are separated and analyzed

---

## 5. Training Data Deep-Dive

### 5.1 Data Sources

| Source | Examples | Classes Present | Notes |
|--------|----------|----------------|-------|
| **GMD** (Groove MIDI Dataset) | 2,151 | 0 (Silent), 1 (Verse slow), 5 (Chorus fast) | Only 3 of 7 classes present; no mid-tempo, no breakdown |
| **Lakh MIDI** (filtered) | 241,101 | 1–5 dominant, 6 (Breakdown) = 51 | No Silent rows at all; 112× larger than GMD |
| **Merged train** | 243,252 | All 7 classes (but see imbalance below) | 99.1% Lakh, 0.9% GMD |
| **Validation** (GMD-only) | 540 | 0, 1, 5 only | Structurally broken — tests only 3 of 7 classes |
| **Real guitar captures** | 0 in training | N/A | None. Zero. The model has never seen real audio features. |

### 5.2 Class Distribution — Catastrophic Imbalance

```
Class 0 (Silent):         747   ( 0.3%)  ▏
Class 1 (Verse slow):  40,945   (16.8%)  ████
Class 2 (Verse mid):   35,488   (14.6%)  ███▌
Class 3 (Verse fast):  11,761   ( 4.8%)  █▏
Class 4 (Chorus mid): 144,717   (59.5%)  ██████████████▊  ← DOMINANT
Class 5 (Chorus fast):  9,543   ( 3.9%)  █
Class 6 (Breakdown):       51   (0.02%)  · (essentially absent)
```

**59.5% of all training examples are Chorus mid.** The model is overwhelmingly biased toward predicting class 4. The class weight balancing in `train_gmd.py` helps but cannot compensate for this degree of imbalance when coupled with the next problem.

### 5.3 Validation Set — Structurally Broken

The validation set (GMD-only, 540 examples) contains:

| Class | Count |
|-------|-------|
| 0 (Silent) | 198 |
| 1 (Verse slow) | 69 |
| 2 (Verse mid) | **0** |
| 3 (Verse fast) | **0** |
| 4 (Chorus mid) | **0** |
| 5 (Chorus fast) | 273 |
| 6 (Breakdown) | **0** |

**4 of 7 classes have zero validation examples.** The macro-F1 of 0.639 is computed from only 3 classes (0, 1, 5). Classes 2, 3, 4, and 6 — which comprise 78.9% of the training data — can never be validated. The model could predict class 4 for every single input and the validation would show "perfect" results because class 4 doesn't exist in the val set.

**Per-class F1 scores from the most recent training (2026-06-02):**

| Class | F1 | Status |
|-------|-----|--------|
| 0 (Silent) | 0.738 | OK |
| 1 (Verse slow) | 0.577 | Marginal — 41/69 misclassified as Chorus fast |
| 2 (Verse mid) | **0.000** | Never predicted — no val data |
| 3 (Verse fast) | **0.000** | Never predicted — no val data |
| 4 (Chorus mid) | **0.000** | Never predicted — no val data |
| 5 (Chorus fast) | 0.602 | Marginal — 137/273 misclassified as Silent |
| 6 (Breakdown) | **0.000** | Never predicted — 51 examples in 243K |

**This is not a 7-class classifier.** It's a binary classifier (Silent vs Chorus fast) that occasionally guesses Verse slow, with four dead output classes.

### 5.4 Confusion Matrix Analysis

The model confuses Chorus fast → Silent for 137 of 273 GMD examples. This means the GMD's "fast chorus" proxy features look more like "Silent" than "Chorus fast" to the model. The proxy feature pipeline is producing indistinguishable features for musically different situations.

### 5.5 Normalization Statistics — Why They Broke

**Original GMD-only norm stats (before Lakh merge):**
```
bpm:             mean=111.86, std=25.79
rmsEnergy:       mean=0.672,  std=0.237
spectralCentroid: mean=9525.9, std=59.55   ← reasonable!
highFreqFlux:    mean=0.0,    std=0.0      ← GMD had no flux
state_float:     mean=0.999,  std=0.037
```

**After Lakh merge:**
```
bpm:             mean=119.60, std=28.32
rmsEnergy:       mean=0.720,  std=0.115
spectralCentroid: mean=11200,  std=0.000   ← COLLAPSED TO ZERO!
highFreqFlux:    mean=0.753,  std=0.215
state_float:     mean=1.629,  std=0.483
```

What happened: The Lakh centroid proxy formula (`8750 + (mean_note/127)×2450`) produces values tightly clustered in [8750, 11200]. When 112× more Lakh rows than GMD rows are merged, the centroid distribution collapses to a spike at ~11200 with near-zero variance. The `np.maximum(std, 1e-8)` clamp in `merge_datasets.py:128` then sets `std=1e-8`.

**The ONNX model therefore divides every centroid value by 1e-8**, producing values in the 1e11–1e12 range, which saturates every ReLU activation and produces effectively random output.

### 5.6 The No-Real-Audio Problem

This is the fundamental flaw: **the entire training pipeline operates on MIDI-derived proxy features.** At no point does the model see features computed from actual guitar audio through the C++ signal chain (`OnsetDetector`, `EnergyAnalyser`, `StructureTagger`).

The feature capture system (`FeatureCapture.cpp`) records real audio features to JSONL — and 13,151 captured frames exist — but they are only used for gap analysis, not training.

**What MIDI proxies miss that real audio captures:**

| Phenomenon | MIDI Proxy | Real Guitar Audio |
|-----------|-----------|-------------------|
| Pick attack variation | Clean velocity | Spectral transient + noise burst |
| Palm muting | Lower velocity | Dramatic spectral reshaping (reduced harmonics) |
| Feedback/sustain | Note duration | Continuous harmonic spectrum, no onsets |
| String noise | Absent | Present in HF bands → corrupts flux |
| Distortion harmonics | Absent | Fills entire spectrum → centroid saturates |
| Dynamic playing | Velocity steps | Continuous RMS variation |
| Tempo fluctuation | Perfect BPM metadata | Actually detected (and often wrong) BPM |
| Silence between notes | Note-off events | Reverb tail, feedback decay, amp noise |

### 5.7 Lakh Dataset Quality Concerns

**Zero Silent examples (class 0):** The Lakh dataset consists of matched MIDI files from the Lakh MIDI Dataset v0.1. These are pop/rock/jazz MIDI transcriptions, not metal performances. They contain no intentional silence sections — every file has continuous note activity. The 747 Silent examples in training come exclusively from 2,151 GMD examples.

**The "Breakdown" label is a synthetic heuristic:** It's applied when note density < 2.5 and BPM < 110. This has nothing to do with actual musical breakdowns. It catches slow/sparse MIDI files regardless of genre. Only 51 out of 241,101 Lakh examples meet this criteria — these are likely slow ballads or sparse intros, not metal breakdowns.

**The BPM metadata comes from file headers**, not from actual tempo detection. Many MIDI files have incorrect or default (120 BPM) tempo metadata. The code has no tempo-from-content detection.

### 5.8 Intensity Variants — Mechanical but Not Musical

Every source row is expanded 3× via `_INTENSITY_VARIANTS = (0.0, 0.5, 1.0)`, which shifts adjusted BPM by ±20. This means 243,252 training examples come from only ~81,000 unique musical moments. More critically, the labels are recomputed for each variant via the same rule oracle — so the model sees "same song, ±20 BPM → same pattern class" as a training signal. This encourages the model to ignore BPM variation.

### 5.9 Summary of Training Data Issues

| Issue | Severity | Impact |
|-------|----------|--------|
| No real audio in training | **Critical** | Model operates on wrong feature distribution |
| Centroid std = 0 → division by 1e-8 | **Critical** | ONNX produces random output on live audio |
| 59.5% of data = single class | **Critical** | Model biased toward Chorus mid |
| Val set missing 4 of 7 classes | **Critical** | Performance on missing classes is unknown |
| 51 Breakdown examples in 243K | **Critical** | Class is untrained, never predicted |
| Lakh = non-metal MIDI files | **High** | Features don't represent metal playing |
| Rule oracle labels = circular training | **High** | Model can't exceed rule baseline |
| BPM from file headers, not detection | **Medium** | Training BPM ≠ runtime BPM |
| Intensity variant expansion dilutes signal | **Medium** | 3× data duplication without information gain |
| GMD val + Lakh train = distribution mismatch | **Medium** | Validation not representative |

---

## 6. UX Improvement Recommendations

### 6.1 Immediate (High Impact, Low Implementation Cost)

| # | Recommendation | Rationale |
|---|---------------|-----------|
| 1 | **Half-Time toggle button** | Single most important sludge feature — forces all patterns into half-time feel regardless of BPM. Toggles snare from 2&4 to 3. |
| 2 | **Manual tempo tap** | When onset detection fails on drone sections (common in sludge), user taps quarter notes on a button or MIDI CC. |
| 3 | **Pattern lock** | User can override auto-selection and lock a pattern for a section. Sludge songs have long static sections. |
| 4 | **BPM display shows detected vs. played** | Show both raw detected BPM and the stabilized playback BPM so user understands when detection is confused. |
| 5 | **Reduce UI metrics** | Display only BPM, state (as descriptive word like "Heavy"/"Ambient"/"Silent"), and pattern name. Hide RMS/centroid/flux/confidence behind an "advanced" disclosure. |

### 6.2 Short-Term (Moderate Implementation)

| # | Recommendation |
|---|---------------|
| 6 | **Swing/groove knob (0–100%)** | Controls triplet/swing feel for 8th notes. Critical for sludge's "loping" groove feel. |
| 7 | **Drum intensity independent of BPM offset** | Currently `policyIntensity` affects BPM (±20) AND is reported as feature. Split into: (a) drum complexity, (b) BPM offset, (c) bass activity. |
| 8 | **Section markers** | User can mark "this is a verse / this is a chorus / this is a breakdown" via UI or MIDI to train/guide the classifier. |
| 9 | **Calibration mode** | 30-second "play your typical heavy riff" then "play your soft parts" — plugin learns the user's personal RMS/centroid ranges. |
| 10 | **Transition detection sensitivity** | Knob controlling how aggressively the plugin responds to energy changes (from "gradual" to "immediate"). |

### 6.3 Long-Term (Architectural)

| # | Recommendation |
|---|---------------|
| 11 | **Recording/playback analysis** | Record a session, let user annotate sections offline, retrain the ONNX model on their annotated data (personalized model). |
| 12 | **MIDI-learnable controls** | All knobs/buttons MIDI-learnable for foot controller operation during performance. |
| 13 | **Preset system** | "Sludge," "Doom," "Thrash," "Death" presets that reconfigure tempo ranges, pattern libraries, and state thresholds. |

---

## 7. Concrete Fix Priority

### Tier 1 — Must Fix (Plugin Unusable for Sludge Without These)

| # | Issue | File(s) | Fix |
|---|-------|---------|-----|
| F1 | **BPM floor at 80** | `OnsetDetector.h:109` | Change `kMinBpm = 40.0f` |
| F2 | **ONNX normalization mismatch** | `training/build_dataset.py`, `assets/norm_stats.json` | Recompute norm_stats from **real guitar captures**, not MIDI proxies. Alternatively: add runtime feature normalization with capture-derived stats, or remove baked normalization from ONNX graph entirely (normalize in C++ before inference). |
| F3 | **Spectral centroid std=1e-8** | `pattern_model.py:52` (`torch.clamp(std, min=1e-8)`) | The centroid std from proxy data is near-zero, causing division explosion. Either: (a) disable normalization for centroid, (b) use a much larger epsilon, or (c) use capture-derived std (~3666). |
| F4 | **Add half-time LOUD pattern** | `MidiPatternLibrary.cpp`, `pattern_rules.h` | Make half-time (index 7) available in LOUD state and add dedicated sludge half-time patterns. |
| F5 | **Lower BPM range in all components** | `PatternPlayer.h`, `TempoStabiliser.h`, `pattern_rules.h` | Propagate 40 BPM floor through all BPM-clamping code paths (currently scattered across `jlimit(40,320)`, `jlimit(80,220)`, etc.). |

### Tier 2 — High Impact

| # | Issue | Fix |
|---|-------|-----|
| F6 | **Add AMBIENT/DRONE structural state** | Expand `StructureState` enum: `SILENT, AMBIENT, SOFT, LOUD, BREAKDOWN`. Add pattern mappings. |
| F7 | **Sub-bass energy feature** | Add 40–120 Hz band energy to `EnergyAnalyser` and `FeatureVector`. Use for heaviness classification. |
| F8 | **Increase hold times 4–8×** | Multiply all `StructureTagger` hold times by 4–8×, make tempo deadband 8 BPM. |
| F9 | **Swing parameter** | Add to `PatternPlayer` — offset 8th-note events by swing ratio. |
| F10 | **UI: Half-time toggle + BPM display cleanup** | Add to `AccompanimentEditor`. |

### Tier 3 — Quality of Life

| # | Issue | Fix |
|---|-------|-----|
| F11 | **Pattern lock UI** | User can lock/unlock a pattern index. |
| F12 | **Manual tap tempo** | Accumulate tap intervals, set seed BPM. |
| F13 | **Calibration mode** | 30s recording → personal RMS/centroid ranges. |
| F14 | **Preset system** | JSON config files for genre-specific parameter sets. |

---

## 8. Architectural Recommendations

### 8.1 Normalization Strategy

The fundamental problem: **ONNX models are trained on proxy features but infer on audio features.** Two possible solutions:

**Option A — Runtime normalization (cheaper, immediate):**
- Remove baked normalization from ONNX graphs
- Add `computeNormalizationStats()` to C++ that accumulates live features during a calibration phase
- Normalize in C++ before calling `Ort::Session::Run()`
- Requires `PatternOnnxExport` to export without normalization layer

**Option B — Retrain on captures (better, more work):**
- Collect 30–60 minutes of real guitar playing with human-annotated ground truth patterns
- Train new models directly on audio feature captures
- Use human annotations for labels, not rule oracle
- This breaks the circularity of rule-trained models

### 8.2 Architecture for Sludge-Specific States

Current:
```
Silent → Soft → Loud
```

Recommended sludge-aware state machine:
```
Silent ──→ Ambient ──→ Building ──→ Heavy ──→ Breakdown
  │           │            │           │           │
  └───────────┴────────────┴───────────┴───────────┘
             (can transition to any via hold timer)
```

Each state has its own pattern pool (3–5 patterns) with half-time variants.

### 8.3 Bass Strategy for Sludge

In sludge, bass is NOT an independent voice — it reinforces the guitar riff, typically:
- Unison with guitar root (possibly octave down)
- Sustained notes following guitar chord changes
- Occasional fills during transitions

The current 16-step piano-roll generative bass model is architecturally wrong for sludge. Consider:
- **Mode: Follow** — bass tracks `pitchRootMidi`, plays sustained whole/half notes at guitar root
- **Mode: Octave** — same as Follow but one octave down
- **Mode: Generative** — current ONNX behavior (for genres where it makes sense)

---

## 9. Summary

The Metal Accompaniment plugin has a solid engineering foundation but was designed around thrash/death metal assumptions that fundamentally don't translate to sludge metal. The three critical issues are:

1. **BPM range** (80–220 excludes sludge's 40–80 BPM core)
2. **ONNX normalization** (trained on MIDI proxies, catastrophically wrong on live audio)
3. **Pattern library** (2 of 11 patterns are sludge-appropriate)

The ML pipeline's circular training (rule oracle labels) means the ONNX path currently provides no benefit over the rule-based fallback while adding the normalization mismatch problem. Fixing normalization and breaking the training circularity are prerequisites for the ONNX path to add value.

The recommended immediate fixes (Tier 1) are all local, low-risk changes that would make the plugin at least _playable_ for sludge. The architectural changes (expanded states, renormalized models, pattern library expansion) are medium-term investments.

---

*Audit performed by domain analysis + full codebase review of 33 source files, 12 documents, and 4 training artifacts.*
