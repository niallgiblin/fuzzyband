# Architecture — AI Metal Accompaniment Plugin

> This document describes the component boundaries, threading model, and data
> flow for the Phase 1 rule-based implementation. It is written to make the
> Phase 2 ML swap-in as straightforward as possible.

---

## High-Level Overview

```
Guitar signal (audio in)
        │
        ▼
┌─────────────────────────────────────────────────┐
│                  JUCE Plugin                     │
│                                                  │
│  Audio Thread (real-time, hard deadline)         │
│  ┌────────────┐   ┌──────────────┐              │
│  │  OnsetDet  │   │ EnergyAnal   │              │
│  └─────┬──────┘   └──────┬───────┘              │
│        └────────┬─────────┘                      │
│                 ▼                                │
│         FeatureVector  ──► lock-free queue       │
│                 ▲                                │
│         atomic<int>  ◄── pattern index           │
│                 │                                │
│         PatternPlayer  ──► MidiBuffer            │
│                                                  │
│  Background Thread (50Hz inference loop)         │
│  ┌──────────────────────┐                        │
│  │  IInference          │                        │
│  │  (RuleBased / ONNX)  │                        │
│  └──────────────────────┘                        │
└─────────────────────────────────────────────────┘
        │
        ▼
MIDI out ──► Drum VSTi (ch 10) + Bass VSTi (ch 2)
```

---

## Component Reference

### `OnsetDetector`

**File:** `src/analysis/OnsetDetector.h/.cpp`  
**Thread:** Audio (called from `processBlock`)  
**Purpose:** Detects note attacks in the guitar signal and maintains a running
tempo estimate.

**Algorithm:**
1. Compute magnitude spectrum per buffer via `juce::dsp::FFT`
2. Spectral flux = sum of positive differences between successive spectra
3. Peak-pick flux signal above a configurable threshold
4. Collect inter-onset intervals (IOI), median-filter over last 8
5. Convert median IOI to BPM, clamp to [80, 220]

**Public interface:**
```cpp
class OnsetDetector {
public:
    void prepare(double sampleRate, int blockSize);
    void process(const float* audioData, int numSamples);
    float getCurrentBpm() const;          // thread-safe read
    bool  onsetDetectedThisBlock() const; // reset each block
};
```

**Key parameters:**
- `fluxThreshold` — sensitivity (default 0.35, tune by ear)
- `minBpm` / `maxBpm` — clamp range (default 80 / 220)
- `ioiWindowSize` — smoothing window (default 8 onsets)

---

### `EnergyAnalyser`

**File:** `src/analysis/EnergyAnalyser.h/.cpp`  
**Thread:** Audio (called from `processBlock`)  
**Purpose:** Computes RMS energy and spectral features used to classify the
current guitar state.

**Outputs:**
- `rmsEnergy` — 100ms rolling RMS, normalised 0..1
- `spectralCentroid` — weighted mean frequency (distinguishes palm mute from open chord)
- `highFreqFlux` — flux in 2kHz+ band (presence / attack content)

**Public interface:**
```cpp
class EnergyAnalyser {
public:
    void prepare(double sampleRate, int blockSize);
    void process(const float* audioData, int numSamples);
    float getRmsEnergy()       const;
    float getSpectralCentroid() const;
    float getHighFreqFlux()    const;
};
```

---

### `StructureTagger`

**File:** `src/analysis/StructureTagger.h/.cpp`  
**Thread:** Audio (called from `processBlock`)  
**Purpose:** Converts raw energy/spectral features into a discrete structural
state with hysteresis to prevent flickering.

**States:**
```cpp
enum class StructureState { SILENT, VERSE, CHORUS, BREAKDOWN };
```

**State transitions (threshold-based):**

