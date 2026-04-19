#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include "analysis/FeatureVector.h"
#include "inference/PolicyPatternMapper.h"

// PolicyPatternMapper::applyPatternPolicy(base, fv):
//
//   idx = clamp(base, 0, 6)
//   idx = (idx + policyGenreIndex) % 7
//   varShift = lround(policyVariation * 2) - 1   // variation 0.0→-1, 0.5→0, 1.0→+1
//   idx = (idx + varShift + 7) % 7
//   return idx

// ─── Genre mapping ────────────────────────────────────────────────────────────

TEST_CASE("PolicyPatternMapper: each genre index (0-4) produces a distinct output for fixed base", "[policy]")
{
    // At base=2, variation=0.5 (varShift=0), genres 0-4 shift the index by 0-4 respectively
    // Expected: (2+g)%7 for g in [0..4] = {2, 3, 4, 5, 6} — all distinct
    std::array<int, 5> results{};
    for (int g = 0; g < 5; ++g)
    {
        FeatureVector fv;
        fv.policyGenreIndex = g;
        fv.policyVariation  = 0.5f;
        results[static_cast<size_t>(g)] = PolicyPatternMapper::applyPatternPolicy(2, fv);
    }

    // All five values must be unique
    std::array<int, 5> sorted = results;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 4; ++i)
        REQUIRE(sorted[i] < sorted[i + 1]);
}

TEST_CASE("PolicyPatternMapper: genre 0 with variation 0.5 is identity for base 2", "[policy]")
{
    FeatureVector fv;
    fv.policyGenreIndex = 0;
    fv.policyVariation  = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(2, fv) == 2);
}

TEST_CASE("PolicyPatternMapper: genre 1 shifts base 3 to 4 at variation 0.5", "[policy]")
{
    FeatureVector fv;
    fv.policyGenreIndex = 1;
    fv.policyVariation  = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(3, fv) == 4);
}

TEST_CASE("PolicyPatternMapper: genre 4 wraps around the 7-pattern ring", "[policy]")
{
    // base=4, genre=4 → (4+4)%7 = 1; variation=0.5 → varShift=0 → result=1
    FeatureVector fv;
    fv.policyGenreIndex = 4;
    fv.policyVariation  = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(4, fv) == 1);
}

// ─── Variation extremes ───────────────────────────────────────────────────────

TEST_CASE("PolicyPatternMapper: variation 0.0 shifts index by -1 (mod 7)", "[policy]")
{
    // varShift = lround(0.0 * 2) - 1 = -1
    // genre=0, base=3 → idx=3, varShift=-1 → (3 - 1 + 7) % 7 = 2
    FeatureVector fv;
    fv.policyGenreIndex = 0;
    fv.policyVariation  = 0.0f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(3, fv) == 2);
}

TEST_CASE("PolicyPatternMapper: variation 1.0 shifts index by +1", "[policy]")
{
    // varShift = lround(1.0 * 2) - 1 = 1
    // genre=0, base=3 → idx=3, varShift=+1 → (3 + 1 + 7) % 7 = 4
    FeatureVector fv;
    fv.policyGenreIndex = 0;
    fv.policyVariation  = 1.0f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(3, fv) == 4);
}

TEST_CASE("PolicyPatternMapper: variation 0.5 is identity shift (varShift == 0)", "[policy]")
{
    // varShift = lround(0.5 * 2) - 1 = lround(1.0) - 1 = 0
    FeatureVector fv;
    fv.policyGenreIndex = 0;
    fv.policyVariation  = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(5, fv) == 5);
}

// ─── Output range clamping ────────────────────────────────────────────────────

TEST_CASE("PolicyPatternMapper: result is always in [0, 6] for all genre/variation combos", "[policy]")
{
    for (int base = 0; base <= 6; ++base)
    {
        for (int g = 0; g < 5; ++g)
        {
            for (float var : { 0.0f, 0.5f, 1.0f })
            {
                FeatureVector fv;
                fv.policyGenreIndex = g;
                fv.policyVariation  = var;
                const int result = PolicyPatternMapper::applyPatternPolicy(base, fv);
                REQUIRE(result >= 0);
                REQUIRE(result <= 6);
            }
        }
    }
}

TEST_CASE("PolicyPatternMapper: negative base is clamped to 0 before genre shift", "[policy]")
{
    // base is clamped to [0,6]: clamp(-1, 0, 6) = 0
    // genre=2, variation=0.5 → (0+2)%7=2
    FeatureVector fv;
    fv.policyGenreIndex = 2;
    fv.policyVariation  = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(-1, fv) == 2);
}

TEST_CASE("PolicyPatternMapper: base above 6 is clamped to 6 before genre shift", "[policy]")
{
    // clamp(10, 0, 6) = 6; genre=0, variation=0.5 → 6
    FeatureVector fv;
    fv.policyGenreIndex = 0;
    fv.policyVariation  = 0.5f;
    REQUIRE(PolicyPatternMapper::applyPatternPolicy(10, fv) == 6);
}
