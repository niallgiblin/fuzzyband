# Testing Patterns

**Analysis Date:** 2026-04-16

## Test Framework

**Runner:**
- Catch2 v3.5.2
- Config: `CMakeLists.txt` lines 46-54
- Integrated via CMake: `FetchContent_Declare` and `FetchContent_MakeAvailable`

**Assertion Library:**
- Catch2 built-in assertions via `#include <catch2/catch_test_macros.hpp>`

**Run Commands:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure --config Release
```

**Build Target:**
- Test executable: `MetalAccompanimentTests` defined in `CMakeLists.txt` lines 237-271
- Enabled by CMake option: `option(MA_BUILD_TESTS "Build unit tests" ON)`
- Automatically discovered via `catch_discover_tests(MetalAccompanimentTests)`

## Test File Organization

**Location:**
- Co-located in separate `tests/` directory at project root
- Not integrated into source tree

**Naming:**
- Prefix: `test_`
- Followed by module name: `test_onset_detector.cpp`, `test_pattern_player.cpp`, `test_rule_based_inference.cpp`, `test_structure_tagger.cpp`

**Structure:**
```
tests/
├── test_onset_detector.cpp
├── test_pattern_player.cpp
├── test_rule_based_inference.cpp
└── test_structure_tagger.cpp
```

**Test Coverage Areas:**
- `test_onset_detector.cpp`: Onset/tempo detection analysis (line 1-33)
- `test_rule_based_inference.cpp`: Inference pattern selection logic (line 1-31)
- `test_pattern_player.cpp`: MIDI pattern playback (line 1-20)
- `test_structure_tagger.cpp`: Audio structure state machine (line 1-19)

## Test Structure

**Suite Organization:**
```cpp
#include <catch2/catch_test_macros.hpp>
#include "analysis/OnsetDetector.h"

TEST_CASE("OnsetDetector converges near 160 BPM on synthetic impulse train", "[onset]")
{
    OnsetDetector detector;
    const double sr = 44100.0;
    detector.prepare(sr, 512);
    
    // ... test body ...
    
    const float bpm = detector.getCurrentBpm();
    REQUIRE(bpm > 150.0f);
    REQUIRE(bpm < 170.0f);
}
```

**Key Patterns:**
- Single TEST_CASE per test file (no nested suites observed)
- Descriptive test name describing behavior: "converges near 160 BPM on synthetic impulse train"
- Tags in square brackets for categorization: `[onset]`, `[inference]`, `[midi]`, `[structure]`
- Instance variables initialized locally within test
- Assertions at end using `REQUIRE` macro

**Setup Pattern:**
- No shared fixtures; each test creates its own instances
- Direct instantiation: `OnsetDetector detector;`
- Call `prepare()` or equivalent initialization before test: `detector.prepare(sr, 512);`

**Teardown Pattern:**
- Implicit: stack-allocated objects destroyed at test end
- JUCE leak detector macro active via `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`

**Assertion Pattern:**
```cpp
REQUIRE(bpm > 150.0f);      // Lower bound
REQUIRE(bpm < 170.0f);      // Upper bound
REQUIRE(inf.selectPattern(f) == 0);  // Equality
REQUIRE(midi.getNumEvents() > 0);    // Comparison
```

## Compilation and Linking

**Test Target Setup (CMakeLists.txt lines 237-266):**
```cmake
add_executable(MetalAccompanimentTests
    tests/test_onset_detector.cpp
    tests/test_structure_tagger.cpp
    tests/test_pattern_player.cpp
    tests/test_rule_based_inference.cpp

    # Include plugin sources needed by tests
    src/analysis/OnsetDetector.cpp
    src/analysis/EnergyAnalyser.cpp
    src/analysis/StructureTagger.cpp
    src/inference/RuleBasedInference.cpp
    src/midi/MidiPatternLibrary.cpp
    src/midi/PatternPlayer.cpp
)

target_include_directories(MetalAccompanimentTests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${moodycamel_SOURCE_DIR}"
)

