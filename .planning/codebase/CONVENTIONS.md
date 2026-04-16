# Coding Conventions

**Analysis Date:** 2026-04-16

## Naming Patterns

**Files:**
- Headers use `.h` extension: `OnsetDetector.h`, `AccompanimentProcessor.h`
- Implementation files use `.cpp` extension: `OnsetDetector.cpp`, `AccompanimentProcessor.cpp`
- Class names are PascalCase: `OnsetDetector`, `EnergyAnalyser`, `StructureTagger`
- Struct names are PascalCase: `FeatureVector`, `MidiEvent`, `MidiPattern`

**Classes:**
- Implementation classes use PascalCase: `AccompanimentProcessor`, `RuleBasedInference`
- Interface classes start with `I`: `IInference` in `src/inference/IInference.h`
- Final classes explicitly marked with `final` keyword: `class RuleBasedInference final : public IInference`

**Functions and Methods:**
- Member functions use camelCase: `getCurrentBpm()`, `process()`, `prepare()`, `selectPattern()`
- Boolean getters use `is`/`has` prefixes: `isBusesLayoutSupported()`, `acceptsMidi()`, `producesMidi()`
- Setter functions use `set` prefix: `setBpm()`, `setPatternIndex()`, `setStructureSilent()`
- Private methods start with lowercase: `runFftFrame()`, `medianIoiBpm()`, `pushIoi()`

**Variables:**
- Member variables use camelCase: `sampleRate`, `fifo`, `patternLibrary`, `onsetThisBlock`
- Atomic variables explicitly marked: `std::atomic<float> currentBpm`
- Ring buffer indices use descriptive names: `ioiRingWrite`, `ioiRingCount`, `fifoWrite`
- Constants use `k` prefix followed by PascalCase: `kMinBpm`, `kMaxBpm`, `kSilentRms`, `kBreakdownCentroidHz`
- Static constants use `constexpr` keyword: `static constexpr float kMinBpm = 80.0f`

**Types and Enums:**
- Enum classes use UPPER_CASE values: `StructureState::SILENT`, `StructureState::VERSE`, `StructureState::CHORUS`
- Type names are PascalCase: `StructureState`, `MidiBuffer`, `AudioBuffer`

## Code Style

**Formatting:**
- Uses 4-space indentation (inferred from source code)
- Opening braces on same line as declaration: `class OnsetDetector {`
- Closing braces on their own line
- Single-line conditional bodies allowed: `if (got && inference && debugPreviewSamplesRemaining.load(...)) { ... }`

**Includes:**
- Header guards use `#pragma once` instead of traditional guards: see all `.h` files
- Standard library includes ordered first: `#include <algorithm>`, `#include <cmath>`
- JUCE framework includes next: `#include <juce_dsp/juce_dsp.h>`, `#include <JuceHeader.h>`
- Third-party includes: `#include "readerwriterqueue.h"` (moodycamel)
- Local includes last, quoted with relative paths: `#include "analysis/OnsetDetector.h"`

**Memory Management:**
- Use `std::unique_ptr` for heap allocation ownership: `std::unique_ptr<juce::dsp::FFT> fft`
- Use `std::make_unique` for initialization: `fft = std::make_unique<juce::dsp::FFT>(fftOrder)`
- No manual `new`/`delete` observed in codebase
- JUCE macro used for leak detection: `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentProcessor)`

## Import Organization

**Order (observed in AccompanimentProcessor.h):**
1. Standard library: `<atomic>`, `<memory>`, `<thread>`
2. Framework headers: `<JuceHeader.h>`, `<juce_dsp/juce_dsp.h>`
3. Project includes: `"analysis/OnsetDetector.h"`, `"inference/RuleBasedInference.h"`
4. Third-party: `"readerwriterqueue.h"`

## Error Handling

**Pattern:** Defensive checks rather than exceptions
- Return early on invalid state: `if (in == nullptr) return;`
- Load atomic values with appropriate memory ordering: `currentBpm.load(std::memory_order_relaxed)`
- Validate pointers before use: `if (ph = getPlayHead())` then `if (pos = ph->getPosition())`
- Cast unused parameters explicitly: `(void)maxBlockSize;` in `OnsetDetector::prepare()`
- Use `juce::jmax()` and `juce::jmin()` for bounds checking: `minOnsetIntervalSamples = juce::jmax(1, ...)`

**No Exceptions:**
- Real-time audio code avoids exceptions (audio thread cannot allocate)
- All error states handled through early returns and state checks

## Logging

**Framework:** Console output and JUCE logging
- No explicit logging framework observed
- Code uses simple returns for error cases
- Debug values computed and exposed: `getDisplayBpm()`, `getDisplayStateIndex()`, `getDisplayPatternIndex()`

## Comments

**When to Comment:**
- Sparse use of comments in implementation; code is self-documenting
- Parameters documented in headers: `/** Call once per block with latest analyser outputs. */` in `StructureTagger::update()`
- Magic numbers explained when necessary: threshold values like `fluxThreshold = 0.35f` declared as member variables
- Phase status marked: `// TODO(Phase 2): load ONNX model from BinaryData` in `OnnxInference.cpp`

**No JSDoc:**
- JUCE project uses simple C++ documentation style
- Method signatures are self-explanatory

## Function Design

**Size:** Functions are concise, typically 20-50 lines
- Complex operations broken into helper methods: `runFftFrame()`, `medianIoiBpm()`
- Examples: `selectPattern()` in `RuleBasedInference.cpp` is 20 lines with clear switch-case structure

**Parameters:**
- Const references for heavy objects: `const FeatureVector& features`
- Pointers for optional/nullable objects: `const MidiPatternLibrary* lib`
- Value types for primitives: `float bpm`, `int index`
- Output parameters passed as references/pointers: `juce::MidiBuffer& midi`

**Return Values:**
- Simple return types: `float`, `int`, `bool`
- No output parameters using return values; uses pass-by-reference instead
- Void methods common for state updates: `void prepare()`, `void process()`

## Module Design

**Exports:**
- Each module exports single primary class: `OnsetDetector` from `analysis/OnsetDetector.{h,cpp}`
- Supporting types defined in same header: `StructureState` enum in `StructureTagger.h`
- Data structures in dedicated headers: `FeatureVector` in `FeatureVector.h`, `MidiEvent`/`MidiPattern` in `MidiPatternLibrary.h`

**Barrel Files:** Not used; each file imports what it needs explicitly

**Header Structure:**
- Guards with `#pragma once`
- Includes at top
- Class/type declarations
- Implementation classes include member initializations
- Private sections clearly marked

**Atomics and Threading:**
- `std::atomic<T>` used for cross-thread shared state: `std::atomic<float> currentBpm`
- Memory ordering specified explicitly: `std::memory_order_relaxed`, `std::memory_order_acquire`, `std::memory_order_release`
- Threads managed explicitly: `std::thread inferenceThread` in `AccompanimentProcessor`
- Lock-free queue from moodycamel: `moodycamel::ReaderWriterQueue<FeatureVector>` for audio-to-inference communication

---

*Convention analysis: 2026-04-16*
