#include <catch2/catch_test_macros.hpp>
#include "inference/pattern_rules.h"
#include "analysis/FeatureVector.h"

#if defined(MA_ENABLE_ONNX)
#include "inference/OnnxInference.h"
#endif

static FeatureVector makeF(StructureState state, float bpm, float policyIntensity = 0.5f)
{
    FeatureVector f;
    f.state = state;
    f.bpm = bpm;
    f.policyIntensity = policyIntensity;
    return f;
}

/** Mirrors OnnxInference::selectPattern post-inference gate (OnnxInference.cpp ~164-166). */
static int applyOnnxAcceptanceGate(int clampedModelOutput, const FeatureVector& f)
{
    return PatternRules::isOnnxPatternAcceptable(clampedModelOutput, f)
        ? clampedModelOutput
        : PatternRules::rulePatternForState(f);
}

TEST_CASE("OnnxInference acceptance gate surfaces Breakdown when acceptable", "[OnnxInference][Breakdown]")
{
    const FeatureVector f = makeF(StructureState::SOFT, 100.0f);
    REQUIRE(applyOnnxAcceptanceGate(6, f) == 6);
}

TEST_CASE("OnnxInference acceptance gate rejects Breakdown at high BPM", "[OnnxInference][Breakdown]")
{
    const FeatureVector f = makeF(StructureState::LOUD, 170.0f);
    const int result = applyOnnxAcceptanceGate(6, f);
    REQUIRE(result != 6);
    REQUIRE(result == PatternRules::rulePatternForState(f));
}

#if defined(MA_ENABLE_ONNX)

TEST_CASE("OnnxInference loads bundled model and runs selectPattern", "[OnnxInference]")
{
    OnnxInference inference;
    REQUIRE(inference.tryLoadModel());
    inference.prepare(48000.0);

    FeatureVector f = makeF(StructureState::SOFT, 100.0f);
    f.rmsEnergy = 0.05f;
    f.spectralCentroid = 9000.0f;
    f.highFreqFlux = 0.1f;

    const int pattern = inference.selectPattern(f);
    REQUIRE(pattern >= 0);
    REQUIRE(pattern <= 6);
}

#endif
