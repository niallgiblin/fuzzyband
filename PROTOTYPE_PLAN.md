# Prototype Plan — Fuzzyband v0.7.x

**Goal:** A plugin that meaningfully accompanies sludge metal guitar playing, with clearly demonstrable live results. ONNX builds only.

---

## Part 1 — C++ Plugin Fixes

All fixes are in existing files. Estimated total: ~2–3 hours of work.

### Fix 1 (5 min) — Gate style-pool override on classifier being active

**File:** `src/AccompanimentProcessor.cpp` ~line 401

**Problem:** `stylePatternPool(0)` (palm mute pool) is applied unconditionally with `count > 0`, permanently locking drums to patterns `{1, 2, 7}` regardless of playing intensity. LOUD and BREAKDOWN patterns are never selected.

**Change:**
```cpp
// BEFORE (~line 401):
auto stylePool = PatternRules::stylePatternPool(playingStyle);
if (stylePool.count > 0)
    sectionDrumIdx = stylePool.indices[globalBar % stylePool.count];

// AFTER:
const bool styleClassifierActive = (styleClassifier != nullptr)
    && styleClassifier->isLoaded()
    && (styleClassifyCount.load(std::memory_order_relaxed) > 0);
auto stylePool = PatternRules::stylePatternPool(playingStyle);
if (stylePool.count > 0 && styleClassifierActive)
    sectionDrumIdx = stylePool.indices[globalBar % stylePool.count];
```

---

### Fix 2 (5 min) — Reduce StructureTagger hold times

**File:** `src/analysis/StructureTagger.h` lines 63–74

Halve or quarter the hold times from their current "sludge ×8" values. The goal is responsiveness — if you want slow-burn, it can still be restored per-preset.

```cpp
// REPLACE the hold time constants block with:
static constexpr double kHoldSilentSec             = 0.0;
static constexpr double kHoldAmbientToSoftSec      = 0.5;   // was 6.0
static constexpr double kHoldAmbientToSilentSec    = 1.5;   // was 8.0
static constexpr double kHoldSoftToLoudSec         = 0.4;   // was 1.6
static constexpr double kHoldSoftToAmbientSec      = 1.5;   // was 6.0
static constexpr double kHoldSoftToSilentSec       = 2.0;   // was 6.0
static constexpr double kHoldLoudToBreakdownSec    = 0.4;   // unchanged
static constexpr double kHoldLoudToSoftSec         = 2.0;   // was 6.0
static constexpr double kHoldLoudToSilentSec       = 3.0;   // was 8.0
static constexpr double kHoldBreakdownToLoudSec    = 2.0;   // was 8.0
static constexpr double kHoldBreakdownToSoftSec    = 3.0;   // was 8.0
static constexpr double kHoldBreakdownToSilentSec  = 3.0;   // was 8.0
```

---

### Fix 3 (5 min) — Reduce inference hold guards

**File:** `src/AccompanimentProcessor.cpp`

Two changes: the auto-change cadence (currently every 8 bars = 16 seconds at 120 BPM) and the structure commit minimum hold (currently 2 bars = 4 seconds).

```cpp
// ~line 378 — auto-change: was barsSinceChange >= 8 (every 8 bars)
const bool autoChangeReady = (barsSinceChange >= 4);

// ~line 327 — structure commit: was barsSinceStructChange >= 2
canCommit = (barsSinceStructChange >= 1);
```

---

### Fix 4 (5 min) — Bass section fallback when Play is off

**File:** `src/AccompanimentProcessor.cpp` ~line 797

When Play is off, the sequencer stays at section "INTRO" (1 note/bar, vel 65). Override with "VERSE" for free-play mode.

```cpp
// BEFORE (~line 797):
auto bassCommit = RuleBasedBass::generate(
    lastBassPitch,
    structureSequencer.getCurrentSectionName(),
    structureSequencer.isFirstBar(),
    bpmForPlayer);

// AFTER:
const char* bassSection = playActive.load(std::memory_order_acquire)
    ? structureSequencer.getCurrentSectionName()
    : "VERSE";
auto bassCommit = RuleBasedBass::generate(
    lastBassPitch,
    bassSection,
    structureSequencer.isFirstBar(),
    bpmForPlayer);
```

---

### Fix 5 (10 min) — Move lastBassPitch to member variable

**File:** `src/AccompanimentProcessor.h` and `src/AccompanimentProcessor.cpp`

The `static float lastBassPitch` local variable never resets on `prepareToPlay()`.

