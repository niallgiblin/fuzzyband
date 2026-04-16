# External Integrations

**Analysis Date:** 2026-04-16

## Overview

This is a **standalone audio plugin with no external API integrations**. The plugin operates entirely within the audio processing pipeline of a DAW and communicates only through standard plugin protocols (VST3/AU) and MIDI.

## Plugin Protocols

**VST3:**
- Implements JUCE's VST3 plugin client
- Compiled format: `build/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3`
- Bundle ID: `com.ng.MetalAccompaniment`
- Manufacturer code: `NGAC`
- Plugin code: `MtAc`
- Categories: `Fx`, `Tools`

**Audio Unit (AU):**
- macOS-only plugin format
- Compiled format: `build/MetalAccompaniment_artefacts/Release/AU/Metal Accompaniment.component`
- Main type: `kAudioUnitType_MusicEffect`
- Only compiled when building on macOS

**Standalone:**
- Optional standalone executable for testing without a DAW
- Enabled via `MA_BUILD_STANDALONE=ON` CMake option

## Audio I/O

**Input:**
- Mono or stereo audio from guitar/instrument track
- No MIDI input required
- Audio thread sample rate: 44.1 kHz, 48 kHz, or higher (DAW-dependent)

**Output:**
- MIDI messages on two channels:
  - **Channel 10** (drum channel): Drum pattern events
  - **Channel 2** (configurable): Bass pattern events
- No audio output (MIDI-only effect)
- Routed to external drum and bass VSTi instruments

## DAW Routing

**Tested DAW:** Reaper

**Setup pattern:**
1. Insert Metal Accompaniment plugin on guitar/input track
2. Create separate MIDI tracks for drum and bass VSTi instruments
3. Route MIDI from guitar track → drum track (ch. 10) and bass track (ch. 2)
4. See `README.md` lines 58-63 for detailed Reaper routing steps

**Other DAWs:** VST3 and AU are standard protocols supported by most modern DAWs (Ableton, Logic Pro, Studio One, Cubase, etc.). Routing mechanism varies by DAW.

## Data Storage

**No Database:**
- Plugin does not use any database (SQL, document, or key-value store)

**No File I/O at Runtime:**
- MIDI patterns are stored as `constexpr` data in `src/midi/MidiPatternLibrary.h/.cpp`
- Patterns are compiled into the plugin binary
- No configuration files loaded at runtime
- No user data persisted

**File I/O (Build/Export Only):**
- `tools/export_patterns.cpp` - Offline tool to export MIDI patterns to `metal_accompaniment_patterns.mid` file
- Used for pattern auditioning in DAWs before committing to live plugin use
- Not invoked by the plugin itself

**Asset Files:**
- `assets/accompaniment_model.onnx` - ONNX model file (Phase 2, optional)
  - Only bundled when `MA_ENABLE_ONNX=ON`
  - Embedded as binary data via JUCE BinaryData system
  - Not accessed as a file; loaded from memory

## Authentication & Identity

**None.** Plugin requires no authentication or external identity verification.

## Monitoring & Observability

**Error Tracking:**
- None. Plugin does not report errors to external services.

**Logging:**
- No external logging system
- Debug output via standard C++ `std::cerr` or JUCE's debug console (JUCE_DEBUG_AUDIO_PLUGIN or similar)
- See `src/AccompanimentProcessor.cpp` for debug macro usage

**Metrics:**
- No telemetry or metrics collection
- Plugin does not send data to external servers

## Real-Time Constraints

**Audio Thread:**
- Hard real-time deadline (~5ms at 256 samples / 48kHz)
- No blocking I/O, no memory allocation, no external service calls
- Guaranteed bounded time for:
  - `OnsetDetector::process()` - FFT computation (moodycamel ReaderWriterQueue on CPU cache)
  - `EnergyAnalyser::process()` - Spectral analysis
  - `StructureTagger::update()` - State machine logic
  - `PatternPlayer::process()` - MIDI event generation

**Background Inference Thread:**
- Runs at ~50Hz (20ms sleep between iterations)
- May call into ONNX Runtime (blocking allowed here, not on audio thread)
- Does not interact with external services

## MIDI Communication

**Outgoing MIDI (from plugin):**
- **Channel 10**: Drum events (note-on, note-off, velocity, timing humanization)
- **Channel 2**: Bass events
- Event types: Note on, Note off
- Velocity range: 0-127 (humanized with ±10 random offset)
- Timing: Humanized with ±2ms random offset per hit
- No MIDI CC messages, no pitch bend

**Incoming MIDI:**
- None. Plugin declares `NEEDS_MIDI_INPUT = FALSE` in `CMakeLists.txt` line 116

## Feature Vector Handoff (Internal)

**Data Structure:** `FeatureVector` (defined in `src/analysis/FeatureVector.h`)
- Contains: BPM, RMS energy, spectral centroid, high-freq flux, structure state, sample timestamp
- Passed from audio thread to inference thread via moodycamel ReaderWriterQueue

## Webhooks & Callbacks

**None.** Plugin does not expose webhooks or accept external callbacks.

## Network Communication

**None.** Plugin has no network capabilities.
- `JUCE_WEB_BROWSER=0` - Web browser disabled
- `JUCE_USE_CURL=0` - HTTP client disabled

## State Saving / Recall

**Plugin State:**
- Current pattern index (atomic integer)
- Current BPM estimate
- Current structure state
- Not persisted to disk (live analysis only)

**DAW Automation:**
- Future phase: May expose plugin parameters for DAW automation (e.g., sensitivity, pattern selection override)
- Currently: No automatable parameters

## External Dependencies at Compile Time

All dependencies are fetched via CMake FetchContent and compiled locally:
- JUCE 8.0.10 (framework)
- moodycamel ReaderWriterQueue (lock-free queue)
- Catch2 3.5.2 (testing, optional)
- ONNX Runtime (inference, Phase 2, optional)

No package managers (npm, pip, cargo, conan) are used. No pre-built binaries are downloaded.

## Build System Integration

**GitHub Actions CI:**
- `.github/workflows/ci.yml`
- Runs on macOS latest
- Builds VST3, AU, and standalone targets
- Runs full test suite via Catch2

**CMake Integration:**
- `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` - Exports compile_commands.json for IDE support
- VSCode configuration in `.vscode/` directory (if present) can use compile_commands.json

## Security & Sandboxing

**Plugin Sandboxing:**
- VST3 and AU plugins run in the DAW's process (not sandboxed)
- Plugin has access to the DAW's memory and file system
- No additional security boundaries

**Audio Data:**
- Guitar audio is processed in real-time
- No audio data is stored or transmitted outside the plugin

## Version Management

**Plugin Version:**
- Defined in `CMakeLists.txt`: `project(MetalAccompaniment VERSION 0.1.0)`
- Updated as project version increments

**JUCE Version:**
- Pinned to 8.0.10 via Git tag
- FetchContent ensures reproducible builds

**Catch2 Version:**
- Pinned to v3.5.2 for testing

---

*Integration audit: 2026-04-16*
