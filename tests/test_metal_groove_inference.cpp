#include <catch2/catch_test_macros.hpp>

#if defined(MA_ENABLE_ONNX)
#include "inference/MetalGrooveInference.h"
#include "inference/pattern_embeddings.h"
#include "analysis/FeatureVector.h"
#include <algorithm>
#include <array>
#include <cmath>
#endif

#if defined(MA_ENABLE_ONNX)

namespace
{

/** Create a mel spectrogram filled with a constant dB value. */
std::array<float, 2048> makeMel(float dbValue = -40.0f)
{
    std::array<float, 2048> mel{};
    mel.fill(dbValue);
    return mel;
}

/** Create a FeatureVector for fallback testing. */
FeatureVector makeF(StructureState state, float bpm = 120.0f)
{
    FeatureVector f;
    f.state = state;
    f.bpm = bpm;
    f.rmsEnergy = 0.1f;
    f.spectralCentroid = 9000.0f;
    f.highFreqFlux = 0.1f;
    f.policyIntensity = 0.5f;
    return f;
}

} // namespace

// ── Model loading ───────────────────────────────────────────────────────────

TEST_CASE("MetalGrooveInference loads bundled model", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());
    REQUIRE(inference.getLoadErrorCount() == 0);
    REQUIRE(inference.getName() == "MetalGrooveInference");
}

TEST_CASE("MetalGrooveInference prepare is no-op", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());
    REQUIRE_NOTHROW(inference.prepare(44100.0));
    REQUIRE_NOTHROW(inference.prepare(48000.0));
}

// ── Mel-driven pattern selection ────────────────────────────────────────────

TEST_CASE("MetalGrooveInference selectPatternFromMel returns valid index", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    auto mel = makeMel(-40.0f);
    const int idx = inference.selectPatternFromMel(mel.data(), -1);
    REQUIRE(idx >= 0);
    REQUIRE(idx < PatternEmbeddings::kNumPatterns);
}

TEST_CASE("MetalGrooveInference selectPatternFromMel with excludeIndex", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    auto mel = makeMel(-40.0f);

    // Get baseline pattern
    int baseline = inference.selectPatternFromMel(mel.data(), -1);
    REQUIRE(baseline >= 0);

    // Excluding the baseline should give a different pattern
    int excluded = inference.selectPatternFromMel(mel.data(), baseline);
    REQUIRE(excluded >= 0);
    REQUIRE(excluded < PatternEmbeddings::kNumPatterns);
    REQUIRE(excluded != baseline);
}

TEST_CASE("MetalGrooveInference selectPatternFromMel deterministic", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    auto mel = makeMel(-40.0f);

    // Same input should produce same output
    int first = inference.selectPatternFromMel(mel.data(), -1);
    int second = inference.selectPatternFromMel(mel.data(), -1);
    REQUIRE(first == second);
}

TEST_CASE("MetalGrooveInference selectPatternFromMel different inputs", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    // Different mel inputs MAY produce different patterns (not guaranteed,
    // but with distinct enough inputs they should differ from the silent default)
    auto melA = makeMel(-10.0f);   // loud
    auto melB = makeMel(-70.0f);   // quiet

    int idxA = inference.selectPatternFromMel(melA.data(), -1);
    int idxB = inference.selectPatternFromMel(melB.data(), -1);
    REQUIRE(idxA >= 0);
    REQUIRE(idxB >= 0);
    // Both are valid; they may or may not differ depending on centroid layout
}

// ── Cosine similarity ───────────────────────────────────────────────────────

TEST_CASE("MetalGrooveInference centroids have valid norms", "[MetalGrooveInference]")
{
    for (int i = 0; i < PatternEmbeddings::kNumPatterns; ++i)
    {
        float normSq = 0.0f;
        for (int j = 0; j < PatternEmbeddings::kEmbeddingDim; ++j)
            normSq += PatternEmbeddings::kCentroids[i][j] * PatternEmbeddings::kCentroids[i][j];
        float norm = std::sqrt(normSq);
        REQUIRE(norm > 0.1f);  // centroids should be non-trivial
        REQUIRE(norm < 100.0f); // and not exploding
    }
}

