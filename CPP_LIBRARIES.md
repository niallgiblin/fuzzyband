# C++ Libraries in Metal Accompaniment

This project is a JUCE audio plugin with a small set of third-party dependencies. All external C++ libraries are fetched at configure time via CMake `FetchContent` (except ONNX Runtime, which is installed separately when enabled).

## Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  DAW host                                                       │
└───────────────────────────┬─────────────────────────────────────┘
                            │ VST3 / AU / Standalone
┌───────────────────────────▼─────────────────────────────────────┐
│  JUCE 8 — plugin shell, audio I/O, GUI, DSP (FFT), MIDI          │
│  ├─ Audio thread: analysis + PatternPlayer → MidiBuffer         │
│  └─ Background thread: inference (rules or ONNX)                │
│       ▲                                                           │
│       │ moodycamel ReaderWriterQueue (lock-free)                  │
│       │ std::atomic (pattern index, BPM, UI display)              │
│  ONNX Runtime (optional) — pattern / structure / bass models    │
└─────────────────────────────────────────────────────────────────┘

Catch2 — unit, integration, and E2E tests only (not linked into plugin)
```

| Library | Version | Linked into plugin | Role |
|---------|---------|-------------------|------|
| [JUCE](https://juce.com/) | 8.0.10 | Yes | Framework: plugin formats, audio/MIDI, GUI, FFT |
| [moodycamel ReaderWriterQueue](https://github.com/cameron314/readerwriterqueue) | `master` | Yes (header-only) | Lock-free audio → inference handoff |
| [ONNX Runtime](https://onnxruntime.ai/) | External install | Optional (`MA_ENABLE_ONNX=ON`) | ML inference for pattern, structure, bass |
| [Catch2](https://github.com/catchorg/Catch2) | 3.5.2 | Tests only | Test runner and assertions |

The C++ standard library (`<atomic>`, `<thread>`, `<mutex>`, containers) is used throughout for threading, RT-safe state, and buffers. Pitch estimation (YIN) and beat tracking are implemented in-project with no extra DSP library.

---

## JUCE 8

**What it is:** Cross-platform C++ framework for audio applications and plugins. CMake pulls it from GitHub (`GIT_TAG 8.0.10`) and links a curated set of modules.

**How this project uses it:**

### Plugin lifecycle and hosting

- `juce_add_plugin()` in `CMakeLists.txt` defines the **Metal Accompaniment** target as VST3, AU (macOS), and optionally Standalone.
- `AccompanimentProcessor` subclasses `juce::AudioProcessor` — the DAW calls `prepareToPlay()` / `processBlock()` on the real-time audio thread.
- `AccompanimentEditor` subclasses `juce::AudioProcessorEditor` and drives UI refresh with `juce::Timer`.
- Plugin parameters (intensity, structure blend, generative bass mode) use `juce::AudioProcessorValueTreeState` (APVTS) with slider/combo attachments in the editor. State is saved/restored through `getStateInformation()` / `setStateInformation()`.

### Audio and MIDI

- **`juce::AudioBuffer<float>`** — mono/stereo guitar input and pass-through in `processBlock()`.
- **`juce::MidiBuffer`** — drum (channel 10) and bass (channel 2) note events from `PatternPlayer`.
- **`juce::FloatVectorOperations`** — SIMD-friendly sample clipping on the audio thread.
- **`juce::MidiMessage` / `juce::MidiFile`** — offline pattern export in `MetalAccompanimentExportPatterns` (`tools/export_patterns.cpp`).

### DSP (spectral analysis)

- **`juce::dsp::FFT`** — real FFT used by `OnsetDetector` (spectral flux for onsets/tempo) and `EnergyAnalyser` (RMS, spectral centroid, high-frequency flux for structure tagging).
- Other analysis modules (`BeatTracker`, `PitchEstimator`, etc.) use JUCE math helpers (`juce::jlimit`, `juce::MathConstants`) but implement their own algorithms without additional libraries.

### GUI

- **`juce_gui_basics`** — `Label`, `Slider`, `ComboBox`, `TextButton`, custom `LookAndFeel_V4` in `AccompanimentEditor`.
- **`juce_graphics`** — custom painting for sliders, combo boxes, and the dark green “fuzzyband” theme.

### Bundled assets (BinaryData)

When `MA_ENABLE_ONNX=ON`, `juce_add_binary_data(MetalAccompanimentData …)` embeds `.onnx` files from `assets/` into the plugin binary. ONNX loaders read model bytes via `BinaryData::accompaniment_model_onnx` (and sibling symbols) — no filesystem access at runtime.

### Compile flags

JUCE features not needed are disabled globally: `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`, `JUCE_VST3_CAN_REPLACE_VST2=0`.

### JUCE modules linked

| Module | Primary use in this repo |
|--------|-------------------------|
| `juce_core` | Strings, timing, binary streams |
| `juce_audio_basics` | Buffers, MIDI messages, MIDI file export |
| `juce_audio_processors` | `AudioProcessor`, APVTS, plugin metadata |
| `juce_audio_plugin_client` | VST3/AU/Standalone entry points |
| `juce_audio_devices` / `juce_audio_utils` | Standalone I/O (when built) |
| `juce_audio_formats` | Available via JUCE stack; export tool uses MIDI types from audio_basics |
| `juce_dsp` | FFT for onset and energy analysis |
| `juce_events` | Editor timer |
| `juce_graphics` / `juce_gui_basics` / `juce_gui_extra` | Plugin UI |

---

## moodycamel ReaderWriterQueue

**What it is:** Header-only, lock-free **single-producer / single-consumer** queue. Included from the FetchContent checkout; no separate link step.

**Why it is used:** The audio thread must never block on mutexes or allocation. Feature vectors and bass step frames are pushed from `processBlock()`; a background inference thread pops them. If the queue is full, `try_enqueue()` fails and the sample is dropped rather than blocking.

**Where it appears:**

| Queue | Location | Producer | Consumer | Payload |
|-------|----------|----------|----------|---------|
| `featureQueue` | `AccompanimentProcessor` | Audio thread | Inference thread | `FeatureVector` (BPM, RMS, centroid, structure, etc.) |
| `bassStepQueue` | `AccompanimentProcessor` | Inference thread | Audio thread | `BassStepFrame` (generative bass ONNX output) |
| `queue` | `FeatureCapture` | Audio thread | Capture/export path | `FeatureCaptureRow` (debug/training JSONL) |

Typical API: `try_enqueue()` on the producer side, `try_dequeue()` in a drain loop on the consumer side. Capacity is fixed at construction (e.g. 4096 for features, 32 for bass steps).

**Complement:** Pattern selection and UI telemetry use **`std::atomic`** loads/stores (`latestPatternIndex`, `displayBpm`, etc.) so the audio thread never waits on inference.

---

## ONNX Runtime (optional, Phase 2)

**What it is:** Microsoft’s cross-platform inference engine for ONNX models. **Not** fetched by CMake — you download a prebuilt package and pass `-DONNXRUNTIME_ROOT=…` when configuring with `-DMA_ENABLE_ONNX=ON`.

**How this project uses it:**

- C++ API: `#include <onnxruntime_cxx_api.h>` — `Ort::Env`, `Ort::Session`, `Ort::Value` tensors.
- Models load from **JUCE BinaryData** memory (not disk paths).
- Each inference class owns one session, runs on the **background thread** (never in `processBlock()`):
  - **`OnnxInference`** — drum/pattern index from `accompaniment_model.onnx`
  - **`OnnxStructureInference`** — structure class from `structure_model.onnx`
  - **`OnnxBassInference`** — generative bass steps from `bass_model.onnx`
