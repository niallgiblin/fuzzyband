<!-- GSD:project-start source:PROJECT.md -->
## Project

**Metal Accompaniment**

A JUCE 8 VST3/AU plugin for macOS that listens to a guitarist's audio input and outputs rhythmically appropriate drum and bass MIDI in real time. Phase 1 uses a rule-based signal analysis pipeline (onset detection, tempo tracking, energy/structure classification) with no ML. The interface is designed so Phase 2 can drop in an ONNX ML model without changing the threading model or plugin architecture.

**Core Value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.

### Constraints

- **Performance**: End-to-end audio→MIDI latency < 30ms; CPU < 15% on M-series at 256-sample buffer
- **Threading**: Audio thread must never block — lock-free queue + atomics only
- **Stability**: 20-minute session with no crashes or xruns
- **Accuracy**: Tempo within ±5 BPM; pattern switching within ~200ms of energy change
- **Interface**: IInference must remain stable — Phase 2 ONNX model is a drop-in
<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->
## Technology Stack

## Languages
- C++ 20 - Audio plugin implementation, DSP algorithms, inference
- CMake 3.22+ - Build configuration and dependency management
- C++ 20 - Test suites
## Runtime
- macOS (primary development target)
- Linux (secondary support, flags in CMakeLists.txt)
- Windows (secondary support, compiler flags in CMakeLists.txt)
- VST3 plugin (cross-platform) - `build/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3`
- AU (Audio Unit) plugin (macOS only) - `build/MetalAccompaniment_artefacts/Release/AU/Metal Accompaniment.component`
- Standalone executable (optional) - Controlled by `MA_BUILD_STANDALONE` CMake option
## Frameworks
- JUCE 8.0.10 - Audio plugin framework, MIDI handling, GUI, real-time audio processing
- Catch2 3.5.2 - Unit test framework
- ONNX Runtime - ML model inference library (Phase 2, not active in Phase 1)
## Key Dependencies
- moodycamel ReaderWriterQueue - Lock-free single-producer single-consumer queue
- `<atomic>` - Lock-free atomic types for pattern index handoff
- `<memory>` - Smart pointers (`std::unique_ptr`)
- `<thread>` - Background inference thread spawning
- `<vector>` - Dynamic arrays for DSP buffers and pattern storage
- `<array>` - Fixed-size arrays (onset IOI history)
- `<cstdint>` - Fixed-width integer types
- `<string>` - Pattern names and debug output
## Configuration
- No environment variables required at runtime
- Plugin bundle ID: `com.ng.MetalAccompaniment`
- Plugin manufacturer code: `NGAC`
- Plugin code: `MtAc`
- `CMakeLists.txt` - Main build configuration
- C++ Standard: C++20 (enforced via `CMAKE_CXX_STANDARD 20`)
- Build types: Debug, Release
- Optimization flags (all platforms):
- Platform-specific compiler flags in `CMakeLists.txt` lines 178-197
- `MA_BUILD_TESTS` (default: ON) - Build unit test executable
- `MA_BUILD_STANDALONE` (default: ON) - Build standalone app target
- `MA_ENABLE_ONNX` (default: OFF) - Enable ONNX Runtime integration (Phase 2)
- `ONNXRUNTIME_ROOT` - Required when `MA_ENABLE_ONNX=ON`
- ONNX model file: `assets/accompaniment_model.onnx` (bundled via JUCE BinaryData when `MA_ENABLE_ONNX=ON`)
## Platform Requirements
- CMake 3.22+
- C++20 compatible compiler:
- Git (for FetchContent)
- For VST3/AU installation: DAW supporting VST3 or AU (Reaper, Ableton, Logic Pro, etc.)
- DAW with VST3 or AU support
- macOS 10.13+ (for AU plugin)
- Linux kernel 4.0+ (for VST3)
- Windows 7+ (for VST3)
- Audio interface with MIDI output capability
- VST3 directory: `~/Library/Audio/Plug-Ins/VST3/`
- AU directory: `~/Library/Audio/Plug-Ins/`
## Compilation
- `JUCE_WEB_BROWSER=0` - Disable web browser component (not needed)
- `JUCE_USE_CURL=0` - Disable CURL dependency (not needed)
- `JUCE_VST3_CAN_REPLACE_VST2=0` - VST2 compatibility setting
- `MA_ENABLE_ONNX=1` - Conditionally defined when `MA_ENABLE_ONNX=ON`
- Enabled via `CMAKE_EXPORT_COMPILE_COMMANDS ON` for IDE tooling support
## Build Targets
| Target | Output | Purpose |
|---|---|---|
| `MetalAccompaniment` | VST3/AU/Standalone plugin | Main audio plugin |
| `MetalAccompanimentData` | Binary data archive | Bundles ONNX model (Phase 2) |
| `MetalAccompanimentTests` | Executable | Unit test runner (Catch2) |
| `MetalAccompanimentExportPatterns` | Executable | Offline MIDI pattern exporter |
## Build Commands
# Configure Release build with standalone app
# Build all targets
# Run tests
# Run offline pattern exporter
## CI/CD
- Workflow: `.github/workflows/ci.yml`
- Runs on: macOS (GitHub Actions `macos-latest`)
- Steps:
<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->
## Conventions