target_compile_definitions(MetalAccompanimentTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(MetalAccompanimentTests PRIVATE
    Catch2::Catch2WithMain
    ${JUCE_MODULES}
)
```

**Key Integration Points:**
- Test executable links selected source files (not full plugin)
- Uses `Catch2::Catch2WithMain` - includes main() function automatically
- Includes full JUCE module set for audio testing utilities
- Same compiler flags and compile definitions as plugin target

## What Gets Tested

**Unit Test Scope:**
- Individual analyzers: `OnsetDetector`, `StructureTagger`
- Inference logic: `RuleBasedInference` state-to-pattern mapping
- MIDI playback: `PatternPlayer` event generation
- Audio feature processing: real-time analysis on synthetic signals

**Test Data:**
```cpp
// Synthetic impulse train (test_onset_detector.cpp lines 12-16)
const float bpmTarget = 160.0f;
const int periodSamples = static_cast<int>(
    std::round(static_cast<double>(sr) * 60.0 / static_cast<double>(bpmTarget))
);
const int chunk = 256;
const int totalSamples = periodSamples * 24;  // 24 beats
```

**No Test Fixtures/Factories:**
- Each test builds its own test data inline
- Deterministic synthetic signals: impulse trains at specific BPM
- State objects created fresh: `RuleBasedInference inf;`

## Real-Time Audio Integration

**Special Considerations:**
- Tests work with real JUCE audio types: `juce::MidiBuffer`, `juce::AudioBuffer`
- Block-based processing tested: `detector.process(buf.data(), chunk)` with 256-sample blocks
- Lock-free queue not tested (inference thread not spawned in tests)
- No timing tests; relative accuracy tested instead: BPM within ±10 range

## Coverage

**Requirements:** None enforced - no coverage tool configuration found

**Coverage Status (inferred):**
- Core analysis modules: Good (OnsetDetector, StructureTagger, RuleBasedInference all have tests)
- MIDI generation: Basic (PatternPlayer tested for event generation, not all patterns)
- Editor/UI: Not tested (plugin UI in `AccompanimentEditor.cpp` not covered)
- Threading/sync: Not tested (inference thread behavior in `AccompanimentProcessor` not tested)
- ONNX inference: Not tested (Phase 2 stub in `src/inference/OnnxInference.cpp`)

## Test Types

**Unit Tests:**
- Self-contained tests of individual modules
- Use synthetic/deterministic input
- Verify output accuracy within tolerance (e.g., BPM detection within 150-170 range)
- Example: `test_onset_detector.cpp` - feed impulse train, verify BPM convergence

**Integration Tests:**
- None explicitly defined; tests focus on unit module behavior
- JUCE integration implicit: code uses real `juce::MidiBuffer`, `AudioProcessor` interfaces

**E2E Tests:**
- Not implemented
- Would require full plugin host or standalone executable

## Common Patterns Observed

**Synthetic Signal Generation:**
```cpp
// test_onset_detector.cpp lines 18-28
for (int pos = 0; pos < totalSamples; pos += chunk)
{
    std::vector<float> buf(static_cast<size_t>(chunk), 0.0f);
    for (int i = 0; i < chunk; ++i)
    {
        const int g = pos + i;
        if (g % periodSamples == 0)
            buf[static_cast<size_t>(i)] = 1.0f;  // Impulse at beat
    }
    detector.process(buf.data(), chunk);
}
```

**State Testing (StructureTagger):**
```cpp
// test_structure_tagger.cpp lines 4-19
StructureTagger tagger;
tagger.prepare(44100.0);

StructureState s = tagger.update(0.2f, 900.0f, 0.0f, 512);
REQUIRE(s == StructureState::BREAKDOWN);

s = tagger.update(0.01f, 900.0f, 0.0f, 512);  // Hysteresis holds state
REQUIRE(s == StructureState::BREAKDOWN);

for (int i = 0; i < 200; ++i)  // Accumulate time to transition
    s = tagger.update(0.01f, 1500.0f, 0.0f, 512);

REQUIRE(s == StructureState::SILENT);
```

**MIDI Event Verification:**
```cpp
// test_pattern_player.cpp lines 6-20
PatternPlayer player;
player.setPatternLibrary(&lib);
player.prepare(48000.0, 512);
player.setBpm(120.0f);
player.setPatternIndex(1);
player.setStructureSilent(false);

juce::MidiBuffer midi;
player.process(midi, 4800, 0);

REQUIRE(midi.getNumEvents() > 0);  // Verify MIDI generated
```

---

*Testing analysis: 2026-04-16*
