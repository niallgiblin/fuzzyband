# Codebase Structure

**Analysis Date:** 2026-04-16

## Directory Layout

```
/Users/ng/Desktop/fuzzyband/
├── src/                           # Plugin source code
│   ├── AccompanimentProcessor.h/.cpp    # Plugin entry point, JUCE AudioProcessor
│   ├── AccompanimentEditor.h/.cpp       # GUI editor
│   ├── analysis/                  # Audio feature extraction
│   │   ├── OnsetDetector.h/.cpp
│   │   ├── EnergyAnalyser.h/.cpp
│   │   ├── StructureTagger.h/.cpp
│   │   └── FeatureVector.h        # Data struct for feature handoff
│   ├── inference/                 # Pattern selection logic
│   │   ├── IInference.h           # Abstract interface
│   │   ├── RuleBasedInference.h/.cpp  # Phase 1 implementation
│   │   └── OnnxInference.h/.cpp   # Phase 2 stub
│   └── midi/                      # MIDI generation
│       ├── MidiPatternLibrary.h/.cpp   # Pattern definitions
│       └── PatternPlayer.h/.cpp   # Beat-aligned MIDI emission
├── tests/                         # Unit tests (Catch2)
│   ├── test_onset_detector.cpp
│   ├── test_structure_tagger.cpp
│   ├── test_pattern_player.cpp
│   └── test_rule_based_inference.cpp
├── tools/                         # Utility executables
│   └── export_patterns.cpp        # CLI tool to export patterns as MIDI files
├── CMakeLists.txt                 # Build configuration
├── ARCHITECTURE.md                # Component boundaries and threading model
└── README.md                       # User documentation
```

## Directory Purposes

**`src/`:**
- Purpose: All plugin source code (audio processing, MIDI generation, GUI)
- Contains: C++ headers and implementations
- Key files: `AccompanimentProcessor.h/.cpp` (plugin controller)

**`src/analysis/`:**
- Purpose: Extract audio features from incoming guitar signal
- Contains: Feature detection classes and data structures
- Key files: 
  - `OnsetDetector.h/.cpp` (BPM estimation via spectral flux)
  - `EnergyAnalyser.h/.cpp` (RMS, spectral centroid, high-freq flux)
  - `StructureTagger.h/.cpp` (state machine: SILENT/VERSE/CHORUS/BREAKDOWN)
  - `FeatureVector.h` (struct passed to inference thread)

**`src/inference/`:**
- Purpose: Pluggable pattern selection logic (rule-based in Phase 1, ONNX in Phase 2)
- Contains: Interface and implementations
- Key files:
  - `IInference.h` (abstract interface for swappable implementations)
  - `RuleBasedInference.h/.cpp` (threshold-based decision rules)
  - `OnnxInference.h/.cpp` (ONNX Runtime stub, compiled but unused in Phase 1)

**`src/midi/`:**
- Purpose: Convert patterns to MIDI note events
- Contains: Pattern library and playback logic
- Key files:
  - `MidiPatternLibrary.h/.cpp` (7 hardcoded drum/bass pattern definitions)
  - `PatternPlayer.h/.cpp` (beat-aligned MIDI event generation with humanization)

**`tests/`:**
- Purpose: Unit test suite (Catch2 framework)
- Contains: Test executables for core components
- Key files:
  - `test_onset_detector.cpp` (BPM convergence tests)
  - `test_structure_tagger.cpp` (state transition logic)
  - `test_pattern_player.cpp` (MIDI timing and quantization)
  - `test_rule_based_inference.cpp` (pattern selection rules)

**`tools/`:**
- Purpose: Standalone utilities
- Contains: CLI tools not part of plugin binary
- Key files:
  - `export_patterns.cpp` (utility to render all 7 patterns as MIDI files for auditioning)

## Key File Locations

**Entry Points:**
- `src/AccompanimentProcessor.h`: JUCE `AudioProcessor` subclass — plugin initialization, `processBlock()` audio thread entry, background inference thread spawn
- `src/AccompanimentEditor.h`: JUCE `AudioProcessorEditor` subclass — GUI display and debug button handling
- `tools/export_patterns.cpp`: Standalone pattern MIDI exporter

**Configuration:**
- `CMakeLists.txt`: Build system (C++20, CMake 3.22+, JUCE 8.0.10, Catch2 3.5.2)
- `.env` files: Plugin metadata (name, version, bundle ID, MIDI channels) — defined in CMakeLists.txt via `juce_add_plugin()`

