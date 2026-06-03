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

// Helper for diversifier tests — sets energy and centroid fields
static FeatureVector makeFD(StructureState state, float bpm, float rms, float centroid,
                             float policyIntensity = 0.5f)
{
    FeatureVector f;
    f.state = state;
    f.bpm = bpm;
    f.rmsEnergy = rms;
    f.spectralCentroid = centroid;
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

TEST_CASE("PatternRules::isPatternCompatibleWithState expanded for new indices", "[pattern_rules]")
{
    // SOFT: indices 1-3 and 7
    REQUIRE(PatternRules::isPatternCompatibleWithState(7, StructureState::SOFT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(8, StructureState::SOFT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(9, StructureState::SOFT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(10, StructureState::SOFT) == false);

    // LOUD: indices 4-5 and 8-10
    REQUIRE(PatternRules::isPatternCompatibleWithState(8, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(9, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(10, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(7, StructureState::LOUD) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(3, StructureState::LOUD) == false);

    // SILENT: still only 0
    REQUIRE(PatternRules::isPatternCompatibleWithState(7, StructureState::SILENT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(8, StructureState::SILENT) == false);
}

TEST_CASE("PatternRules::isOnnxPatternAcceptable expanded for indices 7-10", "[pattern_rules]")
{
    // Index 7 (half-time) accepted for SOFT, rejected for LOUD and SILENT
    REQUIRE(PatternRules::isOnnxPatternAcceptable(7, makeF(StructureState::SOFT, 100.0f)) == true);
    REQUIRE(PatternRules::isOnnxPatternAcceptable(7, makeF(StructureState::LOUD, 100.0f)) == false);
    REQUIRE(PatternRules::isOnnxPatternAcceptable(7, makeF(StructureState::SILENT, 100.0f)) == false);

    // Index 8 (blast) accepted for LOUD only
    REQUIRE(PatternRules::isOnnxPatternAcceptable(8, makeF(StructureState::LOUD, 180.0f)) == true);
    REQUIRE(PatternRules::isOnnxPatternAcceptable(8, makeF(StructureState::SOFT, 180.0f)) == false);

    // Index 9 (sparse) accepted for LOUD only
    REQUIRE(PatternRules::isOnnxPatternAcceptable(9, makeF(StructureState::LOUD, 100.0f)) == true);
    REQUIRE(PatternRules::isOnnxPatternAcceptable(9, makeF(StructureState::SOFT, 100.0f)) == false);

    // Index 10 (thrash) accepted for LOUD only
    REQUIRE(PatternRules::isOnnxPatternAcceptable(10, makeF(StructureState::LOUD, 150.0f)) == true);
    REQUIRE(PatternRules::isOnnxPatternAcceptable(10, makeF(StructureState::SOFT, 150.0f)) == false);
}

TEST_CASE("PatternRules::diversifyPattern SOFT low-energy routes to half-time", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::SOFT, 110.0f, 0.03f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(1, f, 1) == 7);
    REQUIRE(PatternRules::diversifyPattern(2, f, 1) == 7);
    REQUIRE(PatternRules::diversifyPattern(3, f, 1) == 7);
}

TEST_CASE("PatternRules::diversifyPattern SOFT high-energy stays on base", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::SOFT, 110.0f, 0.10f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(1, f, 1) == 1);
    REQUIRE(PatternRules::diversifyPattern(2, f, 0) == 2);
    REQUIRE(PatternRules::diversifyPattern(3, f, 3) == 3);
}

TEST_CASE("PatternRules::diversifyPattern SOFT high bar-phase stays on base", "[pattern_rules][diversify]")
{
    // rms low but barMod8%4 >= 2 → no diversify
    FeatureVector f = makeFD(StructureState::SOFT, 110.0f, 0.03f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(1, f, 2) == 1);
    REQUIRE(PatternRules::diversifyPattern(2, f, 3) == 2);
}

TEST_CASE("PatternRules::diversifyPattern LOUD high-BPM high-centroid routes to blast", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::LOUD, 180.0f, 0.10f, 900.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 0) == 8);
    REQUIRE(PatternRules::diversifyPattern(5, f, 0) == 8);
}

TEST_CASE("PatternRules::diversifyPattern LOUD high-BPM even-bar routes to thrash", "[pattern_rules][diversify]")
{
    // bpm >= 140, centroid not high enough for blast, even bar → thrash
    FeatureVector f = makeFD(StructureState::LOUD, 150.0f, 0.10f, 700.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 2) == 10);
    REQUIRE(PatternRules::diversifyPattern(5, f, 0) == 10);
}

TEST_CASE("PatternRules::diversifyPattern LOUD low-energy routes to sparse", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::LOUD, 100.0f, 0.04f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 0) == 9);
    REQUIRE(PatternRules::diversifyPattern(5, f, 0) == 9);
}

TEST_CASE("PatternRules::diversifyPattern LOUD no condition matches stays on base", "[pattern_rules][diversify]")
{
    // mid-BPM, mid-energy, odd bar → no route applies
    FeatureVector f = makeFD(StructureState::LOUD, 145.0f, 0.10f, 700.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 1) == 4);
    REQUIRE(PatternRules::diversifyPattern(5, f, 1) == 5);
}