In `AccompanimentProcessor.h`, private section, add:
```cpp
float lastBassPitch = 36.0f;   // C2 — drop C tuning; resets on prepareToPlay
```

In `prepareToPlay()` in `AccompanimentProcessor.cpp`, add:
```cpp
lastBassPitch = 36.0f;
```

In `processBlock()`, remove the `static float lastBassPitch = 36.0f;` line — the member variable takes its place.

---

### Fix 6 (10 min) — Lower pitch confidence threshold

**File:** `src/AccompanimentProcessor.cpp` ~line 794

The current threshold of 0.15 is too conservative for sludge — distorted power chords have unpredictable fundamental pitch confidence. Lowering to 0.08 lets the bass update more often.

```cpp
// BEFORE:
if (rawConf > 0.15f && !digitalSilence)
    lastBassPitch = rawMidi;

// AFTER:
if (rawConf > 0.08f && !digitalSilence)
    lastBassPitch = rawMidi;
```

---

### Fix 7 (45 min) — Wire BeatTracker + blend with manual BPM

**File:** `src/AccompanimentProcessor.cpp`

This is the most impactful single change. Without it, drums play at a fixed 120 BPM forever.

**Step A:** In `processBlock()`, after `energyAnalyser.process(in, numSamples)`, add:
```cpp
beatTracker.pushFluxSample(energyAnalyser.getHighFreqFlux(), hostSampleTime);
```

**Step B:** Replace the static `bpmForPlayer` read with a blended value:
```cpp
// BEFORE:
float bpmForPlayer = 120.0f;
if (auto* rawBpm = apvts.getRawParameterValue("bpm"))
    bpmForPlayer = rawBpm->load();

// AFTER:
float bpmForPlayer = 120.0f;
if (auto* rawBpm = apvts.getRawParameterValue("bpm"))
    bpmForPlayer = rawBpm->load();

// Blend detected BPM when tracker is confident
{
    const float trackedBpm  = beatTracker.getBpm();
    const float trackedConf = beatTracker.getConfidence();
    if (trackedConf > 0.55f && trackedBpm > 40.0f && trackedBpm < 300.0f)
        bpmForPlayer = bpmForPlayer * (1.0f - trackedConf) + trackedBpm * trackedConf;
}
```

**Step C:** Update the `PlaybackGate` call to use real lock state (remove hardcoded `true`):
```cpp
// BEFORE:
const GateDecision gd = playbackGate.update(
    st,
    beatTracker.getBeatPhase01(),
    beatTracker.isLocked(),
    true,  // always tempo-locked — user sets BPM manually (D005)
    ...

// AFTER:
const GateDecision gd = playbackGate.update(
    st,
    beatTracker.getBeatPhase01(),
    beatTracker.isLocked(),
    beatTracker.isLocked(),  // now driven by actual tracker state
    ...
```

**Step D:** Update `displayBpm` to show the blended value so the UI reflects the tracked tempo:
The `displayBpm.store(bpmForPlayer, ...)` at the bottom of `processBlock` already stores the blended value, so this is automatic.

**Note on accuracy:** The BeatTracker uses the `highFreqFlux` as its OSF (onset strength function). This was designed for drums, not guitar. For sludge metal with pick attacks and palm mutes it should work reasonably well. The EMA smoothing (`smoothedBpm += 0.15f * (bestBpm - smoothedBpm)`) means it takes ~7 hops to fully lock (~6.7 s at 44100/512 hop). Once locked, it's stable. The blend factor means the manual BPM slider still acts as a strong prior when confidence is low.

---

### Fix 8 (30 min) — Tune stylePatternPool for sludge metal

**File:** `src/inference/pattern_rules.h`

The current mappings send you to busy, "standard metal" patterns. Adjust for sludge pacing:

```cpp
inline SectionPatternPool stylePatternPool(int styleIndex) noexcept
{
    using P = SectionPatternPool;
    switch (styleIndex)
    {
        // Palm mute chugs: half-time first, then verse groove, then sparse breakdown
        case 0: return P{ 3, { 7, 1, 9 } };

        // Open chord: heavy chorus, breakdown on sustain
        case 1: return P{ 3, { 4, 6, 14 } };

        // Single note runs: verse fast, thrash when BPM is high
        case 2: return P{ 3, { 3, 10, 2 } };

        // Sustain/drone/feedback: breakdown, sparse, ambient-half-time
        case 3: return P{ 3, { 6, 9, 7 } };

        // Silence
        case 4: return P{ 1, { 0 } };

        default: return P{ 0, {} };
    }
}
```

