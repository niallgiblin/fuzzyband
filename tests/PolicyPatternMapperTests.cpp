#include <catch2/catch_test_macros.hpp>
#include "analysis/FeatureVector.h"
#include "inference/PolicyPatternMapper.h"

TEST_CASE("PolicyPatternMapper variation 0.5 is identity (no genre shift)", "[policy]")
{
    FeatureVector fv;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(3, fv) == 3);
}

TEST_CASE("PolicyPatternMapper variation 0.5 is identity for base 2", "[policy]")
{
    FeatureVector fv;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(2, fv) == 2);
}
