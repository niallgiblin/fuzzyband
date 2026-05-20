# Fuzzyband Plugin Guide

A practical reference for understanding, tuning, and extending the plugin as a daily songwriting tool.

---

## What the plugin does (in plain English)

You play guitar → the plugin listens → it figures out tempo and how hard you're playing → it picks a drum/bass pattern → it emits MIDI events to your DAW on channels 10 (drums) and 2 (bass).

The plugin never records audio. It only reads the guitar signal to extract features, then discards it. The output is pure MIDI.

---

## The signal chain

```
Guitar audio
  │
  ├─ OnsetDetector       → IOI-based BPM estimate (spectral flux peaks)
  │   └─ feeds flux → BeatTracker  → autocorrelation BPM + confidence + lock
  │
  ├─ EnergyAnalyser      → RMS, spectral centroid, high-freq flux
  │
  └─ StructureTagger     → SILENT / SOFT / LOUD  (with hysteresis hold)
       │
       └─ PitchEstimator → guitar root MIDI note (for bass transposition)

All of the above → FeatureVector pushed to lock-free queue
                          │
                 (background thread, ~50 Hz)
                          │
                  RuleBasedInference  → pattern index (0–6)
                  StructureInference  → smoother state blending (optional)
                          │
                    latestPatternIndex (atomic int)
                          │
                  PatternPlayer  (audio thread)  → MIDI out
```

---

## File map — the things you'll actually touch

| File | What it does | When to touch it |
|---|---|---|
| `src/midi/MidiPatternLibrary.cpp` | All 7 drum/bass patterns defined as beat-offset events | Add/change patterns |
| `src/inference/RuleBasedInference.cpp` | Maps FeatureVector → pattern index | Change which pattern fires when |
| `src/analysis/StructureTagger.cpp` | Thresholds for SILENT/SOFT/LOUD transitions | Tune sensitivity to your playing dynamics |
| `src/analysis/StructureTagger.h` | `kSilentRms`, `kLoudRms`, hold times | Same |
| `src/analysis/BeatTracker.cpp` | Autocorrelation beat tracker | Tune tempo lock speed |
| `src/midi/PatternPlayer.cpp` | Emits MIDI events, tracks beat position, humanisation | Fix timing bugs, change humanisation amounts |
| `src/AccompanimentProcessor.cpp` | Wires everything together, manages playback gate | Core logic for when MIDI starts/stops |

---

## Two modes: ONNX (primary) vs rule-based (fallback)

The plugin ships **three trained ML models** in `assets/`:

| Model file | Tensor contract | What it does |
|---|---|---|
| `assets/accompaniment_model.onnx` | `X [1,7] → Y int64 [1]` | `PatternNet` — selects drum pattern index 0–6. Trained on Google Magenta GMD + Lakh MIDI datasets. |
| `assets/bass_model.onnx` | `X_bass [1,7] → Y_bass float [1,32]` | `BassNet` — generates a 16-step piano-roll (pitch offset + velocity per sixteenth note, relative to detected guitar root). |
| `assets/structure_model.onnx` | `X_struct [1,12,7] → Y_struct float [1,3]` | Structure classifier from 12-frame history of audio features. More nuanced than simple RMS threshold. |

These models are only active when the plugin is built with `-DMA_ENABLE_ONNX=ON` and `ONNXRUNTIME_ROOT` points at an ONNX Runtime installation. **The default CMake build has `MA_ENABLE_ONNX=OFF`**, so unless you explicitly built with ONNX enabled, you are running the rule-based fallback.

To check which mode is active at runtime, look at `activeInferenceName` in the processor — the editor logs this value.

### The rule-based fallback

When ONNX is disabled or model load fails, the plugin falls back to:

- `RuleBasedInference` for pattern selection (simple switch on state + BPM)
- `RuleStructureInference` for structure classification (same RMS thresholds as `StructureTagger`)
- **No generative bass at all** — the bass falls through to the static library patterns

`RuleBasedInference::selectPattern()` at `src/inference/RuleBasedInference.cpp:9`:

```
SILENT          → index 0 (silent)
SOFT + BPM<120  → index 1 (Verse slow)
SOFT + BPM<160  → index 2 (Verse mid)
SOFT + BPM≥160  → index 3 (Verse fast)
LOUD + BPM<160  → index 4 (Chorus mid)
LOUD + BPM≥160  → index 5 (Chorus fast)
```

