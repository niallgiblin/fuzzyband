cmake_minimum_required(VERSION 3.22)
project(MetalAccompaniment VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────
# Options
# ─────────────────────────────────────────────────────────────────

option(MA_BUILD_TESTS       "Build unit tests"                  ON)
option(MA_BUILD_STANDALONE  "Build standalone app target"       ON)
option(MA_ENABLE_ONNX       "Enable ONNX Runtime (Phase 2)"     OFF)

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

set(BINARY_DATA_SOURCES "")

if(MA_ENABLE_ONNX)
    list(APPEND BINARY_DATA_SOURCES
        "${CMAKE_SOURCE_DIR}/assets/accompaniment_model.onnx"
    )
endif()

if(BINARY_DATA_SOURCES)
    juce_add_binary_data(MetalAccompanimentData
        SOURCES ${BINARY_DATA_SOURCES}
    )
endif()

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
    src/analysis/OnsetDetector.cpp
    src/analysis/EnergyAnalyser.cpp
    src/analysis/StructureTagger.cpp
    src/analysis/PitchEstimator.cpp

    # Inference
    src/inference/RuleBasedInference.cpp
    src/inference/OnnxInference.cpp        # stub in Phase 1

    # MIDI
    src/midi/MidiPatternLibrary.cpp
    src/midi/PatternPlayer.cpp
)

# ─────────────────────────────────────────────────────────────────
# Include paths
# ─────────────────────────────────────────────────────────────────

target_include_directories(MetalAccompaniment PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${moodycamel_SOURCE_DIR}"
)

if(MA_ENABLE_ONNX)
    target_include_directories(MetalAccompaniment PRIVATE "${ONNX_INCLUDE_DIR}")
endif()

# ─────────────────────────────────────────────────────────────────
# Compile definitions
# ─────────────────────────────────────────────────────────────────

target_compile_definitions(MetalAccompaniment PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    $<$<BOOL:${MA_ENABLE_ONNX}>:MA_ENABLE_ONNX=1>
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
    add_executable(MetalAccompanimentTests
        tests/test_onset_detector.cpp
        tests/test_structure_tagger.cpp
        tests/test_pattern_player.cpp
        tests/test_pitch_estimator.cpp
        tests/test_rule_based_inference.cpp

        # Include plugin sources needed by tests (not the plugin target itself)
        src/analysis/OnsetDetector.cpp
        src/analysis/EnergyAnalyser.cpp
        src/analysis/StructureTagger.cpp
        src/analysis/PitchEstimator.cpp
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

    include(CTest)
    include(Catch)
    catch_discover_tests(MetalAccompanimentTests)
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