## Naming Patterns
- Headers use `.h` extension: `OnsetDetector.h`, `AccompanimentProcessor.h`
- Implementation files use `.cpp` extension: `OnsetDetector.cpp`, `AccompanimentProcessor.cpp`
- Class names are PascalCase: `OnsetDetector`, `EnergyAnalyser`, `StructureTagger`
- Struct names are PascalCase: `FeatureVector`, `MidiEvent`, `MidiPattern`
- Implementation classes use PascalCase: `AccompanimentProcessor`, `RuleBasedInference`
- Interface classes start with `I`: `IInference` in `src/inference/IInference.h`
- Final classes explicitly marked with `final` keyword: `class RuleBasedInference final : public IInference`
- Member functions use camelCase: `getCurrentBpm()`, `process()`, `prepare()`, `selectPattern()`
- Boolean getters use `is`/`has` prefixes: `isBusesLayoutSupported()`, `acceptsMidi()`, `producesMidi()`
- Setter functions use `set` prefix: `setBpm()`, `setPatternIndex()`, `setStructureSilent()`
- Private methods start with lowercase: `runFftFrame()`, `medianIoiBpm()`, `pushIoi()`
- Member variables use camelCase: `sampleRate`, `fifo`, `patternLibrary`, `onsetThisBlock`
- Atomic variables explicitly marked: `std::atomic<float> currentBpm`
- Ring buffer indices use descriptive names: `ioiRingWrite`, `ioiRingCount`, `fifoWrite`
- Constants use `k` prefix followed by PascalCase: `kMinBpm`, `kMaxBpm`, `kSilentRms`, `kBreakdownCentroidHz`
- Static constants use `constexpr` keyword: `static constexpr float kMinBpm = 80.0f`
- Enum classes use UPPER_CASE values: `StructureState::SILENT`, `StructureState::VERSE`, `StructureState::CHORUS`
- Type names are PascalCase: `StructureState`, `MidiBuffer`, `AudioBuffer`
## Code Style
- Uses 4-space indentation (inferred from source code)
- Opening braces on same line as declaration: `class OnsetDetector {`
- Closing braces on their own line
- Single-line conditional bodies allowed: `if (got && inference && debugPreviewSamplesRemaining.load(...)) { ... }`
- Header guards use `#pragma once` instead of traditional guards: see all `.h` files
- Standard library includes ordered first: `#include <algorithm>`, `#include <cmath>`
- JUCE framework includes next: `#include <juce_dsp/juce_dsp.h>`, `#include <JuceHeader.h>`
- Third-party includes: `#include "readerwriterqueue.h"` (moodycamel)
- Local includes last, quoted with relative paths: `#include "analysis/OnsetDetector.h"`
- Use `std::unique_ptr` for heap allocation ownership: `std::unique_ptr<juce::dsp::FFT> fft`
- Use `std::make_unique` for initialization: `fft = std::make_unique<juce::dsp::FFT>(fftOrder)`
- No manual `new`/`delete` observed in codebase
- JUCE macro used for leak detection: `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentProcessor)`
## Import Organization
## Error Handling
- Return early on invalid state: `if (in == nullptr) return;`
- Load atomic values with appropriate memory ordering: `currentBpm.load(std::memory_order_relaxed)`
- Validate pointers before use: `if (ph = getPlayHead())` then `if (pos = ph->getPosition())`
- Cast unused parameters explicitly: `(void)maxBlockSize;` in `OnsetDetector::prepare()`
- Use `juce::jmax()` and `juce::jmin()` for bounds checking: `minOnsetIntervalSamples = juce::jmax(1, ...)`
- Real-time audio code avoids exceptions (audio thread cannot allocate)
- All error states handled through early returns and state checks
## Logging
- No explicit logging framework observed
- Code uses simple returns for error cases
- Debug values computed and exposed: `getDisplayBpm()`, `getDisplayStateIndex()`, `getDisplayPatternIndex()`
## Comments
- Sparse use of comments in implementation; code is self-documenting
- Parameters documented in headers: `/** Call once per block with latest analyser outputs. */` in `StructureTagger::update()`
- Magic numbers explained when necessary: threshold values like `fluxThreshold = 0.35f` declared as member variables
- Phase status marked: `// TODO(Phase 2): load ONNX model from BinaryData` in `OnnxInference.cpp`
- JUCE project uses simple C++ documentation style
- Method signatures are self-explanatory
## Function Design
- Complex operations broken into helper methods: `runFftFrame()`, `medianIoiBpm()`
- Examples: `selectPattern()` in `RuleBasedInference.cpp` is 20 lines with clear switch-case structure
- Const references for heavy objects: `const FeatureVector& features`
- Pointers for optional/nullable objects: `const MidiPatternLibrary* lib`
- Value types for primitives: `float bpm`, `int index`
- Output parameters passed as references/pointers: `juce::MidiBuffer& midi`
- Simple return types: `float`, `int`, `bool`
- No output parameters using return values; uses pass-by-reference instead
- Void methods common for state updates: `void prepare()`, `void process()`
## Module Design
- Each module exports single primary class: `OnsetDetector` from `analysis/OnsetDetector.{h,cpp}`
- Supporting types defined in same header: `StructureState` enum in `StructureTagger.h`
- Data structures in dedicated headers: `FeatureVector` in `FeatureVector.h`, `MidiEvent`/`MidiPattern` in `MidiPatternLibrary.h`
- Guards with `#pragma once`
- Includes at top
- Class/type declarations
- Implementation classes include member initializations
- Private sections clearly marked
- `std::atomic<T>` used for cross-thread shared state: `std::atomic<float> currentBpm`
- Memory ordering specified explicitly: `std::memory_order_relaxed`, `std::memory_order_acquire`, `std::memory_order_release`
- Threads managed explicitly: `std::thread inferenceThread` in `AccompanimentProcessor`
- Lock-free queue from moodycamel: `moodycamel::ReaderWriterQueue<FeatureVector>` for audio-to-inference communication
<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->
## Architecture