---

### Build and verify

After all fixes, bump version in `CMakeLists.txt` line 4 (`0.7.11` → `0.7.12`) then rebuild:

```bash
export ONNXRUNTIME_ROOT="/Users/ng/Downloads/onnxruntime-osx-arm64-1.24.4"
cmake -B build-onnx -DCMAKE_BUILD_TYPE=Release \
  -DMA_BUILD_STANDALONE=ON \
  -DMA_ENABLE_ONNX=ON \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"
cmake --build build-onnx --config Release --parallel
```

**Diagnostic checklist after build:**
- Open plugin in Reaper, watch RMS readout — confirm it reads > 0.025 while playing
- Watch State label — should change from SILENT within 1 second of strumming
- Watch Pattern — should be something other than 0 or 7 when playing loud
- Confirm drums are audible on MIDI output channel 10
- Confirm bass is audible on channel 2, tracking roughly the root note you're playing

---

## Part 2 — Training Pipeline Analysis & Improvements

### Current state assessment

| Component | Status | Risk for sludge metal |
|---|---|---|
| **Style classifier** (CNN, 155k params) | **Trained on wrong signal** (driven tone); receives clean DI at runtime | **CRITICAL** — mel spectrograms don't match; classifier is running on a fundamentally different signal than it was trained on |
| **Pattern model** (7-feature MLP) | Trained on GMD + Lakh MIDI, macro-F1 0.64 | **MEDIUM** — is a distillation of the rule oracle, adds noise but no new capability |
| **Bass model** (small MLP) | Trained on synthetic data | **LOW** — bass comes from RuleBasedBass in practice (generative bass off by default) |
| **Data collection** | 5 recordings, one per class, near-breakup amp sim | **CRITICAL** — wrong signal chain; need re-recording with clean DI |

---

### Issue A — Style classifier trained on wrong signal (clean DI vs near-breakup amp) ✅ RE-RECORDED

**This is the most important ML fix.**

The plugin sits *before* the amp/cab sim and fuzz in the FX chain. It receives **clean guitar DI**. The training recordings were made with an amp sim plugin at near-breakup — a significantly different signal.

**What the classifier was trained on:**
- Soft pick transients (amp sim compression rounds off the attack)
- Extra even harmonics from amp saturation
- Coloured tonal character from the cab sim
- Potential high-frequency rolloff (cab sim removes fizz above ~5 kHz)

**What the classifier receives at runtime:**
- Sharp, uncoloured pick transients — the attack is the dominant timbral event
- Bright, clean harmonics with no cab rolloff (guitar pickups extend to 10+ kHz)
- No compression — dynamic range is much wider
- Technique differences are **more distinct** on clean DI, not less

This also invalidates the clean/breakup boundaries between classes: on a near-breakup signal, a palm mute and a hard-strummed open chord can look similar (both saturate the amp). On a clean DI, they're spectrally very different — the palm mute has strong low-frequency thump and tight decay; the open chord has a bright harmonic spread and longer sustain. **Clean DI is actually easier to classify correctly once retrained on it.**

For sludge metal with clean DI, the two hardest pairs to separate will be:
- **palm_mute vs single_note** at high speed (both have similar attack density)
- **open_chord vs sustain** on slow, ringing power chords

**Fix: Re-record everything as clean DI — matching the plugin's actual input**

The plugin is inserted on the guitar track *before* any FX. Record training audio the same way: guitar → audio interface → DAW track with Fuzzyband inserted, no amp sim, no drive after it. Export each take as a dry WAV from the DAW at 44.1 kHz.

```bash
# Signal chain for recording training data (must match plugin position):
# Guitar → Audio Interface → DAW track → (export dry WAV)
# NO amp sim, NO fuzz/distortion, NO cab sim
# This is what the plugin actually sees at runtime

# Record into data/raw/<class>/:
# palm_mute/take1.wav, take2.wav, take3.wav  — chunky muted riffs
# open_chord/take1.wav, take2.wav, take3.wav — power chord washes
# single_note/take1.wav, take2.wav, take3.wav — single-note lines
# sustain/take1.wav, take2.wav, take3.wav    — held notes, drones, feedback
# silence/ — generated automatically

# Aim for: 3 takes × 2-3 minutes per class
# Vary the technique within each take (different positions, BPMs, dynamics)
```

Then rebuild the dataset with recording-level splits:

```bash
# The current build script uses StratifiedShuffleSplit on windows — need to change
# to split by filename first, then extract windows.
# Quick fix: at minimum, have ≥2 WAV files per class and put the last file in val.
```

After recording, retrain:
```bash
source training/.venv/bin/activate
python3 training/scripts/build_mel_dataset.py  # rebuild X.npy / y.npy
python3 training/scripts/train_classifier.py   # retrain CNN
python3 scripts/export_classifier_onnx.py      # export to assets/classifier.onnx
```

---

### Issue B — Pattern model is a noisy rule-oracle copy

The pattern model in `assets/accompaniment_model.onnx` was trained with labels assigned by a Python port of `PatternRules::rulePatternForState`. It's learned to reproduce the rule function with macro-F1 0.64 — meaning it's noisier than just running the rule directly, with no new musical capability.

The domain-gap analysis (FEATURE_PROXY.md) also shows `bpm` is "risky" because the GMD+Lakh proxy corpus has many low-BPM/silent rows that don't match a real sludge guitar session.

**Implication:** The ONNX pattern model is actively *worse* than the rule-based path. For a working prototype, you could bypass `OnnxInference` entirely and route to `RuleBasedInference` + the diversification layer. The patterns that matter for sludge are already in `diversifyPattern()` — half-time (7), blast (8), sparse breakdown (9), thrash (10).

**Fix options for pattern model:**

**Option A (quick) — Disable OnnxInference, keep everything else**

In `src/AccompanimentProcessor.cpp`, change `makeInference()` to always return rule-based:

```cpp
std::unique_ptr<IInference> makeInference()
{
    // Pattern ONNX model is a noisy oracle copy — rule path is cleaner
    return std::make_unique<RuleBasedInference>();
}
```

This doesn't change style classification or bass — only the pattern model. Combined with Fix 1 (style pool gate) and Fix 8 (sludge pattern pools), the rule path + style classifier should give better results.

**Option B (longer) — Collect real guitar capture labels, retrain**

Use the feature capture system to record a real playing session, annotate it manually (which sections were LOUD/SOFT/etc.), then retrain the pattern model on captured features rather than MIDI proxies. This closes the domain gap completely.

Steps:
1. Enable feature capture in the plugin UI (capture button)
2. Play a 15-minute sludge session
3. Annotate the JSONL with time-range labels using the CSV format from `training/evaluate_feature_capture.py`
4. Run `evaluate_feature_capture.py` with your annotations
5. Use the captured X vectors as training data for a new pattern model
6. `python3 training/train_gmd.py --data-dir path/to/captured-tensors`

This gives you a model trained on your actual guitar signal — no domain gap.

---

### Issue C — Bass model is synthetic and inactive

The `assets/bass_model.onnx` is trained on synthetic pitch distributions around E2/A2/B1. The `OnnxBassInference` feeds the current pitch root to this model to generate 16-step bass lines.

However:
- Default `generativeBassMode = "Off"` means `RuleBasedBass` always runs instead
- `RuleBasedBass` generates 1-4 root notes per bar (depending on section), shifted by pitch tracking
- For sludge metal, this sparse root-note bass is actually appropriate

**Recommended approach:** Keep `generativeBassMode = "Off"` (default) and focus on making the pitch tracking work correctly (Fixes 5 and 6). The rule-based bass that plays 2 notes per bar on the root + octave is appropriate for sludge. The ONNX bass model can be a stretch goal.

**If you want to improve generative bass:**
- Change the default `generativeBassMode` from `2` (Off) to `0` (Auto) in `createParameterLayout()`
- Record yourself playing bass lines for each section type
- Retrain `BassNet` on real bass MIDI rather than synthetic pitch distributions

---

### Issue D — Training data class imbalance and augmentation

Looking at the EVALUATION.md: `sustain` class has 337 test windows vs 105-109 for the others. This suggests the sustain recording is longer (or was augmented more). The `SUSTAIN_AUG_FACTOR = 2` in `build_mel_dataset.py` confirms sustain gets 2× augmentation.

For sludge metal, sustain and palm mute are the dominant playing styles. The current data likely has:
- More sustain (augmented) — good, sludge players sustain a lot
- Single sustain recording — bad, will overfit to one tone

Also: the augmentation in `train_classifier.py` is minimal (Gaussian noise ±0.01, gain ±6dB). For guitar-specific robustness, add:
- **Time masking** (randomly zero 1-4 time frames) — simulates pickup drop-outs
- **Frequency masking** (randomly zero 2-4 mel bands) — simulates EQ variation
- **More gain range** (±12dB) — sludge covers a wide dynamic range
- **Spectral shift augmentation** — mimics pickup position / tone knob variation

