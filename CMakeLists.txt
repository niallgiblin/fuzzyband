cmake_minimum_required(VERSION 3.22)
# Plugin UI shows ProjectInfo::versionString from this line. Bump when you cut a
# milestone checkpoint or release; GSD phase/milestone truth lives in .planning/.
project(MetalAccompaniment VERSION 1.0.38)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ─────────────────────────────────────────────────────────────────
# Options
# ─────────────────────────────────────────────────────────────────

option(MA_BUILD_TESTS       "Build unit tests"                  ON)
option(MA_BUILD_STANDALONE  "Build standalone app target"       ON)
option(MA_ENABLE_ONNX       "Enable ONNX Runtime (v0.6.0 default: ON)"     ON)
option(MA_BUNDLE_GROOVE_RENDERER "Bundle groove_renderer.onnx (Tier-1 learned groove renderer)" ON)
option(MA_ENABLE_TSAN       "Build with ThreadSanitizer (Clang/GCC)" OFF)

if(MA_ENABLE_TSAN)
    if(MSVC)
        message(FATAL_ERROR "MA_ENABLE_TSAN requires Clang or GCC")
    endif()
    # Flags must land before FetchContent so JUCE is instrumented too (T8.2).
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -g -O1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer -g -O1")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=thread")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=thread")
    add_compile_definitions(MA_ENABLE_TSAN=1)
    message(STATUS "ThreadSanitizer enabled")
endif()

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
# ONNX Runtime (required; MA_ENABLE_ONNX defaults ON)
# Set ONNXRUNTIME_ROOT to your ONNX Runtime directory.
# e.g. cmake -B build -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
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
    "${CMAKE_SOURCE_DIR}/assets/fonts/AlegreyaSans-Regular.ttf"
    "${CMAKE_SOURCE_DIR}/assets/fonts/AlegreyaSans-Medium.ttf"
    "${CMAKE_SOURCE_DIR}/assets/fonts/AlegreyaSans-Bold.ttf"
    "${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Regular.ttf"
    "${CMAKE_SOURCE_DIR}/assets/fonts/IBMPlexMono-Medium.ttf"
)

if(MA_ENABLE_ONNX)
    list(APPEND BINARY_DATA_SOURCES
        "${CMAKE_SOURCE_DIR}/assets/metal_groove.onnx"
        "${CMAKE_SOURCE_DIR}/assets/style_cnn.onnx"
    )
endif()

if(MA_BUNDLE_GROOVE_RENDERER)
    list(APPEND BINARY_DATA_SOURCES
        "${CMAKE_SOURCE_DIR}/assets/groove_renderer.onnx"
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
    BUNDLE_ID                   com.ng.fuzzyband
    PLUGIN_MANUFACTURER_CODE    "NGAC"
    PLUGIN_CODE                 "MtAc"
    FORMATS                     ${PLUGIN_FORMATS}
    PRODUCT_NAME                "fuzzyband"
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
    src/analysis/PitchEstimator.cpp
    src/analysis/StablePitchTracker.cpp
    src/analysis/AttackDetector.cpp
    src/analysis/PhraseLearner.cpp

    # Inference
    src/inference/RuleBasedInference.cpp
    src/inference/MetalGrooveInference.cpp
    src/inference/GrooveRenderer.cpp

    # MIDI
    src/midi/MidiPatternLibrary.cpp
    src/midi/PatternPlayer.cpp
    src/midi/BassVoice.cpp
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
    $<$<BOOL:${MA_BUNDLE_GROOVE_RENDERER}>:MA_BUNDLE_GROOVE_RENDERER=1>
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
        $<$<NOT:$<BOOL:${MA_ENABLE_TSAN}>>:-O3>
    )
