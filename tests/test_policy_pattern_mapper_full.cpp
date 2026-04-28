#include <catch2/catch_test_macros.hpp>
#include "analysis/FeatureVector.h"
#include "inference/PolicyPatternMapper.h"

// PolicyPatternMapper::applyPatternPolicy(base, fv):
//
//   idx = clamp(base, 0, 6)
//   idx = idx % 7
//   varShift = lround(policyVariation * 2) - 1   // variation 0.0→-1, 0.5→0, 1.0→+1
//   idx = (idx + varShift + 7) % 7
//   return idx

// ─── Identity mapping (variation=0.5, no genre shift) ────────────────────────

TEST_CASE("PolicyPatternMapper: variation 0.5 is identity for base 2", "[policy]")
{
    FeatureVector fv;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(2, fv) == 2);
}

TEST_CASE("PolicyPatternMapper: base above 6 is clamped to 6 before modulo", "[policy]")
{
    // clamp(10, 0, 6) = 6; variation=0.5 → varShift=0 → result=6
    FeatureVector fv;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(10, fv) == 6);
}

// ─── Variation extremes ───────────────────────────────────────────────────────

TEST_CASE("PolicyPatternMapper: variation 0.0 shifts index by -1 (mod 7)", "[policy]")
{
    // varShift = lround(0.0 * 2) - 1 = -1
    // base=3 → idx=3, varShift=-1 → (3 - 1 + 7) % 7 = 2
    FeatureVector fv;
    fv.policyVariation = 0.0f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(3, fv) == 2);
}

TEST_CASE("PolicyPatternMapper: variation 1.0 shifts index by +1", "[policy]")
{
    // varShift = lround(1.0 * 2) - 1 = 1
    // base=3 → idx=3, varShift=+1 → (3 + 1 + 7) % 7 = 4
    FeatureVector fv;
    fv.policyVariation = 1.0f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(3, fv) == 4);
}

TEST_CASE("PolicyPatternMapper: variation 0.5 is identity shift (varShift == 0)", "[policy]")
{
    // varShift = lround(0.5 * 2) - 1 = lround(1.0) - 1 = 0
    FeatureVector fv;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(5, fv) == 5);
}

// ─── Output range clamping ────────────────────────────────────────────────────

TEST_CASE("PolicyPatternMapper: result is always in [0, 6] for all base/variation combos", "[policy]")
{
    for (int base = 0; base <= 6; ++base)
    {
        for (float var : { 0.0f, 0.5f, 1.0f })
        {
            FeatureVector fv;
            fv.policyVariation = var;
            const int result = PolicyPatternMapper::applyPatternPolicy(base, fv);
            REQUIRE(result >= 0);
            REQUIRE(result <= 6);
        }
    }
}

TEST_CASE("PolicyPatternMapper: negative base is clamped to 0 before modulo", "[policy]")
{
    // base is clamped to [0,6]: clamp(-1, 0, 6) = 0
    // variation=0.5 → varShift=0 → result=0
    FeatureVector fv;
    fv.policyVariation = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(-1, fv) == 0);
}
