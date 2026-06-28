# TRIAGE.md — Fuzzyband v0.7.11 Prototype Fixes

Analysis of why drums don't follow playing and bass has issues.
Current state: rule-based path (no ONNX), manual BPM only.

---

## Summary

The plugin has three systemic problems that prevent it from working as a reactive accompanist:

1. **Tempo is 100% manual** — the BeatTracker exists but is never fed data, so drums always play at the fixed slider value (default 120 BPM).
2. **Structure detection has severe response lag** — hold times were scaled ×4–8 for "sludge pacing" making the system unresponsive to normal playing dynamics.
3. **Bass pitch follows guitar but silently falls back to C2** when pitch confidence drops, with no user feedback.

---

## Issue Breakdown

### [CRITICAL] BeatTracker receives no data — drums never lock to tempo

**File:** `src/AccompanimentProcessor.cpp` · `src/analysis/BeatTracker.cpp`

`BeatTracker::pushFluxSample()` is **never called** in `processBlock()`. The tracker holds its initial state forever: `smoothedBpm = 120.0f`, `confidence = 0.0f`, `locked = false`.

The BPM used for playback reads only from the APVTS parameter:
```cpp
float bpmForPlayer = 120.0f;
if (auto* rawBpm = apvts.getRawParameterValue("bpm"))
    bpmForPlayer = rawBpm->load();   // never uses beatTracker.getBpm()
```

The `PlaybackGate` also always passes `isOnsetTempoLocked = true` (hardcoded comment: "user sets BPM manually — D005"), which bypasses beat-phase snapping entirely.

**Effect:** Drums always play at 120 BPM (or whatever the slider says). If the guitarist plays at a different tempo the groove drifts continuously out of phase. This is the primary reason drums don't follow playing.

**Fix options (pick one for prototype):**

**Option A — Minimal: wire flux into BeatTracker, blend with manual BPM**

In `processBlock()`, after `energyAnalyser.process(in, numSamples)`, add:
```cpp
beatTracker.pushFluxSample(energyAnalyser.getHighFreqFlux(), hostSampleTime);
```

Then blend detected BPM with the manual value based on tracker confidence:
```cpp
const float trackedBpm  = beatTracker.getBpm();
const float trackedConf = beatTracker.getConfidence();
bpmForPlayer = trackedConf > 0.5f
    ? trackedBpm * trackedConf + bpmForPlayer * (1.0f - trackedConf)
    : bpmForPlayer;
```

Also update `PlaybackGate` call to use real lock state:
```cpp
const GateDecision gd = playbackGate.update(
    st,
    beatTracker.getBeatPhase01(),
    beatTracker.isLocked(),
    false,   // was hardcoded true; now let beat tracker drive snap
    beatTracker.getConfidence(),
    numSamples, sr);
```

**Option B — Simpler: add a Tap Tempo button to the UI**

If auto-detection is too unreliable, expose tap tempo via a button that calculates BPM from inter-tap intervals and writes it to the `bpm` APVTS parameter. The BPM slider already exists; tap tempo just feeds it automatically. Add to `AccompanimentEditor` and `AccompanimentProcessor::tapTempo()`.

---

### [CRITICAL] Structure detection hold times are too long — drums lag 6–10 s

**File:** `src/analysis/StructureTagger.h`

Hold times were increased ×4–8 from initial values "for sludge pacing". The result is that normal guitar dynamics produce no pattern change for many seconds:

| Transition | Current | Was | Effect |
|---|---|---|---|
| SILENT → anything | 0.0 s | 0.0 s | OK — exits immediately |
| AMBIENT → SOFT | **6.0 s** | 0.75 s | Quiet picking takes 6 s to trigger drums |
| SOFT → LOUD | **1.6 s** | 0.20 s | Acceptable |
| LOUD → SOFT | **6.0 s** | 1.50 s | Loud-to-quiet takes 6 s to change pattern |
| LOUD → SILENT | **8.0 s** | 2.00 s | Drums keep playing 8 s after stopping |
| BREAKDOWN → LOUD | **8.0 s** | — | Very sticky breakdown |

On top of these, the inference thread adds a **2-bar hold guard** before accepting a new drum pattern:
```cpp
const bool drumHoldExpired =
    (lastDrumPatternChangeSample < 0) ||
    (latest.sampleTimestamp - lastDrumPatternChangeSample >= twoBarsInSamplesDrum);
```
At 120 BPM that is 4 additional seconds before the first pattern change after structure shifts.