elseif(UNIX)
    target_compile_options(MetalAccompaniment PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -ffast-math
        $<$<NOT:$<BOOL:${MA_ENABLE_TSAN}>>:-O3>
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
    # Ensure the ONNX Runtime dylib is findable at runtime
    set_target_properties(MetalAccompaniment PROPERTIES
        BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
    )
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
        tests/test_midi_pattern_library.cpp
        tests/test_bass_voice.cpp
        tests/test_pattern_rules.cpp
        tests/test_fill_grammar.cpp
        tests/test_groove_template.cpp
        tests/test_metal_groove_inference.cpp
        tests/test_playback_gate.cpp
        tests/test_stable_pitch_tracker.cpp
        tests/test_pitch_estimator.cpp
        tests/test_attack_detector.cpp
        tests/test_phrase_learner.cpp
        tests/test_golden_signal.cpp
        tests/test_bass_mirror_realaudio.cpp
        tests/test_structure_sequencer.cpp
        tests/test_mel_spectrogram.cpp
        tests/test_feature_capture.cpp
        tests/test_groove_renderer.cpp
        tests/test_midi_probe.cpp
        tests/test_phase1_rendering.cpp
        tests/test_phase3_dynamics.cpp

        # Plugin sources compiled into the test executable
        src/capture/FeatureCapture.cpp
        src/analysis/EnergyAnalyser.cpp
        src/analysis/StructureTagger.cpp
        src/analysis/StructureSequencer.cpp
        src/analysis/AudioRingBuffer.cpp
        src/analysis/MelSpectrogramExtractor.cpp
        src/analysis/PlaybackGate.cpp
        src/analysis/PitchEstimator.cpp
        src/analysis/StablePitchTracker.cpp
        src/analysis/AttackDetector.cpp
        src/analysis/PhraseLearner.cpp
        src/inference/RuleBasedInference.cpp
        src/inference/GrooveRenderer.cpp
        src/midi/MidiPatternLibrary.cpp
        src/midi/PatternPlayer.cpp
        src/midi/BassVoice.cpp
    )

    target_include_directories(MetalAccompanimentTests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/tests"
        "${moodycamel_SOURCE_DIR}"
    )

    target_compile_definitions(MetalAccompanimentTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        MA_PLUGIN_VERSION="${PROJECT_VERSION}"
        "MA_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
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
            src/inference/MetalGrooveInference.cpp
        )
        target_compile_definitions(MetalAccompanimentTests PRIVATE MA_ENABLE_ONNX=1)
        target_include_directories(MetalAccompanimentTests PRIVATE "${ONNX_INCLUDE_DIR}" "${ONNX_INCLUDE_DIR}/onnxruntime")
        target_link_libraries(MetalAccompanimentTests PRIVATE "${ONNX_LIB}")
        if(BINARY_DATA_SOURCES)
            target_link_libraries(MetalAccompanimentTests PRIVATE MetalAccompanimentData)
        endif()
        if(MA_BUNDLE_GROOVE_RENDERER)
            target_sources(MetalAccompanimentTests PRIVATE tests/test_groove_renderer_onnx.cpp)
            target_compile_definitions(MetalAccompanimentTests PRIVATE MA_BUNDLE_GROOVE_RENDERER=1)
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
        # Structure shadow fixture replay (golden regression guard)
        tests/test_structure_shadow_integration.cpp
        # Integration test (full processor pipeline)
        tests/test_processor_pipeline.cpp
        # Real-audio bass mirror (mirror vs harmony producer split)
        tests/test_bass_mirror_play_realaudio.cpp
        # Diagnostic probe: real PitchEstimator output on a supplied clean DI
        tests/test_pitch_probe.cpp
        # E2E tests
        tests/test_e2e_silent_section.cpp
        tests/test_e2e_bpm_tracking.cpp
        tests/test_e2e_structure_transitions.cpp
        tests/test_e2e_groove_variety.cpp
        tests/test_long_duration_stability.cpp
        tests/test_performance_benchmark.cpp
        tests/test_baseline_capture.cpp
        tests/test_phase2_clock.cpp
        tests/test_phase9_acceptance.cpp
    )

    target_include_directories(MetalAccompanimentIntegrationTests PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/tests"
        "${moodycamel_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/MetalAccompaniment_artefacts/JuceLibraryCode"
        "${MA_JUCE_MODULES_DIR}"
    )

    target_compile_definitions(MetalAccompanimentIntegrationTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        MA_PLUGIN_VERSION="${PROJECT_VERSION}"
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

    # ── Orphan-test guard ──────────────────────────────────────────────────────
    # Fail configuration if any tests/*.cpp is not compiled into a test target.
    # This closes the recurring "all-green-but-broken" trap where a test file
    # silently stops being built (see docs/DATA_STRATEGY.md Phase 2). Tests that
    # are only built under a feature flag (e.g. ONNX-gated) are exempted below.
    set(MA_CONDITIONAL_TEST_FILES
        test_onnx_latency_benchmark.cpp      # only linked when MA_ENABLE_ONNX=ON
        test_groove_renderer_onnx.cpp        # only linked when MA_BUNDLE_GROOVE_RENDERER=ON
    )

    file(GLOB MA_ALL_TEST_FILES CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_SOURCE_DIR}/tests" "${CMAKE_SOURCE_DIR}/tests/*.cpp")

    get_target_property(MA_UNIT_TEST_SOURCES  MetalAccompanimentTests            SOURCES)
    get_target_property(MA_INTEG_TEST_SOURCES MetalAccompanimentIntegrationTests SOURCES)

    set(MA_REFERENCED_TEST_NAMES "")
    foreach(_src IN LISTS MA_UNIT_TEST_SOURCES MA_INTEG_TEST_SOURCES)
        get_filename_component(_name "${_src}" NAME)
        list(APPEND MA_REFERENCED_TEST_NAMES "${_name}")
    endforeach()

    set(MA_ORPHAN_TESTS "")
    foreach(_test IN LISTS MA_ALL_TEST_FILES)
        if(_test IN_LIST MA_REFERENCED_TEST_NAMES)
            continue()
        endif()
        if(_test IN_LIST MA_CONDITIONAL_TEST_FILES)
            continue()
        endif()
        list(APPEND MA_ORPHAN_TESTS "${_test}")
    endforeach()

    if(MA_ORPHAN_TESTS)
        string(REPLACE ";" "\n  - " MA_ORPHAN_PRETTY "${MA_ORPHAN_TESTS}")
        message(FATAL_ERROR
            "Orphan test file(s) found in tests/ that no target compiles:\n"
            "  - ${MA_ORPHAN_PRETTY}\n"
            "Add each to MetalAccompanimentTests or MetalAccompanimentIntegrationTests "
            "in CMakeLists.txt (or delete the file). If a test is intentionally built "
            "only under a feature flag, add it to MA_CONDITIONAL_TEST_FILES.")
    endif()

    # ── CTest registration ─────────────────────────────────────────────────────
    include(CTest)
    include(Catch)

    # Unit tests — each TEST_CASE becomes an individual CTest entry with label "unit"
    catch_discover_tests(MetalAccompanimentTests
        PROPERTIES LABELS "unit"
    )

    # Integration: structure shadow fixture replay
    add_test(
        NAME integration_structure_shadow_fixture
        COMMAND MetalAccompanimentIntegrationTests "[structure][shadow]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(integration_structure_shadow_fixture PROPERTIES LABELS "integration")

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
    set_tests_properties(perf_block_processing PROPERTIES LABELS "perf;phase9")

    add_test(
        NAME t9_2_buffer_invariance
        COMMAND MetalAccompanimentIntegrationTests "[t9.2]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(t9_2_buffer_invariance PROPERTIES LABELS "integration;phase9")

    add_test(
        NAME t9_acceptance
        COMMAND MetalAccompanimentIntegrationTests "[phase9]"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    set_tests_properties(t9_acceptance PROPERTIES LABELS "integration;phase9")
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

# JUCE's VST3 helper writes moduleinfo.json AFTER the bundle is codesigned,
# which invalidates the seal. macOS then refuses to load the plugin in a
# signed host (REAPER): "library load disallowed by system policy".
# Re-sign last so the installed artefact stays valid. Must stay at the end
# of this file so it runs after JUCE's own POST_BUILD steps.
if(APPLE)
    foreach(_ma_fmt VST3 AU Standalone)
        if(TARGET MetalAccompaniment_${_ma_fmt})
            add_custom_command(TARGET MetalAccompaniment_${_ma_fmt} POST_BUILD
                # Strip Finder/resource-fork metadata the bundle may have picked up
                # (com.apple.FinderInfo / fpfs) — codesign rejects it as "detritus",
                # which otherwise breaks the AU build.
                COMMAND xattr -cr "$<TARGET_BUNDLE_DIR:MetalAccompaniment_${_ma_fmt}>"
                COMMAND codesign --force --sign - --deep --timestamp=none
                        "$<TARGET_BUNDLE_DIR:MetalAccompaniment_${_ma_fmt}>"
                COMMENT "Re-sign MetalAccompaniment ${_ma_fmt} after JUCE bundle finalization"
                VERBATIM
            )
        endif()
    endforeach()
endif()