```
rmsEnergy < 0.05                    → SILENT
rmsEnergy >= 0.05, centroid < 1200Hz → BREAKDOWN (half-time feel)
rmsEnergy >= 0.05, centroid < 2400Hz → VERSE
rmsEnergy >= 0.05, centroid >= 2400Hz → CHORUS
```

Hysteresis: minimum 2 seconds in any state before a transition is allowed.
This prevents a single quiet moment mid-riff from dropping to SILENT.

**Public interface:**
```cpp
class StructureTagger {
public:
    void prepare(double sampleRate);
    StructureState update(float rms, float centroid, float highFreqFlux);
    StructureState getCurrentState() const;
};
```

---

### `FeatureVector`

**File:** `src/analysis/FeatureVector.h`  
**Purpose:** Plain data struct passed from the audio thread to the inference
background thread via the lock-free queue. Must be trivially copyable.

```cpp
struct FeatureVector {
    float bpm;
    float rmsEnergy;
    float spectralCentroid;
    float highFreqFlux;
    StructureState state;
    int64_t sampleTimestamp; // for latency measurement
};
```

---

### `IInference` (interface)

**File:** `src/inference/IInference.h`  
**Purpose:** Abstract interface that decouples the inference implementation from
the rest of the plugin. Phase 1 uses `RuleBasedInference`. Phase 2 swaps in
`OnnxInference` without touching any other component.

```cpp
class IInference {
public:
    virtual ~IInference() = default;

    // Called once at startup. May allocate, load models, etc.
    virtual void prepare(double sampleRate) = 0;

    // Called at ~50Hz on the background thread. Must not block indefinitely.
    // Returns a pattern index into the MidiPatternLibrary.
    virtual int selectPattern(const FeatureVector& features) = 0;

    // Human-readable name for debug UI
    virtual std::string getName() const = 0;
};
```

---

### `RuleBasedInference` (Phase 1)

**File:** `src/inference/RuleBasedInference.h/.cpp`  
**Thread:** Background inference thread  
**Purpose:** Implements `IInference` using hand-authored rules. No ML.

**Logic:**
```
SILENT    → pattern index 0  (all-off / silence)
VERSE     + bpm < 120 → pattern 1  (slow verse groove)
VERSE     + bpm < 160 → pattern 2  (mid verse groove)
VERSE     + bpm >= 160 → pattern 3 (fast verse groove)
CHORUS    + bpm < 160 → pattern 4  (mid chorus, open hi-hat)
CHORUS    + bpm >= 160 → pattern 5 (fast chorus, double kick)
BREAKDOWN → pattern 6  (half-time, heavy ghost notes)
```

---

### `OnnxInference` (Phase 2 — active when `MA_ENABLE_ONNX=ON`)

**File:** `src/inference/OnnxInference.h/.cpp`  
**Thread:** Background inference thread  
**Purpose:** Implements `IInference` using ONNX Runtime against `assets/accompaniment_model.onnx`
bundled as JUCE `BinaryData` when `MA_ENABLE_ONNX` is enabled at build time.

`AccompanimentProcessor` constructs `OnnxInference` and calls `tryLoadModel()`. If loading fails
(or the option is off), it uses `RuleBasedInference` instead — no audio-thread change either way.

Tensor details (input `X` float32 `[1,5]`, output `Y` int64 clamped 0–6) are frozen in
[`docs/ONNX_IO.md`](docs/ONNX_IO.md); Phase 15 export must target that contract.

---

### `MidiPatternLibrary`

**File:** `src/midi/MidiPatternLibrary.h/.cpp`  
**Purpose:** Stores all drum and bass patterns as `constexpr` data. No file I/O.

```cpp
struct MidiEvent {
    uint8_t note;
    uint8_t velocity;
    float   beatOffset;   // in beats, e.g. 0.5 = eighth note into bar
    float   durationBeats;
};

struct MidiPattern {
    std::string          name;
    float                lengthInBars;
    std::vector<MidiEvent> drumEvents; // channel 10
    std::vector<MidiEvent> bassEvents; // channel 2
};

class MidiPatternLibrary {
public:
    const MidiPattern& getPattern(int index) const;
    int                patternCount() const;
};
```

