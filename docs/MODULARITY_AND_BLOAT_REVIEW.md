# Modularity and Bloat Review

This document reviews how modular the project is, where complexity is concentrated, and what could be trimmed or extracted.

## Executive Summary

The codebase is not bloated in the usual sense. It has a real amount of domain complexity: real-time audio, MIDI scheduling, tempo tracking, ONNX fallback, feature capture, and host/plugin lifecycle. Most modules have a reason to exist.

The main maintainability issue is not too many files. It is that two coordinator files still carry a lot of policy:

- `src/AccompanimentProcessor.cpp`: 633 lines
- `src/midi/PatternPlayer.cpp`: 456 lines

Those files are understandable, but they are where future bugs are most likely to cluster.

## Size Snapshot

Largest production files by line count:

| File | Lines | Comment |
|---|---:|---|
| `src/AccompanimentProcessor.cpp` | 633 | Main coordinator plus inference policy and capture row construction |
| `src/midi/PatternPlayer.cpp` | 456 | MIDI scheduler, static bass, generative bass, note-off management |
| `src/inference/OnnxStructureInference.cpp` | 328 | ONNX windowing, smoothing, confidence gates |
| `src/capture/FeatureCapture.cpp` | 267 | File capture, queue drain, JSONL serialization |
| `src/analysis/OnsetDetector.cpp` | 236 | Spectral flux, tempo lock, IOI history |
| `src/AccompanimentEditor.cpp` | 217 | Custom editor layout and diagnostics |
| `src/midi/MidiPatternLibrary.cpp` | 209 | Pattern data |
| `src/inference/OnnxBassInference.cpp` | 205 | Bass ONNX load/run/decode |
| `src/analysis/BeatTracker.cpp` | 204 | Autocorrelation pulse tracker |
| `src/inference/OnnxInference.cpp` | 198 | Pattern ONNX load/run/fallback |

This is a healthy distribution for a small plugin, with two obvious hotspots.

## What Is Already Modular

The project has already extracted several good "deep" modules:

- `PlaybackGate`: encapsulates start/stop/phrase-breath decisions.
- `TempoStabiliser`: keeps playback tempo from jumping mid-groove.
- `StablePitchTracker`: isolates pitch stability and bass transpose rules.
- `pattern_rules.h`: centralizes shared rule pattern mapping.
- `FeatureCapture`: owns capture queue and writer thread.
- `IInference` and `IStructureInference`: preserve the rule/ONNX boundary.

These are good examples to follow: each module owns one decision area and is independently testable.

## Where Complexity Is Still Concentrated

### 1. `AccompanimentProcessor`

`AccompanimentProcessor` currently does all of this:

- JUCE lifecycle and APVTS setup.
- Thread startup/shutdown.
- Audio analysis orchestration.
- Feature vector construction.
- Playback gate response.
- Pitch-to-bass handoff.
- Inference queue drain.
- Structure blending policy.
- Pattern hold policy.
- Feature capture row construction.
- Generative bass handoff.
- UI display atomics.
- State save/restore.

That is a lot. Some of it must stay in the processor because JUCE calls the processor directly. But some policy could move out.

Best next extraction:

`InferenceCoordinator`

Responsibilities:

- Own `IInference`, `IStructureInference`, and `OnnxBassInference`.
- Drain `FeatureVector` queue.
- Apply structure blend and pattern rejection.
- Enforce pattern/bass hold policy.
- Produce small output structs:
  - `PatternDecision`
  - `BassStepFrame`
  - optional `FeatureCaptureRow`

This would make `AccompanimentProcessor` closer to a real-time shell.

### 2. `PatternPlayer`

`PatternPlayer` does:

- active/pending pattern scheduling,
- beat clock advancement,
- static MIDI pattern rendering,
- drum humanization,
- library bass note-off handling,
- single-note generative bass,
- 16-step generative bass,
- deferred note-off sorting,
- transition crash.

Best next extraction:

`BassRenderer`

Responsibilities:

- static library bass rendering,
- generative bass step rendering,
- deferred note-off tracking,
- note range clamping.

Keep `PatternPlayer` responsible for beat windows and pattern scheduling.

### 3. `MidiPatternLibrary`

The library is mostly data. It is not bad bloat, but C++ pattern construction is not a great authoring format. If patterns grow, consider an offline pattern definition format loaded at build time or compiled into generated C++.

For now, leave it alone. Runtime file loading would add plugin-host complexity and is not worth it yet.

## Things That Can Be Trimmed

### Unused `prevStructureSilent`

`prevStructureSilent` is set in `prepareToPlay()` and assigned at the end of `processBlock()`, but in the current source it does not appear to drive behavior. It may be leftover state from earlier transition logic.

Recommendation: confirm with tests and remove it if unused.

### Stale Docs

Some existing docs describe older implementations. This is not runtime bloat, but it is cognitive bloat. The biggest example is older playback-gate narrative in `docs/PLUGIN_GUIDE.md`.

Recommendation: update or label old sections as historical.

### Repeated ONNX Boilerplate

The three ONNX classes repeat:

- `Ort::Env` helper,
- session options,
- BinaryData session creation,
- shape/type validation,
- run wrapper,
- exception fallback.

This is not urgent because it is isolated and explicit. If another ONNX model is added, create a tiny helper for session construction and tensor contract validation.

### Manual JSON Serialization

`FeatureCapture` manually serializes JSON strings. It is careful and simple enough today. If schema grows, a small JSON writer helper would reduce mistakes.

Avoid adding a large JSON dependency to plugin runtime unless the benefit is clear.

## Things Not To Trim Yet

### Rule-Based Fallback

Do not remove rule inference just because ONNX exists. It keeps default builds simple, provides deterministic tests, and gives users a fallback when models fail.

### Tests

The test set is broad, but that is valuable for audio behavior. The plugin has many timing edge cases; tests are not bloat here.

### Feature Capture

Feature capture is optional, bounded, and off-thread. It supports ML evaluation work and is useful for debugging.

## Modularity Roadmap

### Low-Risk Cleanup

1. Remove confirmed-unused processor fields.
2. Update stale docs.
3. Add a small `BassStepFrame` struct to document the handoff shape.

### Medium-Risk Refactor

1. Replace generative bass plain-array handoff with SPSC queue or double buffer.
2. Extract feature-capture row creation from `drainFeatureQueueAndRunInference()`.
3. Move pattern/bass hold decisions into an `InferenceCoordinator`.

### Higher-Risk Refactor

1. Split `PatternPlayer` into pattern scheduling plus bass renderer.
2. Consolidate ONNX session loading and tensor contract validation.
3. Consider pattern data generation if pattern library grows far beyond the current seven patterns.

## Design Principle For This Project

Prefer small, testable modules only when they make the real-time story easier to reason about. Do not split code just to make files shorter. In audio plugins, a slightly larger file with obvious timing flow can be safer than clever abstraction hiding thread boundaries.

