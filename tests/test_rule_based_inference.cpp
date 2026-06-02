#include <catch2/catch_test_macros.hpp>
#include "inference/RuleBasedInference.h"
#include "inference/pattern_rules.h"
#include "analysis/FeatureVector.h"

TEST_CASE("RuleBasedInference maps states to pattern indices", "[inference]")
{
    RuleBasedInference inf;
    inf.prepare(44100.0);

    FeatureVector f;

    f.state = StructureState::SILENT;
    REQUIRE(inf.selectPattern(f) == 0);

    f.state = StructureState::SOFT;
    f.bpm = 100.0f;
    REQUIRE(inf.selectPattern(f) == 1);
    f.bpm = 140.0f;
    REQUIRE(inf.selectPattern(f) == 2);
    f.bpm = 170.0f;
    REQUIRE(inf.selectPattern(f) == 3);

    f.state = StructureState::LOUD;
    f.bpm = 150.0f;
    REQUIRE(inf.selectPattern(f) == 4);
    f.bpm = 170.0f;
    REQUIRE(inf.selectPattern(f) == 5);
}

TEST_CASE("RuleBasedInference exclusion never returns excluded index", "[inference]")
{
    RuleBasedInference inf;
    inf.prepare(44100.0);

    FeatureVector f;
    f.policyIntensity = 0.5f;

    // Test all three states across all possible excludeIndex values
    struct TestCase { StructureState state; float bpm; float policyIntensity; };
    TestCase cases[] = {
        { StructureState::SILENT, 120.0f, 0.5f },
        { StructureState::SOFT,   100.0f, 0.5f },
        { StructureState::SOFT,   140.0f, 0.5f },
        { StructureState::SOFT,   170.0f, 0.5f },
        { StructureState::SOFT,   130.0f, 0.0f },
        { StructureState::SOFT,   110.0f, 1.0f },
        { StructureState::LOUD,   150.0f, 0.5f },
        { StructureState::LOUD,   170.0f, 0.5f },
    };

    for (const auto& tc : cases)
    {
        f.state = tc.state;
        f.bpm = tc.bpm;
        f.policyIntensity = tc.policyIntensity;
        for (int exclude = 0; exclude <= 6; ++exclude)
        {
            const int result = inf.selectPattern(f, exclude);
            REQUIRE(PatternRules::isPatternCompatibleWithState(result, tc.state));
            if (!(tc.state == StructureState::SILENT && exclude == 0))
                REQUIRE(result != exclude);
        }
    }
}

TEST_CASE("RuleBasedInference exclusion default preserves original behavior", "[inference]")
{
    RuleBasedInference inf;
    inf.prepare(44100.0);

    FeatureVector f;
    f.state = StructureState::SILENT;
    REQUIRE(inf.selectPattern(f) == 0);
    REQUIRE(inf.selectPattern(f, -1) == 0);

    f.state = StructureState::SOFT;
    f.bpm = 100.0f;
    REQUIRE(inf.selectPattern(f) == 1);
    REQUIRE(inf.selectPattern(f, -1) == 1);

    f.state = StructureState::LOUD;
    f.bpm = 170.0f;
    REQUIRE(inf.selectPattern(f) == 5);
    REQUIRE(inf.selectPattern(f, -1) == 5);
}
