#include <catch2/catch_test_macros.hpp>
#include "inference/RuleBasedInference.h"
#include "analysis/FeatureVector.h"

TEST_CASE("RuleBasedInference maps states to pattern indices", "[inference]")
{
    RuleBasedInference inf;
    inf.prepare(44100.0);

    FeatureVector f;

    f.state = StructureState::SILENT;
    REQUIRE(inf.selectPattern(f) == 0);

    f.state = StructureState::VERSE;
    f.bpm = 100.0f;
    REQUIRE(inf.selectPattern(f) == 1);
    f.bpm = 140.0f;
    REQUIRE(inf.selectPattern(f) == 2);
    f.bpm = 170.0f;
    REQUIRE(inf.selectPattern(f) == 3);

    f.state = StructureState::CHORUS;
    f.bpm = 150.0f;
    REQUIRE(inf.selectPattern(f) == 4);
    f.bpm = 170.0f;
    REQUIRE(inf.selectPattern(f) == 5);

    f.state = StructureState::BREAKDOWN;
    REQUIRE(inf.selectPattern(f) == 6);
}
