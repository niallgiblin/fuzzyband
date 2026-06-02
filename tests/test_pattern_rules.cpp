#include <catch2/catch_test_macros.hpp>
#include "inference/pattern_rules.h"
#include "analysis/FeatureVector.h"

// Helper to build a FeatureVector with the given fields
static FeatureVector makeF(StructureState state, float bpm, float policyIntensity = 0.5f)
{
    FeatureVector f;
    f.state = state;
    f.bpm = bpm;
    f.policyIntensity = policyIntensity;
    return f;
}

TEST_CASE("PatternRules::adjustedBpm applies intensity shift", "[pattern_rules]")
{
    // Neutral intensity (0.5) → no shift
    FeatureVector f = makeF(StructureState::SILENT, 140.0f, 0.5f);
    REQUIRE(PatternRules::adjustedBpm(f) == 140.0f);

    // Full intensity (1.0) → +20 shift
    f.policyIntensity = 1.0f;
    REQUIRE(PatternRules::adjustedBpm(f) == 160.0f);

    // Zero intensity (0.0) → -20 shift
    f.policyIntensity = 0.0f;
    REQUIRE(PatternRules::adjustedBpm(f) == 120.0f);
}

TEST_CASE("PatternRules::adjustedBpm clamps at intensity extremes", "[pattern_rules]")
{
    // Lower clamp: bpm=80, intensity=0.0 → raw 60, clamped to 80
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::SOFT, 80.0f, 0.0f)) == 80.0f);

    // Upper clamp: bpm=220, intensity=1.0 → raw 240, clamped to 220
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::LOUD, 220.0f, 1.0f)) == 220.0f);

    // bpm=100, intensity=0.0 → raw 80
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::SOFT, 100.0f, 0.0f)) == 80.0f);

    // bpm=200, intensity=1.0 → raw 220
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::LOUD, 200.0f, 1.0f)) == 220.0f);

    // Interior regression: neutral intensity unchanged
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::SOFT, 140.0f, 0.5f)) == 140.0f);
}

TEST_CASE("PatternRules::isOnnxPatternAcceptable Breakdown matrix", "[pattern_rules][Breakdown]")
{
    // SOFT, bpm=100 → Breakdown acceptable
    REQUIRE(PatternRules::isOnnxPatternAcceptable(6, makeF(StructureState::SOFT, 100.0f)) == true);

    // LOUD, bpm=100 → Breakdown acceptable (non-SILENT, raw bpm < 110)
    REQUIRE(PatternRules::isOnnxPatternAcceptable(6, makeF(StructureState::LOUD, 100.0f)) == true);

    // SILENT → Breakdown rejected
    REQUIRE(PatternRules::isOnnxPatternAcceptable(6, makeF(StructureState::SILENT, 100.0f)) == false);

    // SOFT, bpm=120 → raw bpm >= 110 threshold
    REQUIRE(PatternRules::isOnnxPatternAcceptable(6, makeF(StructureState::SOFT, 120.0f)) == false);

    // Normal compat path: SOFT, pattern 2
    REQUIRE(PatternRules::isOnnxPatternAcceptable(2, makeF(StructureState::SOFT, 140.0f)) == true);
}

TEST_CASE("PatternRules::rulePatternForState maps states to indices", "[pattern_rules]")
{
    // SILENT → 0
    REQUIRE(PatternRules::rulePatternForState(makeF(StructureState::SILENT, 120.0f)) == 0);

    // SOFT: bpm < 120 → 1
    REQUIRE(PatternRules::rulePatternForState(makeF(StructureState::SOFT, 100.0f)) == 1);
    // SOFT: 120 <= bpm < 160 → 2
    REQUIRE(PatternRules::rulePatternForState(makeF(StructureState::SOFT, 140.0f)) == 2);
    // SOFT: bpm >= 160 → 3
    REQUIRE(PatternRules::rulePatternForState(makeF(StructureState::SOFT, 170.0f)) == 3);

    // LOUD: bpm < 160 → 4
    REQUIRE(PatternRules::rulePatternForState(makeF(StructureState::LOUD, 150.0f)) == 4);
    // LOUD: bpm >= 160 → 5
    REQUIRE(PatternRules::rulePatternForState(makeF(StructureState::LOUD, 170.0f)) == 5);
}

TEST_CASE("PatternRules::isPatternCompatibleWithState checks boundaries", "[pattern_rules]")
{
    // SILENT: only index 0 is compatible
    REQUIRE(PatternRules::isPatternCompatibleWithState(0, StructureState::SILENT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(1, StructureState::SILENT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(3, StructureState::SILENT) == false);

    // SOFT: indices 1-3
    REQUIRE(PatternRules::isPatternCompatibleWithState(1, StructureState::SOFT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(2, StructureState::SOFT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(3, StructureState::SOFT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(0, StructureState::SOFT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(4, StructureState::SOFT) == false);

    // LOUD: indices 4-5
    REQUIRE(PatternRules::isPatternCompatibleWithState(4, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(5, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(3, StructureState::LOUD) == false);
}

TEST_CASE("PatternRules exclusion matrix: compatible and not excluded", "[pattern_rules]")
{
    const StructureState states[] = { StructureState::SILENT, StructureState::SOFT, StructureState::LOUD };
    for (auto state : states)
    {
        for (int exclude = 0; exclude <= 6; ++exclude)
        {
            FeatureVector f = makeF(state, 140.0f, 0.5f);
            const int base = PatternRules::rulePatternForState(f);
            const int out = PatternRules::applyExclusion(base, exclude, state, base);
            REQUIRE(PatternRules::isPatternCompatibleWithState(out, state));
            if (base == exclude && !(state == StructureState::SILENT && exclude == 0))
                REQUIRE(out != exclude);
        }
    }
}

TEST_CASE("PatternRules exclusion skips incompatible Breakdown on LOUD", "[pattern_rules]")
{
    FeatureVector f = makeF(StructureState::LOUD, 170.0f);
    const int out = PatternRules::applyExclusion(5, 5, StructureState::LOUD, 5);
    REQUIRE(out != 6);
    REQUIRE(PatternRules::isPatternCompatibleWithState(out, StructureState::LOUD));
    REQUIRE(out == 4);
}

TEST_CASE("PatternRules intensity shifts SOFT class at threshold", "[pattern_rules]")
{
    FeatureVector f = makeF(StructureState::SOFT, 130.0f, 0.0f);
    REQUIRE(PatternRules::rulePatternForState(f) == 1);
}

TEST_CASE("PatternRules::applyExclusion inactive when no match", "[pattern_rules]")
{
    REQUIRE(PatternRules::applyExclusion(2, -1, StructureState::SOFT, 2) == 2);
    REQUIRE(PatternRules::applyExclusion(3, 2, StructureState::SOFT, 3) == 3);
    REQUIRE(PatternRules::applyExclusion(0, 1, StructureState::SILENT, 0) == 0);
}