BPM thresholds shift with the Intensity knob: `bpmAdj = bpm + (intensity - 0.5) * 40`.

**Index 6 (Breakdown) is never selected by the rule-based path.** It's only reachable by clicking Next Pattern. With the trained ONNX model, all 7 indices are selectable.

## The 7 drum patterns (the MIDI event data the ONNX output indexes into)

```
Index 0 — Silent           (no events)
Index 1 — Verse Slow       (kick-snare-hat, 8ths)
Index 2 — Verse Mid        (kick-snare-hat, 8ths, bass every 2 beats)
Index 3 — Verse Fast       (16th-hat pattern)
Index 4 — Chorus Mid       (open hat, ride)
Index 5 — Chorus Fast      (double-kick pattern)
Index 6 — Breakdown        (2-bar sparse groove)
```

These are defined in `src/midi/MidiPatternLibrary.cpp`. The ONNX model outputs an index; the PatternPlayer looks up the events from this library and emits MIDI. When ONNX bass is active, the drum events still come from here but the bass events are overridden by the ONNX piano-roll output.

---

## The playback gate — how the drums decide when to enter

The old gate logic started playback as soon as confidence crossed a threshold. That made the internal beat clock reset immediately, so the first kick could land wherever the guitarist happened to be in the phrase. That behavior has been replaced.

The current implementation is split into two pieces:

- `PlaybackGate` (`src/analysis/PlaybackGate.*`) owns the state machine for silence, phrase-breath holds, confidence gating, deferred beat snaps, and reset decisions.
- `AccompanimentProcessor::processBlock()` applies the returned `GateDecision`: it can arm a crash, reset trackers, call `patternPlayer.snapBpm()`, call `patternPlayer.snapToBarStart()`, and mute/unmute `PatternPlayer`.

The important current call site looks like this:

```cpp
const GateDecision gd = playbackGate.update(
    st,
    beatTracker.getBeatPhase01(),
    beatTracker.isLocked(),
    onsetDetector.isTempoLocked(),
    beatTracker.getConfidence(),
    numSamples,
    sr);

if (gd.snapBeatNow)
{
    patternPlayer.snapBpm(bpmForPlayer);
    patternPlayer.snapToBarStart();
}

patternPlayer.setStructureSilent(!gd.gateOpen && !previewAudible);
```

The result is that the player stays muted while the gate is waiting for a musically useful entry point. When the gate opens, the `PatternPlayer` clock is snapped to the bar start and the BPM is set directly for the first block, avoiding the initial EMA drift.

### What `BeatTracker::getBeatPhase01()` actually is

`BeatTracker::getBeatPhase01()` returns a value in `[0, 1)` computed as:
```cpp
beatPhase01 = std::fmod(static_cast<double>(hopCounter) / periodHops, 1.0);
```

`hopCounter` increments every FFT hop since the plugin started. This is a **free-running oscillator** — phase 0.0 means "a multiple of one beat period has elapsed since plugin startup," not "we are at beat 1 of the guitarist's phrase." It has no anchor to the musical downbeat.

However, because the autocorrelation peaks at the onset period, phase 0 is statistically correlated with when strong beats occur in the audio. When `beatPhase01` wraps from ~1.0 back to ~0.0, that crossing corresponds to a beat boundary in the BeatTracker's estimation.

### Historical note

Older notes in this guide described `pendingBeatSnap` fields living directly in `AccompanimentProcessor`. That design was superseded by the `PlaybackGate` module so the state machine can be tested independently (`tests/test_playback_gate.cpp`) and the processor can stay closer to a real-time coordinator.

---

## Tempo tracking — BPM snap on entry

### Current behavior

Two parallel BPM sources exist:

1. **OnsetDetector** (`src/analysis/OnsetDetector.cpp`): Measures time between detected note onsets (IOIs). Quantizes to nearest 5 BPM. Fast but noisy with fuzz/distortion.

2. **BeatTracker** (`src/analysis/BeatTracker.cpp`): Autocorrelation of spectral flux. Evaluates all integer BPMs 80–220 every hop. Slower to lock but more stable. Has `.getConfidence()` and `.isLocked()`.

