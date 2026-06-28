cmake_minimum_required(VERSION 3.22)
# Plugin UI shows ProjectInfo::versionString from this line. Bump when you cut a
# milestone checkpoint or release; GSD phase/milestone truth lives in .planning/.
project(MetalAccompaniment VERSION 0.8.3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────
# Options
# ─────────────────────────────────────────────────────────────────

option(MA_BUILD_TESTS       "Build unit tests"                  ON)
option(MA_BUILD_STANDALONE  "Build standalone app target"       ON)
option(MA_ENABLE_ONNX       "Enable ONNX Runtime (v0.6.0 default: ON)"     ON)

# ─────────────────────────────────────────────────────────────────
# JUCE — fetch if not found locally
# ─────────────────────────────────────────────────────────────────

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.10
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ─────────────────────────────────────────────────────────────────
# moodycamel lock-free queue (header-only)
# ─────────────────────────────────────────────────────────────────

FetchContent_Declare(
    moodycamel
    GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(moodycamel)

# ─────────────────────────────────────────────────────────────────
# Catch2 (tests only)
# ─────────────────────────────────────────────────────────────────

if(MA_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# ─────────────────────────────────────────────────────────────────
# ONNX Runtime (Phase 2, opt-in)
# Set ONNXRUNTIME_ROOT to your downloaded ONNX Runtime directory.
# e.g. cmake -DMA_ENABLE_ONNX=ON -DONNXRUNTIME_ROOT=/opt/onnxruntime ..
# ─────────────────────────────────────────────────────────────────

if(MA_ENABLE_ONNX)
    if(NOT DEFINED ONNXRUNTIME_ROOT)
        message(FATAL_ERROR "MA_ENABLE_ONNX is ON but ONNXRUNTIME_ROOT is not set.")
    endif()
    set(ONNX_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
    if(APPLE)
        set(ONNX_LIB "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib")
    elseif(UNIX)
        set(ONNX_LIB "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
    elseif(WIN32)
        set(ONNX_LIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    endif()
    message(STATUS "ONNX Runtime enabled: ${ONNX_LIB}")
endif()

# ─────────────────────────────────────────────────────────────────
# BinaryData — bundled assets (ONNX model, future resources)
# ─────────────────────────────────────────────────────────────────

set(BINARY_DATA_SOURCES
    "${CMAKE_SOURCE_DIR}/assets/forest.png"
)

if(MA_ENABLE_ONNX)
    list(APPEND BINARY_DATA_SOURCES
        "${CMAKE_SOURCE_DIR}/assets/accompaniment_model.onnx"
        "${CMAKE_SOURCE_DIR}/assets/metal_groove.onnx"
    )
endif()

juce_add_binary_data(MetalAccompanimentData
    SOURCES ${BINARY_DATA_SOURCES}
)

# ─────────────────────────────────────────────────────────────────
# Plugin target
# ─────────────────────────────────────────────────────────────────

set(PLUGIN_FORMATS VST3)
if(APPLE)
    list(APPEND PLUGIN_FORMATS AU)
endif()
if(MA_BUILD_STANDALONE)
    list(APPEND PLUGIN_FORMATS Standalone)
endif()

juce_add_plugin(MetalAccompaniment
    VERSION                     ${PROJECT_VERSION}
    COMPANY_NAME                "Niall Giblin"
    BUNDLE_ID                   com.ng.MetalAccompaniment
    PLUGIN_MANUFACTURER_CODE    "NGAC"
    PLUGIN_CODE                 "MtAc"
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "Metal Accompaniment"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Fx" "Tools"
    AU_MAIN_TYPE                "kAudioUnitType_MusicEffect"
    DESCRIPTION                 "AI metal drum and bass accompaniment"
)

juce_generate_juce_header(MetalAccompaniment)

# ─────────────────────────────────────────────────────────────────
# Source files
# ─────────────────────────────────────────────────────────────────

target_sources(MetalAccompaniment PRIVATE
    # Plugin entry points
    src/AccompanimentProcessor.cpp
    src/AccompanimentEditor.cpp

    # Analysis
    src/analysis/EnergyAnalyser.cpp
    src/analysis/StructureTagger.cpp
    src/analysis/StructureSequencer.cpp
    src/analysis/AudioRingBuffer.cpp
    src/analysis/MelSpectrogramExtractor.cpp
    src/analysis/PlaybackGate.cpp

    # Inference
    src/inference/RuleBasedInference.cpp
    src/inference/OnnxInference.cpp
    src/inference/MetalGrooveInference.cpp

    # MIDI
    src/midi/MidiPatternLibrary.cpp
    src/midi/PatternPlayer.cpp
    src/midi/RuleBasedBass.cpp
)

# ─────────────────────────────────────────────────────────────────
# Include paths
# ─────────────────────────────────────────────────────────────────

target_include_directories(MetalAccompaniment PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${moodycamel_SOURCE_DIR}"
)

if(MA_ENABLE_ONNX)
    target_include_directories(MetalAccompaniment PRIVATE "${ONNX_INCLUDE_DIR}" "${ONNX_INCLUDE_DIR}/onnxruntime")
endif()

# ─────────────────────────────────────────────────────────────────
# Compile definitions
# ─────────────────────────────────────────────────────────────────

target_compile_definitions(MetalAccompaniment PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<BOOL:${MA_ENABLE_ONNX}>:MA_ENABLE_ONNX=1>
    MA_PLUGIN_VERSION="${PROJECT_VERSION}"
)

# ─────────────────────────────────────────────────────────────────
# Compiler flags
# ─────────────────────────────────────────────────────────────────

if(APPLE)
    target_compile_options(MetalAccompaniment PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -ffast-math              # safe for audio DSP, big win on FFT
        -O3
    )
elseif(UNIX)
    target_compile_options(MetalAccompaniment PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -ffast-math
        -O3
        -march=native
    )
elseif(WIN32)
    target_compile_options(MetalAccompaniment PRIVATE
        /W4 /O2 /fp:fast
    )
endif()

# ─────────────────────────────────────────────────────────────────
# Link libraries
# ─────────────────────────────────────────────────────────────────

set(JUCE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

target_link_libraries(MetalAccompaniment PRIVATE
    ${JUCE_MODULES}
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MA_ENABLE_ONNX)
    target_link_libraries(MetalAccompaniment PRIVATE "${ONNX_LIB}")
endif()

if(BINARY_DATA_SOURCES)
    target_link_libraries(MetalAccompaniment PRIVATE MetalAccompanimentData)
endif()

# ─────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────

if(MA_BUILD_TESTS)
    # ── Unit tests ─────────────────────────────────────────────────────────────
    add_executable(MetalAccompanimentTests
        # Unit tests
        tests/test_structure_tagger.cpp
        tests/test_pattern_player.cpp
        tests/test_rule_based_inference.cpp
        tests/test_energy_analyser.cpp
        tests/test_structure_tagger_extended.cpp
        tests/test_sub_bass_energy.cpp
        tests/test_pattern_player_generative_bass.cpp
        tests/test_midi_pattern_library.cpp
        tests/test_pattern_rules.cpp
        tests/test_onnx_inference.cpp
        tests/test_metal_groove_inference.cpp
        tests/test_playback_gate.cpp

        # Plugin sources compiled into the test executable
        src/analysis/EnergyAnalyser.cpp
        src/analysis/StructureTagger.cpp
        src/analysis/StructureSequencer.cpp
        src/analysis/AudioRingBuffer.cpp
        src/analysis/MelSpectrogramExtractor.cpp
        src/analysis/PlaybackGate.cpp
        src/inference/RuleBasedInference.cpp
        src/midi/MidiPatternLibrary.cpp
        src/midi/PatternPlayer.cpp
        src/midi/RuleBasedBass.cpp
    )

    target_include_directories(MetalAccompanimentTests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${moodycamel_SOURCE_DIR}"
    )

    target_compile_definitions(MetalAccompanimentTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        MA_PLUGIN_VERSION="${PROJECT_VERSION}"
    )

    if(APPLE)
        target_compile_options(MetalAccompanimentTests PRIVATE
            -Wall -Wextra -Wno-unused-parameter -ffast-math)
    elseif(UNIX)
        target_compile_options(MetalAccompanimentTests PRIVATE
            -Wall -Wextra -Wno-unused-parameter -ffast-math -march=native)
    endif()

    target_link_libraries(MetalAccompanimentTests PRIVATE
        Catch2::Catch2WithMain
        ${JUCE_MODULES}
    )

    if(MA_ENABLE_ONNX)
        target_sources(MetalAccompanimentTests PRIVATE
            tests/test_onnx_latency_benchmark.cpp
            src/inference/OnnxInference.cpp
            src/inference/MetalGrooveInference.cpp
        )
        target_compile_definitions(MetalAccompanimentTests PRIVATE MA_ENABLE_ONNX=1)
        target_include_directories(MetalAccompanimentTests PRIVATE "${ONNX_INCLUDE_DIR}" "${ONNX_INCLUDE_DIR}/onnxruntime")
        target_link_libraries(MetalAccompanimentTests PRIVATE "${ONNX_LIB}")
        if(BINARY_DATA_SOURCES)
            target_link_libraries(MetalAccompanimentTests PRIVATE MetalAccompanimentData)
        endif()
        add_test(
            NAME onnx_latency_benchmark
            COMMAND MetalAccompanimentTests "[onnx][latency][benchmark]"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        )
        set_tests_properties(onnx_latency_benchmark PROPERTIES LABELS "onnx;benchmark")
    endif()

    # ── Integration + E2E tests ────────────────────────────────────────────────
    # Links the full MetalAccompaniment plugin target (avoids compiling
    # AccompanimentProcessor twice) and adds JuceLibraryCode includes.
    set(MA_JUCE_MODULES_DIR "${CMAKE_BINARY_DIR}/_deps/juce-src/modules")

    add_executable(MetalAccompanimentIntegrationTests
        # Integration test (full processor pipeline)
        tests/test_processor_pipeline.cpp
        # E2E tests
        tests/test_e2e_silent_section.cpp
        tests/test_e2e_bpm_tracking.cpp
        tests/test_e2e_structure_transitions.cpp
        tests/test_e2e_groove_variety.cpp
        tests/test_long_duration_stability.cpp
        tests/test_performance_benchmark.cpp
    )

    target_include_directories(MetalAccompanimentIntegrationTests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${moodycamel_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/MetalAccompaniment_artefacts/JuceLibraryCode"
        "${MA_JUCE_MODULES_DIR}"
    )

    target_compile_definitions(MetalAccompanimentIntegrationTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        "MA_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
    )

    target_link_libraries(MetalAccompanimentIntegrationTests PRIVATE
        Catch2::Catch2WithMain
        MetalAccompaniment
    )

    # BinaryData headers live under juce_binarydata_* (INTERFACE on MetalAccompanimentData),
    # not next to JuceHeader.h. Linking the data target pulls in that include path; PRIVATE
    # linkage from the plugin alone does not propagate it to dependents.
    if(BINARY_DATA_SOURCES)
        target_link_libraries(MetalAccompanimentIntegrationTests PRIVATE MetalAccompanimentData)
    endif()

    # ── CTest registration ─────────────────────────────────────────────────────
    include(CTest)
    include(Catch)

    # Unit tests — each TEST_CASE becomes an individual CTest entry with label "unit"
    catch_discover_tests(MetalAccompanimentTests
        PROPERTIES LABELS "unit"
    )

    # Integration: processor pipeline tests
    add_test(
        NAME integration_processor_pipeline
        COMMAND MetalAccompanimentIntegrationTests "[integration][pipeline]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(integration_processor_pipeline PROPERTIES LABELS "integration")

    # E2E tests
    add_test(
        NAME e2e_silent_section
        COMMAND MetalAccompanimentIntegrationTests "[e2e][silent]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(e2e_silent_section PROPERTIES LABELS "e2e")

    add_test(
        NAME e2e_bpm_tracking
        COMMAND MetalAccompanimentIntegrationTests "[e2e][bpm]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(e2e_bpm_tracking PROPERTIES LABELS "e2e")

    add_test(
        NAME e2e_structure_transitions
        COMMAND MetalAccompanimentIntegrationTests "[e2e][transitions]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(e2e_structure_transitions PROPERTIES LABELS "e2e")

    add_test(
        NAME e2e_groove_variety
        COMMAND MetalAccompanimentIntegrationTests "[e2e][groove]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(e2e_groove_variety PROPERTIES LABELS "e2e")

    add_test(
        NAME stability_long_duration
        COMMAND MetalAccompanimentIntegrationTests "[stability][long]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(stability_long_duration PROPERTIES LABELS "stability")

    add_test(
        NAME perf_block_processing
        COMMAND MetalAccompanimentIntegrationTests "[perf]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(perf_block_processing PROPERTIES LABELS "perf")
endif()

# ─────────────────────────────────────────────────────────────────
# Offline pattern MIDI export (audition patterns in Reaper)
# ─────────────────────────────────────────────────────────────────

add_executable(MetalAccompanimentExportPatterns
    tools/export_patterns.cpp
    src/midi/MidiPatternLibrary.cpp
)

target_include_directories(MetalAccompanimentExportPatterns PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(MetalAccompanimentExportPatterns PRIVATE
    juce::juce_audio_basics
    juce::juce_core
)

# ─────────────────────────────────────────────────────────────────
# API docs (optional — requires Doxygen on PATH)
# ─────────────────────────────────────────────────────────────────

find_program(DOXYGEN_EXECUTABLE NAMES doxygen)
if(DOXYGEN_EXECUTABLE)
    add_custom_target(doxygen-docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating Doxygen HTML API documentation (docs/doxygen/html/)"
        VERBATIM
    )
endif()