TEST_CASE("PatternRules::diversifyPattern Breakdown routes to sparse", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::LOUD, 100.0f, 0.02f, 400.0f);
    REQUIRE(PatternRules::diversifyPattern(6, f, 0) == 9);
    REQUIRE(PatternRules::diversifyPattern(6, f, 4) == 9);
}

TEST_CASE("PatternRules::diversifyPattern Breakdown stays on base when not sparse", "[pattern_rules][diversify]")
{
    // energy not low enough or wrong bar phase
    FeatureVector f = makeFD(StructureState::LOUD, 100.0f, 0.05f, 400.0f);
    REQUIRE(PatternRules::diversifyPattern(6, f, 0) == 6);

    // low energy but barMod8%4 != 0
    FeatureVector f2 = makeFD(StructureState::LOUD, 100.0f, 0.02f, 400.0f);
    REQUIRE(PatternRules::diversifyPattern(6, f2, 1) == 6);
}

TEST_CASE("PatternRules::diversifyPattern SILENT always returns 0", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::SILENT, 100.0f, 0.02f, 400.0f);
    REQUIRE(PatternRules::diversifyPattern(0, f, 0) == 0);
    REQUIRE(PatternRules::diversifyPattern(0, f, 1) == 0);
}

TEST_CASE("PatternRules::diversifyPattern new indices 7-10 map to themselves", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::SOFT, 100.0f, 0.02f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(7, f, 0) == 7);
    REQUIRE(PatternRules::diversifyPattern(8, f, 0) == 8);
    REQUIRE(PatternRules::diversifyPattern(9, f, 0) == 9);
    REQUIRE(PatternRules::diversifyPattern(10, f, 0) == 10);

    // Even with conditions that would trigger routing, they stay
    FeatureVector f2 = makeFD(StructureState::LOUD, 180.0f, 0.10f, 900.0f);
    REQUIRE(PatternRules::diversifyPattern(7, f2, 0) == 7);
    REQUIRE(PatternRules::diversifyPattern(8, f2, 0) == 8);
    REQUIRE(PatternRules::diversifyPattern(9, f2, 0) == 9);
    REQUIRE(PatternRules::diversifyPattern(10, f2, 0) == 10);
}

TEST_CASE("PatternRules::diversifyPattern is deterministic", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::LOUD, 180.0f, 0.10f, 900.0f);
    for (int i = 0; i < 100; ++i)
        REQUIRE(PatternRules::diversifyPattern(4, f, 0) == 8);

    FeatureVector f2 = makeFD(StructureState::SOFT, 110.0f, 0.03f, 500.0f);
    for (int i = 0; i < 100; ++i)
        REQUIRE(PatternRules::diversifyPattern(1, f2, 1) == 7);
}

TEST_CASE("PatternRules exclusion wraps correctly with kPatternCount=11", "[pattern_rules]")
{
    // When LOUD base=5, exclude=5 → scan finds 8 (blast, next LOUD-compatible at index 8)
    FeatureVector f = makeF(StructureState::LOUD, 170.0f);
    const int out = PatternRules::applyExclusion(5, 5, StructureState::LOUD, 5);
    REQUIRE(out == 8);
    REQUIRE(PatternRules::isPatternCompatibleWithState(out, StructureState::LOUD));

    // SOFT base=3, exclude=3 → should wrap to 1 (or 7 if that's next in scan)
    // Scan: 4→not SOFT, 5→not SOFT, 6→not SOFT, 7→SOFT! So first hit is 7.
    const int outSoft = PatternRules::applyExclusion(3, 3, StructureState::SOFT, 3);
    REQUIRE(outSoft == 7);
    REQUIRE(PatternRules::isPatternCompatibleWithState(outSoft, StructureState::SOFT));
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
    REQUIRE(out == 8);
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