The processor reconciles the two sources before sending tempo to `PatternPlayer`:
```cpp
const float onsetBpm = onsetDetector.getCurrentBpm();
const float tempoCandidateBpm = beatTrackerLocked
    ? foldTrackerAliasTowardOnset(beatTracker.getBpm(), onsetBpm)
    : onsetBpm;
const float stablePlaybackBpm = tempoStabiliser.update(
    tempoCandidateBpm, playbackGate.isGateOpen(), numSamples, sr);
```

`PatternPlayer::setBpm()` still applies a 10% EMA during normal playback:
```cpp
void PatternPlayer::setBpm(float newBpm)
{
    const float clamped = juce::jlimit(40.0f, 320.0f, newBpm);
    bpm += 0.1f * (clamped - bpm);
}
```

On gate entry, `PlaybackGate` can request `snapBpm()`, which bypasses the EMA once so the first audible block is not dragged from the default 120 BPM. After that, normal `setBpm()` smoothing resumes.

---

## Tests

### Relevant test coverage

- `tests/test_pattern_player.cpp` covers `setBpm()`, `snapBpm()`, `snapToBarStart()`, and silence behavior.
- `tests/test_playback_gate.cpp` covers deferred beat-snap entry, phrase-breath holds, reset behavior, and gate decisions.
- `tests/test_processor_pipeline.cpp` exercises processor-level `prepareToPlay()` and `processBlock()` integration.

---

## Structure detection — why it may not match your dynamics

`StructureTagger` uses two thresholds (`src/analysis/StructureTagger.h`):
```cpp
static constexpr float kSilentRms  = 0.002f;
static constexpr float kLoudRms    = 0.08f;
```

And hold times to prevent flickering:
```cpp
static constexpr double kHoldSilentSec = 0.5;
static constexpr double kHoldSoftSec   = 2.0;
static constexpr double kHoldLoudSec   = 2.0;
```

For sludge metal with heavy distortion, **these thresholds will likely be wrong for you.** Distorted guitar compresses audio heavily — your "verse palm mute" and "chorus full chord" may have similar RMS. The 2-second hold times also mean the plugin is slow to react to fast section changes.

**Tuning approach**: Load the plugin in a DAW with a metering plugin on the same input chain. Play your typical verse riff and note the RMS, then your typical chorus and note the RMS. Set `kLoudRms` to be about 60% of the way between the two. Rebuild.

---

## Bass transposition — how root tracking works

The pitch estimator (`src/analysis/PitchEstimator.cpp`) runs YIN pitch detection on the guitar signal. Once it detects a pitch with confidence > 0.35 that stays stable for one beat, it extracts the pitch class (note name without octave) and maps it to a semitone offset from E2 (MIDI 40):

```cpp
// AccompanimentProcessor.cpp:488–494
int delta = pc - bassRootPc;  // bassRootPc = 4 (E)
if (delta > 6)  delta -= 12;
if (delta < -6) delta += 12;
patternPlayer.setBassSemitoneOffset(delta);
```

This transposition applies to all bass events in library patterns. For drop-C tuning, when you play a C chord, the pitch class is C (0), bassRootPc is E (4), so delta = -4. The bass root shifts from E2 to C2. This should work in principle, but needs testing against your actual playing since YIN has known issues with heavily distorted guitar (harmonics can confuse it).

When generative bass is active (ONNX only), the root tracking is separate — it passes `pitchRootMidi` directly into the bass inference model.

---

## The current pattern library — what's missing for sludge metal

All patterns are 4/4, kick-snare-hat only, one bar long (except Breakdown which is 2 bars). For sludge metal you'd want:

- **Half-time groove**: Snare on beat 3 only, kicks on 1 and 2.5, open hat on 2 and 4. This is the foundational sludge feel (think Mastodon Crack the Skye era, Sleep, Boris).
- **Doom/quarter-kick**: Kick on every beat, snare on 2 and 4, very slow ride. For low-BPM passages.
- **Blast beat**: For your fastest BPM range (>180). Currently `VerseFast` at high BPM gives rapid hat hits but not a blast pattern.
- **Crash accents on bar 1**: No crash cymbal (MIDI 49) is used anywhere in the current library.
- **Fill pattern**: A 1-bar fill to play at section transitions (currently no fill logic exists).

