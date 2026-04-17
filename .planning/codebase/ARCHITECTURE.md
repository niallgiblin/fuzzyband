# Architecture

**Analysis Date:** 2026-04-17 (threading details aligned with repo `ARCHITECTURE.md`)

## Pattern Overview

**Overall:** Real-time audio plugin with lock-free threading and pluggable inference layer

**Key Characteristics:**
- Single-producer single-consumer lock-free queue for feature vector handoff
- Audio thread (real-time, hard deadline ~5ms) separate from background inference thread (~50Hz)
- Pluggable inference interface (`IInference`) to support Phase 1 rule-based and Phase 2 ONNX implementations
- No heap allocation on audio thread
- Atomic variables for pattern index synchronization

## Layers

**Analysis Layer:**
- Purpose: Extract audio features from incoming guitar signal in real-time
- Location: `src/analysis/`
- Contains: `OnsetDetector`, `EnergyAnalyser`, `StructureTagger`
- Depends on: JUCE audio processing library, FFT primitives
- Used by: `AccompanimentProcessor.processBlock()` on audio thread

**Feature Representation:**
- Purpose: Plain data structure for passing analysis results to inference thread
- Location: `src/analysis/FeatureVector.h`
- Contains: BPM, RMS energy, spectral centroid, high-frequency flux, structural state
- Depends on: `StructureState` enum
- Used by: Inference layer via lock-free queue

**Inference Layer:**
- Purpose: Map audio features to pattern selection (rule-based in Phase 1, ML in Phase 2)
- Location: `src/inference/`
- Contains: `IInference` interface, `RuleBasedInference` implementation, `OnnxInference` stub
- Depends on: `FeatureVector`
- Used by: `AccompanimentProcessor.inferenceLoop()` on background thread

**MIDI Generation Layer:**
- Purpose: Convert pattern indices and timing to MIDI note events on drum (ch. 10) and bass (ch. 2) tracks
- Location: `src/midi/`
- Contains: `MidiPatternLibrary`, `PatternPlayer`
- Depends on: JUCE MIDI buffer, pattern library
- Used by: `AccompanimentProcessor.processBlock()` on audio thread

**Plugin Host Layer:**
- Purpose: Interface between DAW and plugin components; manage threading and state
- Location: `src/AccompanimentProcessor.h/.cpp`, `src/AccompanimentEditor.h/.cpp`
- Contains: Audio processor implementation, GUI editor, parameter state
- Depends on: JUCE `AudioProcessor` base class
- Used by: DAW host

## Data Flow

**Per-Block Audio Processing:**

1. DAW calls `processBlock(AudioBuffer, MidiBuffer)` on audio thread
2. Non-finite input samples cleared to 0; buffer clipped to `[-2, 2]`
3. `OnsetDetector::process()` analyzes spectral flux and updates BPM estimate
4. `EnergyAnalyser::process()` computes RMS, spectral centroid, high-frequency flux
5. `StructureTagger::update()` maps features to discrete state (SILENT/VERSE/CHORUS/BREAKDOWN)
6. `FeatureVector` constructed with current audio metrics
7. `featureQueue.try_enqueue()` pushes feature vector (non-blocking, lock-free)
8. `latestPatternIndex.load(memory_order_acquire)` reads pattern (coordinates with inference/UI)
9. `PatternPlayer::process()` fills MIDI buffer with drum/bass events based on pattern index
10. MIDI buffer returned to DAW

**Background Inference Loop:**

1. Background thread sleeps for ~20ms (skipped while `inferencePaused` after construction until `prepareToPlay` completes)
2. `featureQueue.try_dequeue()` pops most recent feature vector (drops older ones if queue full)
3. If debug preview countdown is zero: `IInference::selectPattern()` runs decision logic (1-10ms typical)
4. `latestPatternIndex.store(memory_order_release)` updates pattern index when preview is inactive
5. Loop repeats at ~50Hz

**State Management:**

- **Atomic variables:** `latestPatternIndex` (pattern selection; acquire/release with inference and debug UI), `debugPreviewSamplesRemaining` (preview countdown), `cachedSampleRate` (UI-driven debug pattern length), `currentBpm` (BPM display), `displayStateIndex` (structural state display), `displayPatternIndex` (pattern display)
- **Lock-free queue:** Feature vector handoff (`moodycamel::ReaderWriterQueue`)
- **Local mutable state:** BPM history, FFT buffers, beat position tracking in `PatternPlayer`
- **JUCE APVTS:** Plugin parameter state (saved/loaded with session)

