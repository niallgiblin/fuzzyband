#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"
#include "midi/BassMidiValidator.h"

TEST_CASE("BassMidiValidator rejects when structureSilentPolicy is true", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(
        40.f,
        1.0f,
        StructureState::VERSE,
        true));
}

TEST_CASE("BassMidiValidator rejects notes outside [28, 55]", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(
        27.f,
        1.0f,
        StructureState::VERSE,
        false));
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(
        56.f,
        1.0f,
        StructureState::VERSE,
        false));
}

TEST_CASE("BassMidiValidator accepts nominal root and duration when not silent", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(
        40.f,
        1.0f,
        StructureState::VERSE,
        false));
}

/** Mirrors kMinGenerativeConfidence / kLibraryBassScore in AccompanimentProcessor.cpp (GBASS-03). */
TEST_CASE("Degradation: failed validation yields no generative score", "[bass]")
{
    constexpr float kMinGenerativeConfidence = 0.5f;
    constexpr float kLibraryBassScore = 0.45f;
    const float margin = 0.f;
    const float pConf = 0.99f;
    const bool valid = BassMidiValidator::validateBassProposal(
        27.f,
        1.0f,
        StructureState::VERSE,
        false);
    REQUIRE_FALSE(valid);
    float scoreGen = 0.f;
    if (valid && margin >= 0.f && pConf >= kMinGenerativeConfidence)
        scoreGen = pConf;
    REQUIRE(scoreGen == 0.f);
    REQUIRE_FALSE(scoreGen > kLibraryBassScore);
}

TEST_CASE("Degradation: sub-threshold confidence does not beat library baseline", "[bass]")
{
    constexpr float kMinGenerativeConfidence = 0.5f;
    constexpr float kLibraryBassScore = 0.45f;
    const float margin = 0.f;
    const float pConf = 0.48f;
    const bool valid = BassMidiValidator::validateBassProposal(
        40.f,
        1.0f,
        StructureState::VERSE,
        false);
    REQUIRE(valid);
    float scoreGen = 0.f;
    if (valid && margin >= 0.f && pConf >= kMinGenerativeConfidence)
        scoreGen = pConf;
    REQUIRE(scoreGen == 0.f);
    REQUIRE_FALSE(scoreGen > kLibraryBassScore);
}