To add a pattern: add a `buildXxx()` function at the top of `MidiPatternLibrary.cpp`, add it to the `patterns` vector in the constructor, and update `patternCount()` to return the new count. Then update `RuleBasedInference::selectPattern()` to reference the new index.

---

## What would make it feel like a real bandmate — ranked by impact

### 1. Tighten beat-phase entry feel
The gate now waits for a beat-boundary snap through `PlaybackGate`. The next improvement is musical phrasing: making the entry feel like a drummer choosing a bar-length count-in or fill, not just the earliest safe beat.

### 2. Add a half-time groove pattern
For sludge, the half-time feel is more fundamental than the straight-8ths "Verse Slow" pattern. Add `buildHalfTime()` and route SOFT+low-BPM to it.

### 3. Enable the Breakdown pattern automatically
Wire the breakdown pattern to a "long SOFT section" detector — e.g., if the state has been SOFT for > 8 bars and BPM is below 110, select breakdown. It's currently only reachable by clicking the button.

### 4. Tune tempo stabilisation after gate-open
Gate entry already uses `snapBpm()` to avoid first-block EMA drag. The remaining tuning area is how quickly `TempoStabiliser` and `PatternPlayer::setBpm()` should follow real tempo changes once the groove is underway.

### 5. Tuned RMS thresholds for your rig
The SOFT/LOUD threshold at 0.08 RMS is almost certainly wrong for your signal chain. Tune it once with a metering plugin and you'll get much more accurate structure detection.

### 6. Crash on section change
When the structure state transitions SOFT→LOUD or LOUD→SOFT, emit a crash cymbal (MIDI 49) on channel 10 at the next bar boundary. This is one of the clearest "real drummer" signals.

---

## UI controls and what they do

| Control | Parameter ID | Effect |
|---|---|---|
| Output Gain | `outputGain` | Scales the dry guitar passthrough (not MIDI) |
| Intensity | `intensity` | Shifts the BPM window for pattern selection — high intensity biases toward faster/harder patterns |
| Structure blend | `structureBlend` | When ONNX is enabled, blends rule-based vs ML state. At 1.0 = ML only; at 0.0 = rules only. Currently no-op without ONNX |
| Generative bass | `generativeBassMode` | Auto/On/Off — Auto uses ONNX bass model if available, falls back to library patterns |
| Next Pattern | (button, `bumpDebugPattern()`) | Forces a pattern change and previews for 5 seconds |

---

## Building and testing

```bash
# Configure (from repo root)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Run tests
build/MetalAccompanimentTests/MetalAccompanimentTests

# After any change, bump the patch version in CMakeLists.txt line 4 first
# so you can confirm the correct build loaded in the DAW
```

The version number appears in the plugin UI top-right. Always bump before building so you know which version is loaded.

---

## Common failure modes

| Symptom | Likely cause | Where to look |
|---|---|---|
| Drums come in at wrong beat | BeatTracker phase/gate confidence is anchoring poorly for the performance | `PlaybackGate.*`, `BeatTracker.*`, `AccompanimentProcessor::processBlock()` |
| Drums never start | BeatTracker confidence stays below 0.50 | Check `displayBeatConfidence` in editor; tune `kPlaybackConfidenceStart` in AccompanimentProcessor.cpp:18 |
| BPM reads wrong | IOI median confused by fuzz harmonics | Lower `kAdaptiveK` in `OnsetDetector.h:89` or raise confidence floor |
| Wrong pattern for your energy level | RMS thresholds don't match your rig | `StructureTagger.h`: `kLoudRms`, `kSilentRms` |
| Bass plays wrong root | YIN pitch estimation confused by distortion | Confidence floor `kMinPitchConfidence` in `AccompanimentProcessor.cpp:15`; or accept the transposition lag |
| Bass sounds wrong register | `kBassRoot = 40` (E2) isn't your root | `MidiPatternLibrary.cpp:11` — change to 36 for C2 (drop-C) |
| Breakdown never fires automatically | Not wired into RuleBasedInference | `RuleBasedInference.cpp:9` — add a case for it |
