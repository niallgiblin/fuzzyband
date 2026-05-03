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

TEST_CASE("PatternRules::applyExclusion wraps to next index", "[pattern_rules]")
{
    // No exclusion (-1) → unchanged
    REQUIRE(PatternRules::applyExclusion(2, -1) == 2);

    // Exclusion matches → next index via %7
    REQUIRE(PatternRules::applyExclusion(2, 2) == 3);

    // Wrap around: index 6 excluded → 0
    REQUIRE(PatternRules::applyExclusion(6, 6) == 0);

    // No match → original returned
    REQUIRE(PatternRules::applyExclusion(3, 2) == 3);
    REQUIRE(PatternRules::applyExclusion(0, 1) == 0);
}
