#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <fstream>
#include <sstream>
#include "capture/FeatureCapture.h"

namespace
{
std::string readFile(const std::string& path)
{
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
} // namespace

TEST_CASE("FeatureCapture writes feature_capture.v1 header", "[FeatureCapture]")
{
    FeatureCapture capture;
    capture.prepare(48000.0, 256, "RuleBasedInference");

    REQUIRE(capture.start());
    const auto path = capture.getCapturePath();
    REQUIRE_FALSE(path.empty());
    capture.stop();

    const auto text = readFile(path);
    REQUIRE(text.find("feature_capture.v1") != std::string::npos);
    REQUIRE(text.find("schema_version") != std::string::npos);
    REQUIRE(text.find("plugin_version") != std::string::npos);
    REQUIRE(text.find("sample_rate") != std::string::npos);
    REQUIRE(text.find("block_size") != std::string::npos);
    REQUIRE(text.find("capture_start_utc") != std::string::npos);
    REQUIRE(text.find("model_mode") != std::string::npos);
    REQUIRE(text.find("max_rows") != std::string::npos);

    juce::File(path).deleteFile();
}

TEST_CASE("FeatureCapture writes required FeatureVector and prediction fields", "[FeatureCapture]")
{
    FeatureCapture capture;
    capture.prepare(48000.0, 512, "RuleBasedInference");

    REQUIRE(capture.start());
    const auto path = capture.getCapturePath();

    FeatureCaptureRow row{};
    row.bpm = 142.0f;
    row.rmsEnergy = 0.25f;
    row.spectralCentroid = 2300.0f;
    row.highFreqFlux = 0.42f;
    row.state = StructureState::SOFT;
    row.sampleTimestamp = 96000;
    row.pitchRootMidi = 40.0f;
    row.pitchConfidence = 0.8f;
    row.policyIntensity = 0.55f;
    row.rmsDelta = 0.12f;
    row.elapsedSeconds = 2.0;
    row.rulePatternIndex = 2;
    row.activePatternIndex = 2;
    row.onnxPatternIndex = -1;
    row.onnxAvailable = false;
    row.rulePatternName = "Verse mid";
    row.activePatternName = "Verse mid";
    row.modelMode = "RuleBasedInference";

    REQUIRE(capture.tryPush(row));
    capture.stop();

    const auto text = readFile(path);
    REQUIRE(text.find("sample_timestamp") != std::string::npos);
    REQUIRE(text.find("elapsed_seconds") != std::string::npos);
    REQUIRE(text.find("bpm") != std::string::npos);
    REQUIRE(text.find("rms_energy") != std::string::npos);
    REQUIRE(text.find("spectral_centroid") != std::string::npos);
    REQUIRE(text.find("high_freq_flux") != std::string::npos);
    REQUIRE(text.find("pitch_root_midi") != std::string::npos);
    REQUIRE(text.find("pitch_confidence") != std::string::npos);
    REQUIRE(text.find("policy_intensity") != std::string::npos);
    REQUIRE(text.find("rms_delta") != std::string::npos);
    REQUIRE(text.find("rule_pattern_index") != std::string::npos);
    REQUIRE(text.find("active_pattern_index") != std::string::npos);
    REQUIRE(text.find("onnx_pattern_index") != std::string::npos);
    REQUIRE(text.find("onnx_available") != std::string::npos);
    REQUIRE(text.find("Verse mid") != std::string::npos);

    juce::File(path).deleteFile();
}

TEST_CASE("FeatureCapture closes writer and exposes safety cap constants", "[FeatureCapture]")
{
    FeatureCapture capture;
    capture.prepare(44100.0, 128, "RuleBasedInference");
    REQUIRE(capture.start());
    const auto path = capture.getCapturePath();
    capture.stop();

    std::ifstream in(path);
    REQUIRE(in.good());
    REQUIRE(FeatureCapture::kMaxRows > 0);
    REQUIRE(FeatureCapture::kMaxCaptureSeconds > 0.0);

    juce::File(path).deleteFile();
}