- Sessions use single-threaded op settings (`SetIntraOpNumThreads(1)`) suitable for a low-priority worker thread.
- All ONNX code is guarded by `#if defined(MA_ENABLE_ONNX)`. With the flag off (default CI build), stubs remain and **`RuleBasedInference`** / **`RuleStructureInference`** handle pattern and structure instead.

See `docs/ONNX_IO.md`, `docs/BASS_ONNX_IO.md`, and `CONTRIBUTING.md` for tensor contracts and setup.

---

## Catch2

**What it is:** Modern C++ test framework (v3). Fetched only when `MA_BUILD_TESTS=ON`.

**How this project uses it:**

- **`MetalAccompanimentTests`** — unit tests; compiles plugin source modules directly and links `Catch2::Catch2WithMain` plus core JUCE modules for buffers/MIDI.
- **`MetalAccompanimentIntegrationTests`** — links the full `MetalAccompaniment` plugin target for pipeline, shadow, and E2E scenarios.
- CMake `catch_discover_tests()` registers each `TEST_CASE` with CTest labels (`unit`, `integration`, `e2e`).

Catch2 is never linked into the shipping plugin binary.

---

## Minimal JUCE in offline tools

**`MetalAccompanimentExportPatterns`** links only `juce::juce_core` and `juce::juce_audio_basics` to write a single `.mid` file auditioning all patterns from `MidiPatternLibrary` — no plugin processor, GUI, or ONNX.

---

## Build reference

```bash
# Default: JUCE + moodycamel + Catch2; rule-based inference only
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Optional ONNX
cmake -B build-onnx -DCMAKE_BUILD_TYPE=Release \
  -DMA_ENABLE_ONNX=ON \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build build-onnx
```

Dependency declarations and link lines live in **`CMakeLists.txt`** (JUCE/moodycamel/Catch2 via `FetchContent`; ONNX via `ONNXRUNTIME_ROOT`).