Total worst-case: 6 s (AMBIENT→SOFT) + 4 s (2-bar hold) = **10 s lag** before drums respond to a change in playing intensity.

**Fix:**

In `StructureTagger.h`, reduce hold times to values that feel musically reactive at mid-tempo:

```cpp
static constexpr double kHoldAmbientToSoftSec     = 0.5;   // was 6.0
static constexpr double kHoldAmbientToSilentSec   = 1.5;   // was 8.0
static constexpr double kHoldSoftToLoudSec        = 0.4;   // was 1.6 (still feels intentional)
static constexpr double kHoldSoftToAmbientSec     = 1.5;   // was 6.0
static constexpr double kHoldSoftToSilentSec      = 2.0;   // was 6.0
static constexpr double kHoldLoudToBreakdownSec   = 0.4;   // unchanged
static constexpr double kHoldLoudToSoftSec        = 2.0;   // was 6.0
static constexpr double kHoldLoudToSilentSec      = 3.0;   // was 8.0
static constexpr double kHoldBreakdownToLoudSec   = 2.0;   // was 8.0
static constexpr double kHoldBreakdownToSoftSec   = 3.0;   // was 8.0
static constexpr double kHoldBreakdownToSilentSec = 3.0;   // was 8.0
```

In `AccompanimentProcessor.cpp`, reduce the 2-bar drum hold guard minimum to **1 bar** (or drop the guard entirely for the prototype — it only prevents rapid switching, which isn't the current problem):

```cpp
// Change barsSinceChange >= 8 (auto-change) to >= 4
const bool autoChangeReady = (barsSinceChange >= 4);

// Change ">= 2 bar minimum hold" comment to 1 bar:
canCommit = (barsSinceStructChange >= 1);
```

---

### [HIGH] Bass always falls back to C2 when pitch is not detected

**Files:** `src/midi/RuleBasedBass.cpp` · `src/AccompanimentProcessor.cpp`

The rule-based bass generates a piano-roll step sequence rooted at `guitarRootMidi - 12`. Bass pitch is updated only when pitch confidence exceeds a threshold:

```cpp
static float lastBassPitch = 36.0f;   // C2 — never resets on prepareToPlay
if (rawConf > 0.15f && !digitalSilence)
    lastBassPitch = rawMidi;
```

`StablePitchTracker` further requires the pitch class to be stable for **one full beat** (at 120 BPM = 0.5 s). If the guitarist plays chord strums, fast passages, or the input gain is low, confidence rarely exceeds `kMinPitchConfidence` (defined in `StablePitchTracker.h`) and the bass just stays on C2 with no semitone offset.

There's also a bug: `lastBassPitch` is a `static` local, so it is **never reset by `prepareToPlay()`**. After a plugin reload within the same DAW session it retains the last pitch from the previous session.

**Separate bass issue — sectionName during free-play:**

When `playActive = false` (Play button off), `structureSequencer` never advances. `getCurrentSectionName()` returns the first section of the active preset, which for "Standard Metal" is `"INTRO"`. The `RuleBasedBass::notesPerBar()` for INTRO returns `1` (one held root per bar, velocity 65). The result is a very sparse, quiet bass when playing freely without pressing Play.

**Fixes:**

1. Move `lastBassPitch` out of the function body into `AccompanimentProcessor` as a member variable and reset it in `prepareToPlay()`:
   ```cpp
   // In AccompanimentProcessor.h, private section:
   float lastBassPitch = 36.0f;
   // In prepareToPlay():
   lastBassPitch = 36.0f;
   ```

2. In `RuleBasedBass::generate()`, fall back to `"VERSE"` behavior when `sectionName` is null or when Play is off, instead of `"INTRO"`. Caller can pass a neutral default:
   ```cpp
   // In processBlock(), when !playActive, pass "VERSE" instead of sequencer section:
   const char* bassSection = playActive.load(std::memory_order_acquire)
       ? structureSequencer.getCurrentSectionName()
       : "VERSE";
   auto bassCommit = RuleBasedBass::generate(lastBassPitch, bassSection, ...);
   ```

3. Lower the confidence threshold for bass pitch update in `processBlock`:
   ```cpp
   if (rawConf > 0.08f && !digitalSilence)   // was 0.15f
       lastBassPitch = rawMidi;
   ```

---

### [MEDIUM + TRAINING MISMATCH] Playing-style classifier overrides all pattern selection silently

**File:** `src/AccompanimentProcessor.cpp` · `src/inference/pattern_rules.h`

When an ONNX build is loaded with the style classifier, `stylePatternPool` always overrides the section pool AND the inference result:

```cpp
// Style-driven pattern override (M009) — this runs UNCONDITIONALLY
auto stylePool = PatternRules::stylePatternPool(playingStyle);
if (stylePool.count > 0)
    sectionDrumIdx = stylePool.indices[globalBar % stylePool.count];
```

Style index 0–3 all have `count > 0`, so the style classifier has total veto power over every other layer of pattern selection. If the classifier misfires (e.g., reads silence or the wrong class), the pattern chosen by structure/section logic is silently discarded.

With no ONNX build this is not active (classifier is null, `currentPlayingStyle = 0` maps to style 0 = palm mute pool). But style 0 is the *default* uninitialized value — meaning on a non-ONNX build, **the style pool for "palm mute" is always active and always overrides everything**.

**Additional training mismatch (ONNX builds):** The plugin sits *before* the amp/cab sim in the FX chain and receives a clean DI guitar signal. The existing `classifier.onnx` was trained on recordings made with an amp sim at near-breakup — a substantially different mel spectrogram profile. Clean DI has sharper transients, brighter harmonics, and wider dynamic range than a driven signal. The classifier's learned features may not transfer. **The training data must be re-recorded as clean DI to match the runtime signal.** See PROTOTYPE_PLAN.md Day 2.

Wait — on a non-ONNX build, `styleClassifier = nullptr`, so `classify()` never runs and `currentPlayingStyle` stays at 0 forever. `stylePatternPool(0)` returns `{3, {1, 2, 7}}` (Verse Groove, Verse Half-Time, Half-Time). These are all SOFT patterns. So on a non-ONNX build, the drums are permanently locked to SOFT patterns regardless of playing intensity.

**Fix:** Gate the style override on whether the classifier is actually running:

```cpp
// Only apply style override if classifier is active and has recently classified
const bool styleClassifierActive = (styleClassifier != nullptr)
    && styleClassifier->isLoaded()
    && (styleClassifyCount.load(std::memory_order_relaxed) > 0);

auto stylePool = PatternRules::stylePatternPool(playingStyle);
if (stylePool.count > 0 && styleClassifierActive)
    sectionDrumIdx = stylePool.indices[globalBar % stylePool.count];
```

This is probably **the root cause of drums not changing pattern** on the non-ONNX build — they are stuck at pattern indices {1, 2, 7} regardless of playing intensity. LOUD and BREAKDOWN patterns (4, 5, 6) are never selected.

---

### [MEDIUM] `structureSilent` flag keeps drums quiet even when playing

**File:** `src/AccompanimentProcessor.cpp`

```cpp
const int style = currentPlayingStyle.load(std::memory_order_acquire);
const bool audioActive = (rms > 0.001f) || (style != 4);
const bool trulySilent = (!playOn && !audioActive) || digitalSilence;
```

`currentPlayingStyle` is initialized to `0` (palm mute). `style != 4` is `0 != 4 = true`. So `audioActive = true` always on a non-ONNX build. This means `trulySilent = false` whenever `digitalSilence = false`, which is correct.

On an ONNX build where the classifier sets `style = 4` (silence class) when no guitar is playing, this correctly silences the drums. But if the classifier drifts (classifies active playing as silence), drums are silenced despite guitar input.

This is only a risk on ONNX builds; on rule-based builds, the drums won't be silenced by this flag. **Not a primary fix target.**

---

### [LOW] RMS thresholds may be calibrated for high-gain guitar only

**File:** `src/analysis/StructureTagger.h`

```cpp
static constexpr float kSilentRms   = 0.012f;   // -38 dBFS
static constexpr float kAmbientCeil = 0.025f;   // -32 dBFS
static constexpr float kLoudRms     = 0.075f;   // -22 dBFS
static constexpr float kBreakdownRms = 0.12f;   // -18 dBFS
```

A guitar plugged into a typical audio interface at moderate gain will output RMS around −22 to −16 dBFS during strumming. A clean DI at low gain could be −30 dBFS or below, which would put it in the AMBIENT band and require 6 seconds (after fix: 0.5 s) to trigger SOFT. If the user's guitar signal is on the quiet side, drums will be sluggish.

**Fix:** No code change — user should check the RMS readout in the plugin UI. If it reads below 0.025 during strumming, the input gain needs to be raised at the audio interface.

---

### [LOW] Beat phase sync: first bar may start offset from guitar's downbeat

**File:** `src/analysis/PlaybackGate.cpp` · `src/AccompanimentProcessor.cpp`

When `isOnsetTempoLocked = true` (current hardcoded value), the gate opens immediately and fires `snapToBarStart()` on the first transition from closed to open. This zeros `PatternPlayer::beatPosition` and starts the bar counter from that sample. There is no mechanism to align the bar boundary with the guitarist's actual downbeat.

**Effect:** The snare on beat 2 and 4 may land between the guitarist's expected beats 2 and 4, making the groove feel off-beat. This is particularly noticeable at 120 BPM where 1 beat = 0.5 s; a misalignment of even a half-beat is audible.

**Fix for prototype:** After implementing Option A (BeatTracker) above, `snapBeatNow` fires at the detected beat boundary rather than on arbitrary open-gate edge. Alternatively, expose a "re-sync" button that calls `patternPlayer.snapToBarStart()` at a time the user chooses.

---

### [LOW] Stale documentation / dead code

- `CLAUDE.md` and `AGENTS.md` both reference `TempoStabiliser` as a component in `src/analysis/`. No such file exists. BeatTracker is the replacement.
- `ARCHITECTURE.md` mentions `OnsetDetector` which no longer exists (replaced by BeatTracker + EnergyAnalyser).
- `README.md` top-line says "Version 0.5.5" — should be 0.7.11.
- BeatTracker is fully implemented (comb-filter autocorrelation, 80–220 BPM search) but is dead code. Wire it up or remove it to avoid confusion.

---

## Priority Checklist for Working Prototype

| # | Fix | File(s) | Effort | Impact |
|---|-----|---------|--------|--------|
| 1 | **Gate style override on classifier being active** | `AccompanimentProcessor.cpp` | 5 min | Unblocks LOUD/BREAKDOWN patterns on non-ONNX builds |
| 2 | **Reduce StructureTagger hold times** | `StructureTagger.h` | 5 min | Drums respond in < 1 s instead of 6–10 s |
| 3 | **Reduce/remove 2-bar inference hold guard** | `AccompanimentProcessor.cpp` | 5 min | Pattern changes no longer delayed 4 s after structure change |
| 4 | **Fix bass sectionName fallback (VERSE not INTRO)** | `AccompanimentProcessor.cpp` | 5 min | Bass produces 2 notes/bar at normal velocity instead of 1 quiet held root |
| 5 | **Fix lastBassPitch static → member, reset on prepare** | `AccompanimentProcessor.h/.cpp` | 10 min | Bass pitch resets correctly on plugin reload |
| 6 | **Wire BeatTracker (Option A) or add tap tempo (Option B)** | `AccompanimentProcessor.cpp` + optionally `AccompanimentEditor` | 30–60 min | Drums actually lock to the guitarist's tempo |
| 7 | **Lower pitch confidence threshold** | `AccompanimentProcessor.cpp` | 2 min | Bass pitch updates more reliably |

Fixes 1–5 can be done in under 30 minutes and should produce noticeable improvement without touching tempo detection. Fix 6 is the real game-changer for "drums follow playing."

---

## Diagnostic: How to verify what is actually happening

Open the plugin UI and watch the live readouts while playing:

- **BPM**: reads the APVTS "bpm" parameter, not BeatTracker output. Will always show the slider value, never change with playing.
- **State**: shows `displayStateIndex` which is `committedStructureState`. Watch how long it takes to go from SILENT → SOFT/LOUD when you start playing. If it never changes, hold times are too long or RMS thresholds are wrong.
- **RMS**: the absolute RMS of the input. Should be > 0.025 during normal playing; if not, raise interface gain.
- **Pattern**: shows `displayPatternIndex` (inference intent). If stuck at 0 or cycling only through 1/2/7, the style-pool override bug (Issue 4) is active.