## Pattern Overview
- Single-producer single-consumer lock-free queue for feature vector handoff
- Audio thread (real-time, hard deadline ~5ms) separate from background inference thread (~50Hz)
- Pluggable inference interface (`IInference`) to support Phase 1 rule-based and Phase 2 ONNX implementations
- No heap allocation on audio thread
- Atomic variables for pattern index synchronization
## Layers
- Purpose: Extract audio features from incoming guitar signal in real-time
- Location: `src/analysis/`
- Contains: `OnsetDetector`, `EnergyAnalyser`, `StructureTagger`
- Depends on: JUCE audio processing library, FFT primitives
- Used by: `AccompanimentProcessor.processBlock()` on audio thread
- Purpose: Plain data structure for passing analysis results to inference thread
- Location: `src/analysis/FeatureVector.h`
- Contains: BPM, RMS energy, spectral centroid, high-frequency flux, structural state
- Depends on: `StructureState` enum
- Used by: Inference layer via lock-free queue
- Purpose: Map audio features to pattern selection (rule-based in Phase 1, ML in Phase 2)
- Location: `src/inference/`
- Contains: `IInference` interface, `RuleBasedInference` implementation, `OnnxInference` stub
- Depends on: `FeatureVector`
- Used by: `AccompanimentProcessor.inferenceLoop()` on background thread
- Purpose: Convert pattern indices and timing to MIDI note events on drum (ch. 10) and bass (ch. 2) tracks
- Location: `src/midi/`
- Contains: `MidiPatternLibrary`, `PatternPlayer`
- Depends on: JUCE MIDI buffer, pattern library
- Used by: `AccompanimentProcessor.processBlock()` on audio thread
- Purpose: Interface between DAW and plugin components; manage threading and state
- Location: `src/AccompanimentProcessor.h/.cpp`, `src/AccompanimentEditor.h/.cpp`
- Contains: Audio processor implementation, GUI editor, parameter state
- Depends on: JUCE `AudioProcessor` base class
- Used by: DAW host
## Data Flow
- **Atomic variables:** `latestPatternIndex` (pattern selection), `currentBpm` (BPM display), `displayStateIndex` (structural state display), `displayPatternIndex` (pattern display)
- **Lock-free queue:** Feature vector handoff (`moodycamel::ReaderWriterQueue`)
- **Local mutable state:** BPM history, FFT buffers, beat position tracking in `PatternPlayer`
- **JUCE APVTS:** Plugin parameter state (saved/loaded with session)
## Key Abstractions
- Purpose: Estimates current BPM via spectral flux peak detection and inter-onset interval analysis
- Location: `src/analysis/OnsetDetector.h/.cpp`
- Pattern: State machine tracking FFT frame buffer, onset history ring buffer, inter-onset interval smoothing
- Public interface: `process()` (audio thread), `getCurrentBpm()` (atomic read)
- Purpose: Extracts three complementary audio descriptors for state classification
- Location: `src/analysis/EnergyAnalyser.h/.cpp`
- Pattern: Rolling window RMS, spectral moment computation, high-frequency band energy tracking
- Public interface: `process()` (audio thread), accessors for RMS/centroid/flux
- Purpose: Converts continuous features into discrete structural state with hysteresis
- Location: `src/analysis/StructureTagger.h/.cpp`
- Pattern: Threshold-based state machine with 2-second minimum hold time to prevent flickering
- Public interface: `update()` returns current state, `getCurrentState()` accessor
- Purpose: Beat-aligned MIDI event emission with humanization
- Location: `src/midi/PatternPlayer.h/.cpp`
- Pattern: Tracks beat position derived from BPM and sample counter; emits note-on/off events quantized to bar boundaries
- Public interface: `process()` fills MIDI buffer, setters for BPM/pattern/silence state
- Purpose: Decouples inference implementation from audio processing
- Location: `src/inference/IInference.h`
- Pattern: Virtual methods `prepare()`, `selectPattern()`, `getName()`
- Design benefit: Phase 2 ONNX implementation can be swapped without modifying audio thread code
## Entry Points
- Location: `src/AccompanimentProcessor.cpp` constructor
- Triggers: DAW instantiates plugin
- Responsibilities: Create components, start background inference thread, initialize lock-free queue
- Location: `src/AccompanimentProcessor::processBlock()`
- Triggers: DAW audio thread at ~48kHz (256-4096 samples per block)
- Responsibilities: Call analysis components, push features to queue, pull pattern from atomic, fill MIDI buffer
- Location: `src/AccompanimentProcessor::inferenceLoop()` (runs on `std::thread`)
- Triggers: Background thread created at startup, runs continuously at ~50Hz
- Responsibilities: Pop feature vectors, run inference, update pattern index
- Location: `src/AccompanimentEditor.cpp`
- Triggers: DAW opens plugin UI or user clicks "Next Pattern (preview 5s)" button
- Responsibilities: Display BPM/state/pattern, handle debug pattern preview
## Error Handling
- **Lock-free queue overflow:** `featureQueue.try_enqueue()` drops feature if queue full; inference uses last-known state
- **Missing pattern:** `getPattern()` clamps index to valid range [0, patternCount-1]
- **Inference timeout:** Background thread sleeps 20ms regardless; does not block audio thread
- **Invalid FFT state:** `OnsetDetector` maintains safe defaults (120 BPM) if insufficient data accumulated
- **Structural state transitions:** Hysteresis ensures minimum 2-second hold to prevent oscillation
## Cross-Cutting Concerns
- `prepare()` methods verify sample rate and block size are non-zero
- `StructureTagger::update()` clamps RMS and centroid to valid ranges
- `PatternPlayer` asserts library is non-null before use
- Audio thread: uses atomic loads only, no blocking primitives
- Background thread: uses atomic stores only, sleeps between iterations
- Handoff: lock-free queue for feature vectors, atomic integer for pattern index
- No mutexes anywhere in real-time critical path
<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->
## Project Skills

No project skills found. Add skills to any of: `.claude/skills/`, `.agents/skills/`, `.cursor/skills/`, or `.github/skills/` with a `SKILL.md` index file.
<!-- GSD:skills-end -->

## Version Bumping Rule

**Every time a new build is made for testing/debugging, bump the patch version in `CMakeLists.txt` line 4 before building.**

- Format: `project(MetalAccompaniment VERSION X.Y.Z)`
- Current: `0.3.1` — increment the patch number (Z) for each debug/test build
- Milestone releases get a minor bump (Y) when a milestone is completed
- The version shows in the plugin UI top-right as `vX.Y.Z` — this is the primary way to confirm the correct build is loaded in the DAW
- **Always rebuild after bumping** so the new version string is baked into the plugin binary

<!-- GSD:workflow-start source:GSD defaults -->
## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:
- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->



<!-- GSD:profile-start -->
## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