**Core Logic:**
- `src/analysis/OnsetDetector.h/.cpp`: FFT-based BPM tracking (11-bit FFT, 2048 bin)
- `src/analysis/EnergyAnalyser.h/.cpp`: RMS and spectral feature extraction
- `src/analysis/StructureTagger.h/.cpp`: Discrete state machine with hysteresis
- `src/inference/RuleBasedInference.h/.cpp`: 7-pattern decision tree based on BPM and state
- `src/midi/MidiPatternLibrary.h/.cpp`: Drum (ch. 10) and bass (ch. 2) note events
- `src/midi/PatternPlayer.h/.cpp`: Beat clock and MIDI event emission with ±10 velocity, ±2ms timing humanization

**Data Structures:**
- `src/analysis/FeatureVector.h`: Float BPM, RMS, centroid; enum state; int64_t timestamp
- `src/inference/IInference.h`: Virtual interface for inference implementations
- `src/midi/MidiPatternLibrary.h`: `MidiEvent` struct (note, velocity, beatOffset, durationBeats) and `MidiPattern` struct (name, lengthInBars, drum/bass event vectors)

**Testing:**
- `tests/test_*.cpp`: Catch2 test cases for OnsetDetector, StructureTagger, PatternPlayer, RuleBasedInference
- Built as separate executable `MetalAccompanimentTests` via CMake
- No tests for Editor or AccompanimentProcessor (integration scope)

## Naming Conventions

**Files:**
- Plugin header/impl pairs: `ComponentName.h` / `ComponentName.cpp`
- Data structures: `StructName.h` (no separate `.cpp`)
- Test files: `test_component_name.cpp`
- Tools: `tool_purpose.cpp`

**Directories:**
- Logical layers: `analysis/`, `inference/`, `midi/`
- No single-file directories; each layer contains 2-3 related components

**Classes:**
- PascalCase: `OnsetDetector`, `EnergyAnalyser`, `StructureTagger`, `MidiPatternLibrary`, `PatternPlayer`
- Interfaces prefixed with `I`: `IInference`
- Editor appended: `AccompanimentEditor`

**Functions/Methods:**
- camelCase: `getCurrentBpm()`, `processBlock()`, `selectPattern()`, `emitEventsForRange()`
- Setter/getter pattern: `setBpm()`, `getPattern()`
- Framework hooks: `prepare()`, `process()`, `reset()`

**Enums:**
- PascalCase with scope: `enum class StructureState { SILENT, VERSE, CHORUS, BREAKDOWN }`

**Variables:**
- Member private: `m_` prefix not used; rely on private access
- Atomics: `std::atomic<T>` for `latestPatternIndex`, `currentBpm`
- Constants: `k` prefix for class constants: `kMinBpm`, `kDrumChannel`

## Where to Add New Code

**New Audio Analysis Feature:**
- Primary code: `src/analysis/NewAnalyzer.h/.cpp`
- Update: Add field to `src/analysis/FeatureVector.h`
- Call from: `src/AccompanimentProcessor::processBlock()`
- Test: Create `tests/test_new_analyzer.cpp`

**New Pattern Selection Rule:**
- Implementation: Modify `src/inference/RuleBasedInference::selectPattern()` logic
- Or: For Phase 2 ML, implement in `src/inference/OnnxInference.cpp`
- Test: Add case to `tests/test_rule_based_inference.cpp`

**New MIDI Pattern:**
- Edit: `src/midi/MidiPatternLibrary::patterns` vector in `.cpp` file
- Update: Increment `patternCount()` if adding beyond 7
- Test: Verify via `tools/export_patterns.cpp` MIDI export

**New GUI Parameter:**
- Edit: `src/AccompanimentEditor.cpp` paint() and resized()
- Update: `src/AccompanimentProcessor::createParameterLayout()` and APVTS
- Test: Manual inspection in DAW

**New Utility Tool:**
- Location: `tools/new_tool_name.cpp`
- Build: Add `add_executable()` block to CMakeLists.txt
- Link: Against `juce::juce_core` and relevant JUCE modules

## Special Directories

**`build/`:**
- Purpose: CMake build output (generated)
- Generated: Yes
- Committed: No (in .gitignore)

**`CMakeFiles/`:**
- Purpose: CMake internal state
- Generated: Yes
- Committed: No

**`.planning/codebase/`:**
- Purpose: GSD codebase analysis documents
- Generated: Yes (by `/gsd-map-codebase`)
- Committed: No (in .gitignore)

**`assets/`:**
- Purpose: Future ONNX model and plugin resources
- Generated: No (user-provided for Phase 2)
- Committed: No (referenced in CMakeLists.txt only)

---

*Structure analysis: 2026-04-16*
