#include <catch2/catch_test_macros.hpp>
#include "analysis/FeatureVector.h"
#include "inference/PolicyPatternMapper.h"

TEST_CASE("PolicyPatternMapper genre shifts pattern index", "[policy]")
{
    FeatureVector fv;
    fv.policyGenreIndex = 1;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(3, fv) == 4);
}

TEST_CASE("PolicyPatternMapper variation at 0.5 is identity for base 2 with genre 0", "[policy]")
{
    FeatureVector fv;
    fv.policyGenreIndex = 0;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(2, fv) == 2);
}
