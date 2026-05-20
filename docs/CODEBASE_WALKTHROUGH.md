# Codebase Walkthrough

This document explains the plugin in plain language, with enough C++ and JUCE context to make the code approachable.

## The Big Picture

Metal Accompaniment is a JUCE audio plugin. The host DAW calls `AccompanimentProcessor::processBlock()` for each block of guitar audio. Inside that callback, the plugin:

1. Cleans and clips the input samples.
2. Extracts tempo, energy, structure, and pitch features.
3. Pushes a `FeatureVector` into a lock-free queue.
4. Reads the latest pattern decision from an atomic integer.
5. Asks `PatternPlayer` to add drum and bass MIDI events to the block.
6. Passes the dry guitar audio through with output gain.

The important constraint is that `processBlock()` is the real-time audio thread. It must finish quickly and must not block. Long-running work, file I/O, and ONNX model inference are moved to background threads.

## Entry Points

`src/AccompanimentProcessor.cpp` is the main plugin entry point:

- Constructor: creates APVTS parameters, chooses inference backends, starts the background inference thread (`src/AccompanimentProcessor.cpp:111`).
- `prepareToPlay()`: allocates and prepares DSP objects before playback (`src/AccompanimentProcessor.cpp:140`).
- `processBlock()`: real-time block processing (`src/AccompanimentProcessor.cpp:402`).
- `inferenceLoop()`: background queue drain and pattern selection (`src/AccompanimentProcessor.cpp:367`).
- `processBlockBypassed()`: flushes MIDI and passes audio through on bypass (`src/AccompanimentProcessor.cpp:604`).

JUCE creates the plugin through `createPluginFilter()` (`src/AccompanimentProcessor.cpp:630`). The editor is created with `createEditor()` (`src/AccompanimentProcessor.cpp:625`).

## Core Classes

### `AccompanimentProcessor`

This is the coordinator. It owns all major subsystems:

- `OnsetDetector`: spectral-flux onset detection and IOI tempo fallback.
- `BeatTracker`: autocorrelation-style pulse tracking from onset-strength flux.
- `TempoStabiliser`: prevents audible tempo jumps once playback is open.
- `EnergyAnalyser`: RMS, spectral centroid, high-frequency flux.
- `StructureTagger`: rule-based SILENT / SOFT / LOUD state.
- `PitchEstimator`: monophonic guitar pitch estimate.
- `StablePitchTracker`: waits for pitch stability before transposing bass.
- `PatternPlayer`: turns pattern data into MIDI.
- `FeatureCapture`: optional non-real-time JSONL debug capture.
- `IInference` / `IStructureInference` / `OnnxBassInference`: background inference.

You can see this ownership list in `src/AccompanimentProcessor.h:103`.

### `FeatureVector`

`FeatureVector` is the small data snapshot sent from the audio thread to the inference thread. It carries BPM, RMS, centroid, high-frequency flux, state, sample timestamp, pitch, intensity policy, and RMS delta (`src/analysis/FeatureVector.h`).

Think of it as a single "what the guitar is doing right now" packet.

### `PatternPlayer`

`PatternPlayer` renders MIDI. It tracks beat position, active pattern, pending pattern, note-offs, humanization, static bass, and generative bass state.

Pattern changes are not applied immediately. The requested pattern is stored, then committed at a bar boundary (`src/midi/PatternPlayer.cpp:429`). That is a good audio UX choice because abrupt mid-bar groove changes feel broken.

### `MidiPatternLibrary`

`MidiPatternLibrary` is the in-code pattern bank. It builds seven patterns:

- 0: Silent
- 1: Verse slow
- 2: Verse mid
- 3: Verse fast
- 4: Chorus mid
- 5: Chorus fast
- 6: Breakdown

The rule-based path currently maps SILENT to 0, SOFT to 1-3, and LOUD to 4-5. Breakdown is present in the library but not chosen by the current rule logic.

### `AccompanimentEditor`

The editor is a JUCE UI with APVTS-backed controls for intensity, structure blend, and generative bass mode. It polls diagnostic values at 20 Hz with a `juce::Timer` (`src/AccompanimentEditor.cpp:132`).

It does not read analysis objects directly. It reads atomic display values exposed by the processor, which keeps UI work off the audio path.

## One Audio Block, Step By Step

Inside `processBlock()`:

