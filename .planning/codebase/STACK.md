# Technology Stack

**Analysis Date:** 2026-04-16

## Languages

**Primary:**
- C++ 20 - Audio plugin implementation, DSP algorithms, inference
- CMake 3.22+ - Build configuration and dependency management

**Testing:**
- C++ 20 - Test suites

## Runtime

**Environment:**
- macOS (primary development target)
- Linux (secondary support, flags in CMakeLists.txt)
- Windows (secondary support, compiler flags in CMakeLists.txt)

**Deployment:**
- VST3 plugin (cross-platform) - `build/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3`
- AU (Audio Unit) plugin (macOS only) - `build/MetalAccompaniment_artefacts/Release/AU/Metal Accompaniment.component`
- Standalone executable (optional) - Controlled by `MA_BUILD_STANDALONE` CMake option

## Frameworks

**Core Audio:**
- JUCE 8.0.10 - Audio plugin framework, MIDI handling, GUI, real-time audio processing
  - Git-fetched via FetchContent: `https://github.com/juce-framework/JUCE.git` (tag: 8.0.10)
  - Key modules used:
    - `juce::juce_audio_basics` - Core audio data structures
    - `juce::juce_audio_devices` - Hardware I/O
    - `juce::juce_audio_formats` - Audio file format support
    - `juce::juce_audio_plugin_client` - Plugin client interface
    - `juce::juce_audio_processors` - AudioProcessor base class
    - `juce::juce_audio_utils` - Utility functions
    - `juce::juce_core` - Core utilities (strings, files, threading)
    - `juce::juce_dsp` - DSP algorithms (FFT, filters, convolution)
    - `juce::juce_events` - Event handling
    - `juce::juce_graphics` - Graphics rendering
    - `juce::juce_gui_basics` - UI components
    - `juce::juce_gui_extra` - Extended UI components

**Testing:**
- Catch2 3.5.2 - Unit test framework
  - Git-fetched via FetchContent: `https://github.com/catchorg/Catch2.git` (tag: v3.5.2)
  - Only included when `MA_BUILD_TESTS=ON`

**Inference (Phase 2, optional):**
- ONNX Runtime - ML model inference library (Phase 2, not active in Phase 1)
  - Controlled by `MA_ENABLE_ONNX` CMake option (currently OFF)
  - Requires manual setup: `cmake -DMA_ENABLE_ONNX=ON -DONNXRUNTIME_ROOT=/path/to/onnxruntime`
  - Platform-specific binaries:
    - macOS: `libonnxruntime.dylib`
    - Linux: `libonnxruntime.so`
    - Windows: `onnxruntime.lib`

## Key Dependencies

**Critical:**
- moodycamel ReaderWriterQueue - Lock-free single-producer single-consumer queue
  - Git-fetched via FetchContent: `https://github.com/cameron314/readerwriterqueue.git` (branch: master)
  - Header-only library
  - Used for thread-safe feature vector handoff from audio thread to inference thread
  - Location in includes: `"readerwriterqueue.h"` (included in `src/AccompanimentProcessor.h`)

**Standard Library:**
- `<atomic>` - Lock-free atomic types for pattern index handoff
- `<memory>` - Smart pointers (`std::unique_ptr`)
- `<thread>` - Background inference thread spawning
- `<vector>` - Dynamic arrays for DSP buffers and pattern storage
- `<array>` - Fixed-size arrays (onset IOI history)
- `<cstdint>` - Fixed-width integer types
- `<string>` - Pattern names and debug output

## Configuration

**Environment:**
- No environment variables required at runtime
- Plugin bundle ID: `com.ng.MetalAccompaniment`
- Plugin manufacturer code: `NGAC`
- Plugin code: `MtAc`

**Build:**
- `CMakeLists.txt` - Main build configuration
- C++ Standard: C++20 (enforced via `CMAKE_CXX_STANDARD 20`)
- Build types: Debug, Release
- Optimization flags (all platforms):
  - `-ffast-math` (safe for audio DSP, required for FFT performance)
  - `-O3` (Release) / `-O2` (Windows Release)
- Platform-specific compiler flags in `CMakeLists.txt` lines 178-197

**Build Options (CMake):**
- `MA_BUILD_TESTS` (default: ON) - Build unit test executable
- `MA_BUILD_STANDALONE` (default: ON) - Build standalone app target
- `MA_ENABLE_ONNX` (default: OFF) - Enable ONNX Runtime integration (Phase 2)
- `ONNXRUNTIME_ROOT` - Required when `MA_ENABLE_ONNX=ON`

**Binary Assets:**
- ONNX model file: `assets/accompaniment_model.onnx` (bundled via JUCE BinaryData when `MA_ENABLE_ONNX=ON`)

## Platform Requirements

**Development:**
- CMake 3.22+
- C++20 compatible compiler:
  - Xcode (macOS)
  - GCC 10+ (Linux)
  - Clang 12+ (all platforms)
  - MSVC 2019+ (Windows)
- Git (for FetchContent)
- For VST3/AU installation: DAW supporting VST3 or AU (Reaper, Ableton, Logic Pro, etc.)

**Production:**
- DAW with VST3 or AU support
- macOS 10.13+ (for AU plugin)
- Linux kernel 4.0+ (for VST3)
- Windows 7+ (for VST3)
- Audio interface with MIDI output capability

**Installation (macOS example):**
- VST3 directory: `~/Library/Audio/Plug-Ins/VST3/`
- AU directory: `~/Library/Audio/Plug-Ins/`

## Compilation

**Compile Definitions:**
- `JUCE_WEB_BROWSER=0` - Disable web browser component (not needed)
- `JUCE_USE_CURL=0` - Disable CURL dependency (not needed)
- `JUCE_VST3_CAN_REPLACE_VST2=0` - VST2 compatibility setting
- `MA_ENABLE_ONNX=1` - Conditionally defined when `MA_ENABLE_ONNX=ON`

**Export Compile Commands:**
- Enabled via `CMAKE_EXPORT_COMPILE_COMMANDS ON` for IDE tooling support

## Build Targets

| Target | Output | Purpose |
|---|---|---|
| `MetalAccompaniment` | VST3/AU/Standalone plugin | Main audio plugin |
| `MetalAccompanimentData` | Binary data archive | Bundles ONNX model (Phase 2) |
| `MetalAccompanimentTests` | Executable | Unit test runner (Catch2) |
| `MetalAccompanimentExportPatterns` | Executable | Offline MIDI pattern exporter |

## Build Commands

```bash
# Configure Release build with standalone app
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_STANDALONE=ON

# Build all targets
cmake --build build --config Release --parallel

# Run tests
ctest --test-dir build --output-on-failure --config Release

# Run offline pattern exporter
./build/MetalAccompanimentExportPatterns
```

## CI/CD

**GitHub Actions:**
- Workflow: `.github/workflows/ci.yml`
- Runs on: macOS (GitHub Actions `macos-latest`)
- Steps:
  1. Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_STANDALONE=ON`
  2. Build: `cmake --build build --config Release --parallel`
  3. Test: `ctest --test-dir build --output-on-failure --config Release`

---

*Stack analysis: 2026-04-16*