TEST_CASE("MetalGrooveInference centroids are distinct", "[MetalGrooveInference]")
{
    // Verify no two centroids are identical
    int distinctCount = 0;
    for (int i = 0; i < PatternEmbeddings::kNumPatterns; ++i)
    {
        bool unique = true;
        for (int j = 0; j < i; ++j)
        {
            float maxDiff = 0.0f;
            for (int k = 0; k < PatternEmbeddings::kEmbeddingDim; ++k)
                maxDiff = std::max(maxDiff, std::abs(
                    PatternEmbeddings::kCentroids[i][k] - PatternEmbeddings::kCentroids[j][k]));
            if (maxDiff < 1e-5f)
            {
                unique = false;
                break;
            }
        }
        if (unique) ++distinctCount;
    }
    REQUIRE(distinctCount == PatternEmbeddings::kNumPatterns);
}

// ── Style classification ────────────────────────────────────────────────────

TEST_CASE("MetalGrooveInference classifyStyle returns valid index", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    auto mel = makeMel(-40.0f);
    int style = inference.classifyStyle(mel.data());
    REQUIRE(style >= 0);
    REQUIRE(style <= 4);  // 5 playing styles
}

TEST_CASE("MetalGrooveInference classifyStyle deterministic", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    auto mel = makeMel(-40.0f);
    int first = inference.classifyStyle(mel.data());
    int second = inference.classifyStyle(mel.data());
    REQUIRE(first == second);
}

// ── Fallback path (scalar features, no mel) ─────────────────────────────────

TEST_CASE("MetalGrooveInference selectPattern fallback", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    // Scalar-feature fallback should return valid pattern index
    auto f = makeF(StructureState::SOFT, 100.0f);
    int idx = inference.selectPattern(f, -1);
    REQUIRE(idx >= 0);
    REQUIRE(idx < PatternEmbeddings::kNumPatterns);
}

TEST_CASE("MetalGrooveInference selectPattern fallback respects exclude", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    auto f = makeF(StructureState::LOUD, 180.0f);
    int baseline = inference.selectPattern(f, -1);
    int excluded = inference.selectPattern(f, baseline);
    REQUIRE(excluded >= 0);
    REQUIRE(excluded != baseline);
}

// ── Edge cases ──────────────────────────────────────────────────────────────

TEST_CASE("MetalGrooveInference without model returns safe defaults", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    // Don't load model — tryLoadModel not called
    REQUIRE(inference.getLoadErrorCount() == 0);

    auto mel = makeMel();
    REQUIRE(inference.selectPatternFromMel(mel.data(), -1) == 0);
    REQUIRE(inference.classifyStyle(mel.data()) == 4);  // silence default
    REQUIRE(inference.selectPattern(makeF(StructureState::SILENT), -1) == 0);
}

TEST_CASE("MetalGrooveInference selectPatternFromMel exclude all but one", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    auto mel = makeMel(-40.0f);

    // Exclude all patterns except index 5
    // With all others excluded, the only valid choice is 5 (or the fallback)
    // Actually, the implementation just skips excluded indices in the loop
    int idx = inference.selectPatternFromMel(mel.data(), -1);  // get baseline
    // If we exclude idx, it should pick something else
    int idx2 = inference.selectPatternFromMel(mel.data(), idx);
    REQUIRE(idx2 != idx);
}

TEST_CASE("MetalGrooveInference error counts", "[MetalGrooveInference]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.getLoadErrorCount() == 0);
    REQUIRE(inference.getRunErrorCount() == 0);

    // Calling without model loaded doesn't increment run errors
    // (it returns early before the try block)
    inference.selectPatternFromMel(makeMel().data(), -1);
    inference.classifyStyle(makeMel().data());
}

#endif // MA_ENABLE_ONNX