## Key Abstractions

**OnsetDetector:**
- Purpose: Estimates current BPM via spectral flux peak detection and inter-onset interval analysis
- Location: `src/analysis/OnsetDetector.h/.cpp`
- Pattern: State machine tracking FFT frame buffer, onset history ring buffer, inter-onset interval smoothing
- Public interface: `process()` (audio thread), `getCurrentBpm()` (atomic read)

**EnergyAnalyser:**
- Purpose: Extracts three complementary audio descriptors for state classification
- Location: `src/analysis/EnergyAnalyser.h/.cpp`
- Pattern: Rolling window RMS, spectral moment computation, high-frequency band energy tracking
- Public interface: `process()` (audio thread), accessors for RMS/centroid/flux

**StructureTagger:**
- Purpose: Converts continuous features into discrete structural state with hysteresis
- Location: `src/analysis/StructureTagger.h/.cpp`
- Pattern: Threshold-based state machine with 2-second minimum hold time to prevent flickering
- Public interface: `update()` returns current state, `getCurrentState()` accessor

**PatternPlayer:**
- Purpose: Beat-aligned MIDI event emission with humanization
- Location: `src/midi/PatternPlayer.h/.cpp`
- Pattern: Tracks beat position derived from BPM and sample counter; emits note-on/off events quantized to bar boundaries
- Public interface: `process()` fills MIDI buffer, setters for BPM/pattern/silence state

**IInference (Interface):**
- Purpose: Decouples inference implementation from audio processing
- Location: `src/inference/IInference.h`
- Pattern: Virtual methods `prepare()`, `selectPattern()`, `getName()`
- Design benefit: Phase 2 ONNX implementation can be swapped without modifying audio thread code

## Entry Points

**Plugin Initialization:**
- Location: `src/AccompanimentProcessor.cpp` constructor
- Triggers: DAW instantiates plugin
- Responsibilities: Create components, start background inference thread, initialize lock-free queue

**Audio Processing Block:**
- Location: `src/AccompanimentProcessor::processBlock()`
- Triggers: DAW audio thread at ~48kHz (256-4096 samples per block)
- Responsibilities: Call analysis components, push features to queue, pull pattern from atomic, fill MIDI buffer

**Inference Loop:**
- Location: `src/AccompanimentProcessor::inferenceLoop()` (runs on `std::thread`)
- Triggers: Background thread created at startup, runs continuously at ~50Hz
- Responsibilities: Pop feature vectors, run inference, update pattern index

**GUI Editor:**
- Location: `src/AccompanimentEditor.cpp`
- Triggers: DAW opens plugin UI or user clicks "Next Pattern (preview 5s)" button
- Responsibilities: Display BPM/state/pattern, handle debug pattern preview

## Error Handling

**Strategy:** Fail-safe defaults with graceful degradation

**Patterns:**

- **Lock-free queue overflow:** `featureQueue.try_enqueue()` drops feature if queue full; inference uses last-known state
- **Missing pattern:** `getPattern()` clamps index to valid range [0, patternCount-1]
- **Inference timeout:** Background thread sleeps 20ms regardless; does not block audio thread
- **Invalid FFT state:** `OnsetDetector` maintains safe defaults (120 BPM) if insufficient data accumulated
- **Structural state transitions:** Hysteresis ensures minimum 2-second hold to prevent oscillation

No exceptions thrown from audio-thread code. All JUCE assertions active in debug builds.

## Cross-Cutting Concerns

**Logging:** Debug output via `DBG()` macro (JUCE); enabled in debug builds, compiled out in release. No output from audio thread.

**Validation:** 
- `prepare()` methods verify sample rate and block size are non-zero
- `StructureTagger::update()` clamps RMS and centroid to valid ranges
- `PatternPlayer` asserts library is non-null before use

**Thread Safety:**
- Audio thread: uses atomic loads only, no blocking primitives
- Background thread: uses atomic stores only, sleeps between iterations
- Handoff: lock-free queue for feature vectors, atomic integer for pattern index
- No mutexes anywhere in real-time critical path

---

*Architecture analysis: 2026-04-16*