Pattern indices 0–6 correspond to the outputs of `RuleBasedInference`.

---

### `PatternPlayer`

**File:** `src/midi/PatternPlayer.h/.cpp`  
**Thread:** Audio (called from `processBlock`)  
**Purpose:** Reads the current pattern index (via atomic), maintains a beat
clock, and fills the JUCE `MidiBuffer` with note-on/off events.

**Key behaviours:**
- Beat clock derived from `OnsetDetector::getCurrentBpm()` and sample position
- Pattern transitions are quantised to bar boundaries to avoid mid-bar glitches
- Note velocity is humanised: ±10 random offset per hit
- Note timing is humanised: ±2ms random offset per hit
- Sends a note-off flush when switching to SILENT state

**Public interface:**
```cpp
class PatternPlayer {
public:
    void prepare(double sampleRate, int blockSize);
    void setPatternIndex(int index);      // called by audio thread
    void process(MidiBuffer& midi,
                 int numSamples,
                 int64_t hostSamplePosition);
};
```

---

### `AccompanimentProcessor` (top-level plugin)

**File:** `src/AccompanimentProcessor.h/.cpp`  
**Purpose:** The `juce::AudioProcessor` subclass. Owns all components.
Runs the inference background thread.

**Ownership:**
```
AccompanimentProcessor
├── OnsetDetector
├── EnergyAnalyser
├── StructureTagger
├── std::unique_ptr<IInference>    ← RuleBasedInference or OnnxInference
├── MidiPatternLibrary
├── PatternPlayer
├── std::atomic<int>               ← pattern index handoff (acquire/release with inference/UI)
├── std::atomic<int>               ← debug preview sample countdown (paired with pattern index)
├── std::atomic<double>            ← cached sample rate (UI thread reads for debug pattern length)
├── moodycamel::ReaderWriterQueue  ← feature handoff
└── std::thread                    ← inference loop
```

**Lifecycle:** The inference thread is created in the constructor but stays idle (`inferencePaused == true`) until `prepareToPlay()` finishes, so `IInference::prepare(sampleRate)` always runs before the loop calls `selectPattern()`.

**Input path:** Non-finite samples are cleared to 0, then the buffer is clipped to `[-2, 2]` (SIMD `clip` alone is not sufficient for NaN on all targets).

**Sample rate:** `prepareToPlay` clamps a non-positive rate to 44100 Hz before wiring components; `OnsetDetector`, `EnergyAnalyser`, and `StructureTagger` each guard again if `prepare()` is ever called with an invalid rate.

**Soft bypass:** `processBlockBypassed()` clears MIDI, sends all-notes-off, resets the pattern player, and copies mono input to the right channel so the dry guitar still reaches the output.

---

## Threading Model

This is the most important section. Get this wrong and you get either audio
glitches (audio thread blocked) or crashes (data races).

### Audio thread

Runs in `processBlock()`. Has a hard real-time deadline (~5ms at 256 samples /
48kHz). **Must never:**
- Allocate or free heap memory
- Acquire a mutex
- Call any OS blocking primitive
- Access the filesystem
- Call ONNX Runtime directly

**What it does:**
1. Scrubs non-finite input samples, then clips to `[-2, 2]`
2. Calls `OnsetDetector::process()` and `EnergyAnalyser::process()`
3. Calls `StructureTagger::update()` to get current state
4. Pushes a `FeatureVector` onto the lock-free queue (non-blocking, always succeeds)
5. Reads `latestPatternIndex` via `std::atomic::load(memory_order_acquire)` (pairs with inference/UI stores)
6. Decrements `debugPreviewSamplesRemaining` with acquire load / release store when the debug pattern preview is active
7. Calls `PatternPlayer::process()` to fill `MidiBuffer`