---

### Issue E — No sludge-specific label vocabulary

The current style labels (palm_mute, open_chord, single_note, sustain, silence) are guitar-centric generic labels. For sludge metal the meaningful distinctions are:

| What you play | Expected drum response |
|---|---|
| Single-note riff (clean-ish, sparse) | Half-time groove (7), sometimes verse groove (1) |
| Chunky muted chug (fast 16ths) | Thrash (10) or blast (8) |
| Slow muted chug (quarter notes) | Heavy half-time (7), breakdown (9) |
| Open power chord wash | Heavy groove (4) or breakdown (6) |
| Feedback / drone | Sparse breakdown (9), ambient ride |
| Silence / breath | Silence (0) |

Consider renaming or remapping classes in `stylePatternPool` to better match what you actually play, rather than generic playing-technique labels. The five-class CNN can be retrained with sludge-specific labeling — the architecture doesn't care what the classes mean.

---

## Summary Execution Order

### Day 1 — Fix the plugin (all C++ changes)

| Step | Fix | Time |
|---|---|---|
| 1 | Fix 1: style pool gate | 5 min |
| 2 | Fix 2: StructureTagger hold times | 5 min |
| 3 | Fix 3: inference hold guards | 5 min |
| 4 | Fix 4: bass section fallback | 5 min |
| 5 | Fix 5: lastBassPitch → member | 10 min |
| 6 | Fix 6: pitch confidence threshold | 2 min |
| 7 | Fix 7: wire BeatTracker | 45 min |
| 8 | Fix 8: sludge pattern pools | 15 min |
| 9 | Bump version, build, test | 30 min |

**After Day 1:** Drums should respond within 1 second of playing, follow the right intensity section, and begin tracking tempo.

### Day 2 — Re-record clean DI & retrain style classifier

**Critical setup:** Record with the plugin in the chain, amp sim *after* it — or simply record dry guitar with no amp processing at all. The plugin receives the pre-amp signal; the training data must match.

| Step | Task | Time | Status |
|---|---|---|---|
| 1 | Set up a Reaper session: guitar track → audio interface → record dry (no amp/fuzz) | 5 min | ✅ Done |
| 2 | Record 3 takes × 2–3 min for each class: palm_mute, open_chord, single_note, sustain | 1–1.5 hours | ✅ Done |
| 3 | Export each take as a 44.1 kHz mono WAV into `data/raw/<class>/take1.wav` etc. | 15 min | ✅ Done |
| 4 | Delete the old training data: `rm data/raw/*/palm_mute.wav` etc. | 1 min | ✅ Done |
| 5 | Run `python3 training/scripts/build_mel_dataset.py` | 5 min | ⬜ **Next** |
| 6 | Run `python3 training/scripts/train_classifier.py` | 15–30 min | ⬜ |
| 7 | Run `python3 scripts/export_classifier_onnx.py` | 5 min | ⬜ |
| 8 | Copy `assets/classifier.onnx`, bump version, rebuild | 20 min | ⬜ |
| 9 | Live test — check Pattern readout in plugin UI responds to technique changes | ongoing | ⬜ |

**After Day 2:** The style classifier was trained on the same clean DI signal it receives at runtime. Pattern changes should feel musically responsive to changes in technique.

### Day 3 — Close domain gap for pattern model (optional)

| Step | Task | Time |
|---|---|---|
| 1 | Play a 15-min session with feature capture enabled | 15 min |
| 2 | Annotate the capture JSONL with playing intensity labels | 1 hour |
| 3 | Retrain pattern model on captured data | 30 min |
| 4 | Promote to `assets/`, rebuild | 20 min |

**After Day 3:** Pattern model uses your guitar's actual spectral features as input — no MIDI proxy domain gap.

---

## What "demonstrable" looks like after these fixes

A 3-minute demo showing:

1. **Start playing a slow palm-muted riff** → drums enter within 2 seconds with half-time groove, bass follows root note
2. **Hit a big open chord wash** → drums switch to heavier pattern (chorus/breakdown) within a bar
3. **Go into a fast single-note run** → thrash or blast beat kicks in
4. **Drop back to silence** → drums fade within 3 seconds
5. **Re-enter with a breakdown chug** → breakdown pattern fires

This is achievable with Day 1 C++ fixes + the existing ONNX models (even before retraining).