1. `juce::ScopedNoDenormals` protects DSP code from denormal slowdowns (`src/AccompanimentProcessor.cpp:404`).
2. MIDI is cleared for this output block (`src/AccompanimentProcessor.cpp:406`).
3. Non-finite samples are scrubbed, then input is clipped to `[-2, 2]` (`src/AccompanimentProcessor.cpp:416`).
4. `OnsetDetector` and `EnergyAnalyser` process the input (`src/AccompanimentProcessor.cpp:429`).
5. `StructureTagger` produces SILENT / SOFT / LOUD (`src/AccompanimentProcessor.cpp:437`).
6. `PitchEstimator` produces raw MIDI pitch and confidence (`src/AccompanimentProcessor.cpp:439`).
7. The beat tracker and onset BPM are reconciled into a tempo candidate (`src/AccompanimentProcessor.cpp:446`).
8. `PlaybackGate` decides whether the MIDI player should remain silent, open on the next beat, reset after silence, or arm a crash (`src/AccompanimentProcessor.cpp:456`).
9. A `FeatureVector` is filled and enqueued (`src/AccompanimentProcessor.cpp:480`, `src/AccompanimentProcessor.cpp:503`).
10. `PatternPlayer` receives current BPM, pattern index, silence policy, and bass mode (`src/AccompanimentProcessor.cpp:505`).
11. `PatternPlayer::process()` emits MIDI (`src/AccompanimentProcessor.cpp:544`).
12. Dry audio is copied to output with gain (`src/AccompanimentProcessor.cpp:546`).
13. Display atomics are updated for the UI (`src/AccompanimentProcessor.cpp:560`).

## Background Inference

The constructor starts `inferenceThread` (`src/AccompanimentProcessor.cpp:123`). That thread loops while `inferenceRunning` is true, sleeps during pauses, drains the feature queue, runs inference, and sleeps for 20 ms (`src/AccompanimentProcessor.cpp:367`).

`drainFeatureQueueAndRunInference()` drains all queued feature snapshots and keeps only the latest (`src/AccompanimentProcessor.cpp:189`). This is a useful real-time design: if the inference thread falls behind, it skips stale states instead of making the musician wait for old decisions.

Inference then:

- Optionally processes structure shadow inference.
- Blends ML structure with rule structure based on `structureBlend`.
- Selects a pattern.
- Enforces two-bar pattern and bass hold guards.
- Pushes feature-capture rows if capture is enabled.
- Runs ONNX bass proposal when available.

The audio thread never calls ONNX directly.

## C++ Concepts Used Here

- `std::unique_ptr`: exclusive ownership for polymorphic inference backends and JUCE/ORT objects.
- `std::atomic`: lock-free-style handoff for display values, pattern index, run flags, and feature toggles.
- `std::thread`: background inference and capture writer threads.
- RAII: destructors stop and join threads (`AccompanimentProcessor::~AccompanimentProcessor`, `FeatureCapture::~FeatureCapture`).
- `std::vector`: used for DSP buffers, but allocated in `prepare()`, not per sample.
- `std::array` and fixed C arrays: used for small bounded real-time data.

## What To Read When Changing Behavior

| Goal | Start Here |
|---|---|
| Change when drums start or stop | `src/analysis/PlaybackGate.cpp`, then `src/AccompanimentProcessor.cpp` |
| Change tempo tracking | `src/analysis/BeatTracker.cpp`, `src/analysis/OnsetDetector.cpp`, `src/analysis/TempoStabiliser.cpp` |
| Change energy/section sensitivity | `src/analysis/EnergyAnalyser.cpp`, `src/analysis/StructureTagger.cpp` |
| Change pattern choice | `src/inference/pattern_rules.h`, `src/inference/RuleBasedInference.cpp`, `src/inference/OnnxInference.cpp` |
| Change actual MIDI grooves | `src/midi/MidiPatternLibrary.cpp`, `src/midi/PatternPlayer.cpp` |
| Change bass pitch behavior | `src/analysis/PitchEstimator.cpp`, `src/analysis/StablePitchTracker.cpp`, `src/midi/PatternPlayer.cpp` |
| Change UI controls/readouts | `src/AccompanimentEditor.*`, APVTS layout in `src/AccompanimentProcessor.cpp` |
| Change training/capture workflow | `src/capture/FeatureCapture.*`, `training/`, `docs/FEATURE_CAPTURE.md` |