### Background inference thread

Runs in a `std::thread` at ~50Hz (20ms sleep between iterations).
**Responsibilities:**
1. Pop `FeatureVector` from the lock-free queue
2. Call `IInference::selectPattern()` (may take 1–10ms, that's fine here)
3. If the debug preview countdown is not active, write result to `latestPatternIndex` via `std::atomic::store(memory_order_release)`

### Handoff primitives

| Data | Mechanism | Rationale |
|---|---|---|
| Feature vector (audio → inference) | `moodycamel::ReaderWriterQueue` | Single-producer single-consumer, wait-free, no allocation |
| Pattern index (inference/UI → audio) | `std::atomic<int>` with acquire/release | Coordinates with UI-driven debug pattern + preview countdown |
| Preview countdown (UI ↔ audio ↔ inference) | `std::atomic<int>` with acquire/release | Prevents inference from overwriting the pattern while preview is active |
| Sample rate (audio → UI) | `std::atomic<double>` | `bumpDebugPattern()` runs on the message thread |

### Why not a mutex?

A mutex on the audio thread means the OS can preempt it while it holds the lock,
causing a priority inversion that produces an audible glitch or xrun. Lock-free
primitives have bounded, allocation-free operation that is safe on a real-time
thread.

---

## Data Flow (per audio block)

```
processBlock() called by host
        │
        ├─► scrub non-finite samples; clip to [-2, 2]
        │
        ├─► OnsetDetector::process(audioData)
        │       └─► updates internal BPM estimate
        │
        ├─► EnergyAnalyser::process(audioData)
        │       └─► updates rms, centroid, highFreqFlux
        │
        ├─► StructureTagger::update(rms, centroid, flux)
        │       └─► returns current StructureState
        │
        ├─► Build FeatureVector { bpm, rms, centroid, flux, state }
        │
        ├─► featureQueue.try_enqueue(featureVector)   [non-blocking]
        │
        ├─► int pattern = latestPatternIndex.load(acquire)
        │
        └─► PatternPlayer::process(midiBuffer, numSamples, hostPosition)
                └─► fills MidiBuffer with drum + bass MIDI events


[Background thread, ~50Hz]
        │
        ├─► featureQueue.try_dequeue(featureVector)
        │
        ├─► if preview inactive: int pattern = inference->selectPattern(featureVector)
        │
        └─► latestPatternIndex.store(pattern, release)   [when preview countdown == 0]
```

---

## MIDI Channel Convention

| Channel | Content | Notes |
|---|---|---|
| 10 | Drums | GM standard drum channel |
| 2 | Bass | Arbitrary, configurable in future UI |

Both channels are emitted into the same `MidiBuffer` returned from `processBlock`.
The host DAW routes them to separate VSTi tracks.

---

## Build Targets

| Target | Description |
|---|---|
| `MetalAccompaniment_VST3` | VST3 plugin binary |
| `MetalAccompaniment_AU` | Audio Unit (macOS only) |
| `MetalAccompaniment_Standalone` | Standalone app for testing without a DAW |
| `PluginData` | BinaryData library (ONNX model, future assets) |
| `MetalAccompanimentTests` | Unit test binary (Catch2) |

---

## Extending to Phase 2

To swap in ML inference:

1. Implement `OnnxInference : public IInference`
2. In `AccompanimentProcessor` constructor, change:
   ```cpp
   // Phase 1
   inference = std::make_unique<RuleBasedInference>();

   // Phase 2
   inference = std::make_unique<OnnxInference>();
   ```
3. Nothing else changes. The audio thread, pattern player, and MIDI output are
   completely unaware of which inference implementation is active.

Add pitch/chord detection for Phase 2 by inserting a `PitchDetector` component
between `EnergyAnalyser` and `StructureTagger`, and adding `rootNote` and
`chordQuality` fields to `FeatureVector`.
