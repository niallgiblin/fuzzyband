#include <catch2/catch_test_macros.hpp>
#include "inference/pattern_rules.h"
#include "analysis/FeatureVector.h"
#include "midi/MidiPatternLibrary.h"

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
    // Lower clamp: bpm=40, intensity=0.0 → raw 20, clamped to 40
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::SOFT, 40.0f, 0.0f)) == 40.0f);

    // Upper clamp: bpm=300, intensity=1.0 → raw 320, clamped to 300
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::LOUD, 300.0f, 1.0f)) == 300.0f);

    // bpm=60, intensity=0.0 → raw 40
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::SOFT, 60.0f, 0.0f)) == 40.0f);

    // bpm=280, intensity=1.0 → raw 300
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::LOUD, 280.0f, 1.0f)) == 300.0f);

    // Interior regression: neutral intensity unchanged
    REQUIRE(PatternRules::adjustedBpm(makeF(StructureState::SOFT, 140.0f, 0.5f)) == 140.0f);
}

TEST_CASE("PatternRules::isOnnxPatternAcceptable state compat", "[pattern_rules]")
{
    // LOUD → pattern 6 acceptable (now LOUD-compatible)
    REQUIRE(PatternRules::isOnnxPatternAcceptable(6, makeF(StructureState::LOUD, 100.0f)) == true);

    // SOFT → pattern 6 rejected (not SOFT-compatible)
    REQUIRE(PatternRules::isOnnxPatternAcceptable(6, makeF(StructureState::SOFT, 100.0f)) == false);

    // SILENT → pattern 6 rejected
    REQUIRE(PatternRules::isOnnxPatternAcceptable(6, makeF(StructureState::SILENT, 100.0f)) == false);

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

TEST_CASE("PatternRules::refineByRhythm steers a dense chug toward a denser groove", "[pattern_rules][rhythm]")
{
    // Dense picking (>=1.8 attacks/beat, 8ths+) → denser groove, state-compatible.
    FeatureVector dense = makeF(StructureState::LOUD, 120.0f);
    dense.onsetDensityPerBeat = 2.5f;
    REQUIRE(PatternRules::refineByRhythm(4, dense) == 10);  // LOUD+8ths → thrash

    FeatureVector denseSoft = makeF(StructureState::SOFT, 120.0f);
    denseSoft.onsetDensityPerBeat = 2.5f;
    REQUIRE(PatternRules::refineByRhythm(1, denseSoft) == 3);  // SOFT+8ths → verse fast

    // Mid / sparse rhythm leaves the base untouched (a LOUD sustained tone has few
    // attacks but is NOT "sparse playing", so density must not soften it).
    FeatureVector mid = makeF(StructureState::LOUD, 120.0f);
    mid.onsetDensityPerBeat = 1.0f;
    REQUIRE(PatternRules::refineByRhythm(4, mid) == 4);
    FeatureVector sparse = makeF(StructureState::LOUD, 120.0f);
    sparse.onsetDensityPerBeat = 0.4f;
    REQUIRE(PatternRules::refineByRhythm(4, sparse) == 4);

    // Silence stays silent.
    FeatureVector silent = makeF(StructureState::SILENT, 120.0f);
    silent.onsetDensityPerBeat = 4.0f;
    REQUIRE(PatternRules::refineByRhythm(0, silent) == 0);
}

TEST_CASE("PatternRules::isPatternCompatibleWithState expanded for new indices", "[pattern_rules]")
{
    // SOFT: indices 1-3, 7, 20
    REQUIRE(PatternRules::isPatternCompatibleWithState(7, StructureState::SOFT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(20, StructureState::SOFT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(8, StructureState::SOFT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(9, StructureState::SOFT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(10, StructureState::SOFT) == false);

    // LOUD: patterns 4-6, 8-10, 13-15
    REQUIRE(PatternRules::isPatternCompatibleWithState(4, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(5, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(6, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(8, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(9, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(10, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(13, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(14, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(15, StructureState::LOUD) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(7, StructureState::LOUD) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(3, StructureState::LOUD) == false);

    // SILENT: still only 0
    REQUIRE(PatternRules::isPatternCompatibleWithState(0, StructureState::SILENT) == true);
    REQUIRE(PatternRules::isPatternCompatibleWithState(1, StructureState::SILENT) == false);
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

TEST_CASE("PatternRules::diversifyPattern SOFT widened half-time window: barMod8 0-5 routes to half-time", "[pattern_rules][diversify]")
{
    // MEM009: Widened from (barMod8%4 < 2) to (barMod8%8 < 6)
    // Half-time fires on 6 of 8 bars for sludge feel
    FeatureVector f = makeFD(StructureState::SOFT, 110.0f, 0.03f, 500.0f);
    // barMod8=0-5 → half-time
    for (int bar = 0; bar <= 5; ++bar)
        REQUIRE(PatternRules::diversifyPattern(1, f, bar) == 7);
    // barMod8=6,7 → stays on base
    REQUIRE(PatternRules::diversifyPattern(1, f, 6) == 1);
    REQUIRE(PatternRules::diversifyPattern(1, f, 7) == 1);
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

TEST_CASE("PatternRules::diversifyPattern LOUD sludge half-time: BPM < 85, bars 0-1", "[pattern_rules][diversify]")
{
    // Sludge metal at 65 BPM — half-time on bars 0-1 of each 4-bar group
    FeatureVector f = makeFD(StructureState::LOUD, 65.0f, 0.10f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 0) == 7);  // bar 0 → half-time
    REQUIRE(PatternRules::diversifyPattern(4, f, 1) == 7);  // bar 1 → half-time
    REQUIRE(PatternRules::diversifyPattern(4, f, 4) == 7);  // bar 4 → half-time (next 4-bar group)
    REQUIRE(PatternRules::diversifyPattern(5, f, 0) == 7);  // base 5 also gets half-time
    // Bars 2-3 stay standard
    REQUIRE(PatternRules::diversifyPattern(4, f, 2) == 4);
    REQUIRE(PatternRules::diversifyPattern(4, f, 3) == 4);
}

TEST_CASE("PatternRules::diversifyPattern LOUD sludge half-time skips at BPM >= 85", "[pattern_rules][diversify]")
{
    // At 90 BPM (just above sludge threshold), half-time should NOT trigger
    FeatureVector f = makeFD(StructureState::LOUD, 90.0f, 0.10f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 0) != 7);
    REQUIRE(PatternRules::diversifyPattern(4, f, 1) != 7);
}

TEST_CASE("PatternRules::diversifyPattern LOUD half-time is not LOUD-compatible but routes anyway", "[pattern_rules][diversify]")
{
    // Pattern 7 is not LOUD-compatible per isPatternCompatibleWithState,
    // but diversifyPattern deliberately returns it for sludge musical variety.
    REQUIRE(PatternRules::isPatternCompatibleWithState(7, StructureState::LOUD) == false);
    FeatureVector f = makeFD(StructureState::LOUD, 65.0f, 0.10f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 0) == 7);
}

TEST_CASE("PatternRules::diversifyPattern LOUD half-time checked before sparse", "[pattern_rules][diversify]")
{
    // BPM=70, rms=0.04, barMod8=0: half-time wins over sparse
    FeatureVector f = makeFD(StructureState::LOUD, 70.0f, 0.04f, 500.0f);
    REQUIRE(PatternRules::diversifyPattern(4, f, 0) == 7);  // half-time first
    // barMod8=2: half-time skipped, sparse takes over
    REQUIRE(PatternRules::diversifyPattern(4, f, 2) == 9);  // sparse
}

TEST_CASE("PatternRules::diversifyPattern pattern 6 routes to sparse", "[pattern_rules][diversify]")
{
    FeatureVector f = makeFD(StructureState::LOUD, 100.0f, 0.02f, 400.0f);
    REQUIRE(PatternRules::diversifyPattern(6, f, 0) == 9);
    REQUIRE(PatternRules::diversifyPattern(6, f, 4) == 9);
}

TEST_CASE("PatternRules::diversifyPattern pattern 6 stays on base when not sparse", "[pattern_rules][diversify]")
{
    // energy not low enough
    FeatureVector f = makeFD(StructureState::LOUD, 100.0f, 0.10f, 400.0f);
    REQUIRE(PatternRules::diversifyPattern(6, f, 0) == 6);

    // low energy but BPM >= 140 counteracts sparse routing
    FeatureVector f2 = makeFD(StructureState::LOUD, 145.0f, 0.02f, 400.0f);
    // bpm >= 140 and (barMod8 % 2) == 0 → thrash on even bars
    REQUIRE(PatternRules::diversifyPattern(6, f2, 0) == 10);
    // bpm >= 140 and (barMod8 % 2) != 0 → stay on base on odd bars
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

TEST_CASE("PatternRules exclusion wraps correctly with kPatternCount=22", "[pattern_rules]")
{
    // When LOUD base=5, exclude=5 → scan finds 6 (next LOUD-compatible)
    FeatureVector f = makeF(StructureState::LOUD, 170.0f);
    const int out = PatternRules::applyExclusion(5, 5, StructureState::LOUD, 5);
    REQUIRE(out == 6);
    REQUIRE(PatternRules::isPatternCompatibleWithState(out, StructureState::LOUD));

    // SOFT base=3, exclude=3 → should wrap to 7 (next SOFT-compatible)
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

    // SOFT: indices 1-3 and 7
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

TEST_CASE("PatternRules exclusion finds next LOUD-compatible after 5", "[pattern_rules]")
{
    FeatureVector f = makeF(StructureState::LOUD, 170.0f);
    const int out = PatternRules::applyExclusion(5, 5, StructureState::LOUD, 5);
    REQUIRE(out == 6);  // pattern 6 is now LOUD-compatible
    REQUIRE(PatternRules::isPatternCompatibleWithState(out, StructureState::LOUD));
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

// ── Rock-first set compatibility (A4.1 / B1) ─────────────────────────────────

TEST_CASE("PatternRules::kPatternCount stays in sync with the library", "[pattern_rules]")
{
    REQUIRE(PatternRules::kPatternCount == MidiPatternLibrary::kPatternCount);
    MidiPatternLibrary lib;
    REQUIRE(PatternRules::kPatternCount == lib.patternCount());
}

TEST_CASE("PatternRules::isPatternCompatibleWithState covers rock-first indices 22-27", "[pattern_rules][rock]")
{
    // SOFT-compatible: 22, 23, 24, 26, 27
    for (int idx : { 22, 23, 24, 26, 27 })
        REQUIRE(PatternRules::isPatternCompatibleWithState(idx, StructureState::SOFT) == true);
    // LOUD-compatible: 25 (Punk D-Beat)
    REQUIRE(PatternRules::isPatternCompatibleWithState(25, StructureState::LOUD) == true);
    // Cross checks
    REQUIRE(PatternRules::isPatternCompatibleWithState(25, StructureState::SOFT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(22, StructureState::LOUD) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(22, StructureState::SILENT) == false);
    REQUIRE(PatternRules::isPatternCompatibleWithState(24, StructureState::LOUD) == false);
}

TEST_CASE("PatternRules::diversifyPatternForGenre metal genre keeps metal routing", "[pattern_rules][rock]")
{
    // genre 3 (Metal): identical to diversifyPattern
    FeatureVector f = makeFD(StructureState::SOFT, 110.0f, 0.03f, 500.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(1, f, 1, 3) == PatternRules::diversifyPattern(1, f, 1));
    REQUIRE(PatternRules::diversifyPatternForGenre(1, f, 1, 4) == PatternRules::diversifyPattern(1, f, 1));
}

TEST_CASE("PatternRules::diversifyPatternForGenre rock SOFT low-energy routes to rock patterns", "[pattern_rules][rock]")
{
    FeatureVector f = makeFD(StructureState::SOFT, 110.0f, 0.03f, 500.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(1, f, 0, 0) == 22);  // Rock Backbeat
    REQUIRE(PatternRules::diversifyPatternForGenre(1, f, 1, 0) == 23);  // Rock Half-Time
    REQUIRE(PatternRules::diversifyPatternForGenre(2, f, 0, 1) == 22);
    REQUIRE(PatternRules::diversifyPatternForGenre(3, f, 3, 2) == 23);
}

TEST_CASE("PatternRules::diversifyPatternForGenre rock SOFT high-energy stays on base", "[pattern_rules][rock]")
{
    FeatureVector f = makeFD(StructureState::SOFT, 110.0f, 0.10f, 500.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(1, f, 1, 0) == 1);
    REQUIRE(PatternRules::diversifyPatternForGenre(2, f, 0, 0) == 2);
}

TEST_CASE("PatternRules::diversifyPatternForGenre rock LOUD mid-tempo low-energy routes to shuffle/d-beat", "[pattern_rules][rock]")
{
    FeatureVector f = makeFD(StructureState::LOUD, 110.0f, 0.07f, 600.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(4, f, 0, 0) == 24);  // Rock Shuffle
    REQUIRE(PatternRules::diversifyPatternForGenre(4, f, 1, 0) == 25);  // Punk D-Beat
    // High energy stays on base (chorus mid)
    FeatureVector loud = makeFD(StructureState::LOUD, 120.0f, 0.15f, 600.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(4, loud, 0, 0) == 4);
}

TEST_CASE("PatternRules::sectionPatternPoolForGenre rock pools prefer rock patterns", "[pattern_rules][rock]")
{
    const auto verse = PatternRules::sectionPatternPoolForGenre("VERSE", 0);
    REQUIRE(verse.count > 0);
    REQUIRE(verse.indices[0] == 22);
    const auto chorus = PatternRules::sectionPatternPoolForGenre("CHORUS", 1);
    REQUIRE(chorus.count > 0);
    // Metal genre falls back to the original pools
    const auto metalVerse = PatternRules::sectionPatternPoolForGenre("VERSE", 3);
    REQUIRE(metalVerse.indices[0] == 1);
}

// ── C2 (DATA_STRATEGY.md §6.2): Lakh selection priors → pool ordering ─────────

TEST_CASE("PatternRules::priorWeight is bounded and clamps out-of-range", "[pattern_rules][C2]")
{
    for (int g = 0; g < PatternPriors::kNumGenres; ++g)
        for (int p = 0; p < PatternPriors::kPatternCount; ++p)
        {
            const float w = PatternRules::priorWeight(p, g);
            REQUIRE(w >= 0.0f);
            REQUIRE(w <= 1.0f);
        }
    REQUIRE(PatternRules::priorWeight(0, -1) == 0.0f);
    REQUIRE(PatternRules::priorWeight(0, PatternPriors::kNumGenres) == 0.0f);
    REQUIRE(PatternRules::priorWeight(-1, 0) == 0.0f);
    REQUIRE(PatternRules::priorWeight(PatternPriors::kPatternCount, 0) == 0.0f);
}

TEST_CASE("PatternRules::orderPoolByPriors preserves membership, sorts by weight", "[pattern_rules][C2]")
{
    // Same set, reordered most-popular-first for the genre.
    const auto base = PatternRules::sectionPatternPoolForGenre("VERSE", 0);
    const auto ordered = PatternRules::orderPoolByPriors(base, 0);
    REQUIRE(ordered.count == base.count);

    // Membership is unchanged (multiset equality over a tiny pool).
    for (int i = 0; i < base.count; ++i)
    {
        bool found = false;
        for (int j = 0; j < ordered.count; ++j)
            if (ordered.indices[j] == base.indices[i]) found = true;
        REQUIRE(found);
    }
    // Weights are non-increasing along the ordered pool.
    for (int i = 1; i < ordered.count; ++i)
        REQUIRE(PatternRules::priorWeight(ordered.indices[i - 1], 0)
                >= PatternRules::priorWeight(ordered.indices[i], 0));

    // Out-of-range genre leaves the pool untouched.
    const auto passthrough = PatternRules::orderPoolByPriors(base, 999);
    for (int i = 0; i < base.count; ++i)
        REQUIRE(passthrough.indices[i] == base.indices[i]);
}

TEST_CASE("PatternRules::orderedSectionPatternPoolForGenre matches ordered membership", "[pattern_rules][C2]")
{
    const auto raw = PatternRules::sectionPatternPoolForGenre("CHORUS", 0);
    const auto ord = PatternRules::orderedSectionPatternPoolForGenre("CHORUS", 0);
    REQUIRE(ord.count == raw.count);
    for (int i = 1; i < ord.count; ++i)
        REQUIRE(PatternRules::priorWeight(ord.indices[i - 1], 0)
                >= PatternRules::priorWeight(ord.indices[i], 0));
}

// ─── A5.2: post-lock transition grammar ───────────────────────────────────────

TEST_CASE("PatternRules::sectionFamilyOfPattern maps patterns to their home section", "[pattern_rules][A5.2]")
{
    // Pattern 1 = verse groove → VERSE; 4 = chorus mid → CHORUS; 6 = breakdown → BREAKDOWN.
    REQUIRE(std::strcmp(PatternRules::sectionFamilyOfPattern(1, 0), "VERSE") == 0);
    REQUIRE(std::strcmp(PatternRules::sectionFamilyOfPattern(4, 0), "CHORUS") == 0);
    REQUIRE(std::strcmp(PatternRules::sectionFamilyOfPattern(6, 0), "BREAKDOWN") == 0);
    // Unknown indices fall back to a default family (still valid).
    REQUIRE(PatternRules::sectionFamilyOfPattern(999, 0) != nullptr);
}

TEST_CASE("PatternRules::pickNextSectionAfterLock never repeats the riff's own family", "[pattern_rules][A5.2]")
{
    // Locked onto a verse pattern → next section must NOT be VERSE-family.
    const auto ts = PatternRules::pickNextSectionAfterLock(1, 0, "");
    REQUIRE(ts.name != nullptr);
    REQUIRE(ts.pool.count > 0);
    REQUIRE(std::strcmp(ts.name, "VERSE") != 0);

    // Locked onto a breakdown pattern → next must not be BREAKDOWN.
    const auto ts2 = PatternRules::pickNextSectionAfterLock(6, 0, "");
    REQUIRE(std::strcmp(ts2.name, "BREAKDOWN") != 0);

    // Locked onto a chorus pattern → next must not be CHORUS.
    const auto ts3 = PatternRules::pickNextSectionAfterLock(4, 0, "");
    REQUIRE(std::strcmp(ts3.name, "CHORUS") != 0);
}

TEST_CASE("PatternRules::pickNextSectionAfterLock avoids the previously-played section", "[pattern_rules][A5.2]")
{
    // First transition → B. Second transition (avoiding B) must differ.
    const auto first = PatternRules::pickNextSectionAfterLock(1, 0, "");
    const auto second = PatternRules::pickNextSectionAfterLock(1, 0, first.name);
    REQUIRE(std::strcmp(second.name, first.name) != 0);
}

TEST_CASE("PatternRules::pickNextSectionAfterLock is deterministic and always returns a valid pool", "[pattern_rules][A5.2]")
{
    const auto a = PatternRules::pickNextSectionAfterLock(4, 0, "");
    const auto b = PatternRules::pickNextSectionAfterLock(4, 0, "");
    REQUIRE(std::strcmp(a.name, b.name) == 0);
    REQUIRE(a.pool.count > 0);

    // Works for every pattern index without crashing.
    for (int p = 0; p < MidiPatternLibrary::kPatternCount; ++p)
    {
        const auto ts = PatternRules::pickNextSectionAfterLock(p, 0, "");
        REQUIRE(ts.pool.count > 0);
    }
}

// ══════════════════════════════════════════════════════════════════════════
// Style steering (A4.1): diversifyPatternForStyle
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("PatternRules::diversifyPatternForStyle palm-mute routes to half-time/breakdown family", "[pattern_rules][style]")
{
    // Style pool {7,1,9} (half-time, verse groove, sparse breakdown).
    // SOFT state filters to {7,1}; rotation by bar phase.
    FeatureVector soft = makeFD(StructureState::SOFT, 100.0f, 0.08f, 400.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(1, 0, 0, soft.state) == 7);
    REQUIRE(PatternRules::diversifyPatternForStyle(1, 0, 1, soft.state) == 1);
    REQUIRE(PatternRules::diversifyPatternForStyle(1, 0, 2, soft.state) == 7);
    // LOUD state filters to {9} only (sparse breakdown).
    FeatureVector loud = makeFD(StructureState::LOUD, 140.0f, 0.2f, 700.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 0, 0, loud.state) == 9);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 0, 3, loud.state) == 9);
}

TEST_CASE("PatternRules::diversifyPatternForStyle open-chord routes to chorus/breakdown", "[pattern_rules][style]")
{
    // Style pool {4,6,14} (chorus mid, breakdown, open groove) — all LOUD-compatible.
    FeatureVector f = makeFD(StructureState::LOUD, 120.0f, 0.2f, 600.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 1, 0, f.state) == 4);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 1, 1, f.state) == 6);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 1, 2, f.state) == 14);
}

TEST_CASE("PatternRules::diversifyPatternForStyle single-note routes to fast family", "[pattern_rules][style]")
{
    // Style pool {3,10,2} (verse fast, thrash, verse half-time).
    FeatureVector f = makeFD(StructureState::SOFT, 140.0f, 0.12f, 800.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(3, 2, 0, f.state) == 3);
    REQUIRE(PatternRules::diversifyPatternForStyle(3, 2, 1, f.state) == 2);  // SOFT filters out 10
    // LOUD: only 10 (thrash) survives the state filter.
    FeatureVector loud = makeFD(StructureState::LOUD, 150.0f, 0.2f, 900.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(3, 2, 0, loud.state) == 10);
    REQUIRE(PatternRules::diversifyPatternForStyle(3, 2, 5, loud.state) == 10);
}

TEST_CASE("PatternRules::diversifyPatternForStyle silence/unknown/base-0 keep base", "[pattern_rules][style]")
{
    FeatureVector f = makeFD(StructureState::LOUD, 120.0f, 0.2f, 600.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 4, 0, f.state) == 4);   // silence style
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 5, 0, f.state) == 4);   // out-of-range style
    REQUIRE(PatternRules::diversifyPatternForStyle(4, -1, 0, f.state) == 4);  // negative style
    REQUIRE(PatternRules::diversifyPatternForStyle(0, 0, 0, f.state) == 0);   // silent base
}

// ══════════════════════════════════════════════════════════════════════════
// Pool phrasing & seeded rotation: pickPoolPattern / barsPerGrooveForSection
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("PatternRules::pickPoolPattern is deterministic per (seed, slot)", "[pattern_rules][pool]")
{
    const auto verse = PatternRules::sectionPatternPoolForGenre("VERSE", 0);  // {22,23,1,2}
    REQUIRE(verse.count >= 2);
    const int a = PatternRules::pickPoolPattern(verse, 7u, 0, -1);
    const int b = PatternRules::pickPoolPattern(verse, 7u, 0, -1);
    REQUIRE(a == b);
    REQUIRE(a >= 0);
    // Across 16 slots the rotation must visit more than one pool member
    // (the whole point of the seeded rotation).
    bool allSame = true;
    const int first = PatternRules::pickPoolPattern(verse, 7u, 0, -1);
    for (int s = 1; s < 16; ++s)
        if (PatternRules::pickPoolPattern(verse, 7u, s, -1) != first)
        {
            allSame = false;
            break;
        }
    REQUIRE_FALSE(allSame);
}

TEST_CASE("PatternRules::pickPoolPattern never repeats the excluded pattern", "[pattern_rules][pool]")
{
    const auto verse = PatternRules::sectionPatternPoolForGenre("VERSE", 0);  // {22,23,1,2}
    // Every pick must differ from the exclude (the previously-played groove).
    for (int slot = 0; slot < 64; ++slot)
    {
        const int pick = PatternRules::pickPoolPattern(verse, 3u, slot, 22);
        REQUIRE(pick >= 0);
        REQUIRE(pick != 22);
    }
    // Exclusion of a member not in the pool is harmless — the pick stays a member.
    const int pick2 = PatternRules::pickPoolPattern(verse, 3u, 0, 999);
    REQUIRE(pick2 >= 0);
    REQUIRE(pick2 != 999);
}

TEST_CASE("PatternRules::pickPoolPattern handles empty and single-member pools", "[pattern_rules][pool]")
{
    PatternRules::SectionPatternPool empty{ 0, {} };
    REQUIRE(PatternRules::pickPoolPattern(empty, 1u, 0, -1) == -1);

    PatternRules::SectionPatternPool one{ 1, { 16, 0, 0, 0 } };
    REQUIRE(PatternRules::pickPoolPattern(one, 1u, 0, -1) == 16);
    REQUIRE(PatternRules::pickPoolPattern(one, 1u, 0, 16) == 16);  // only member, excluded or not
}

TEST_CASE("PatternRules::barsPerGrooveForSection phrases sections", "[pattern_rules][pool]")
{
    REQUIRE(PatternRules::barsPerGrooveForSection("VERSE") == 2);
    REQUIRE(PatternRules::barsPerGrooveForSection("CHORUS") == 2);
    REQUIRE(PatternRules::barsPerGrooveForSection("SOLO") == 2);
    REQUIRE(PatternRules::barsPerGrooveForSection("BREAKDOWN") == 4);
    REQUIRE(PatternRules::barsPerGrooveForSection("INTRO") == 4);
    REQUIRE(PatternRules::barsPerGrooveForSection("OUTRO") == 4);
    REQUIRE(PatternRules::barsPerGrooveForSection(nullptr) == 2);
    REQUIRE(PatternRules::barsPerGrooveForSection("UNKNOWN") == 2);
}

// ══════════════════════════════════════════════════════════════════════════
// Fill variety: selectFillPattern
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("PatternRules::selectFillPattern sizes the last-bar fill to energy", "[pattern_rules][fill]")
{
    // Mid-section build always uses the short fill.
    REQUIRE(PatternRules::selectFillPattern(1, 0.8f, 0) == 17);
    REQUIRE(PatternRules::selectFillPattern(2, 0.05f, 7) == 17);
    // Quiet last bar → short fill.
    REQUIRE(PatternRules::selectFillPattern(0, 0.05f, 0) == 17);
    // Loud/mid last bar → the fill size varies with seed, never outside [17,19].
    for (unsigned s = 0; s < 64; ++s)
    {
        const int loud = PatternRules::selectFillPattern(0, 0.8f, s);
        REQUIRE(loud >= 18);
        REQUIRE(loud <= 19);
        const int mid = PatternRules::selectFillPattern(0, 0.3f, s);
        REQUIRE(mid >= 17);
        REQUIRE(mid <= 18);
    }
}

TEST_CASE("PatternRules::selectFillPattern Fill Medium is reachable (was dead)", "[pattern_rules][fill]")
{
    bool sawMedium = false;
    for (unsigned s = 0; s < 64; ++s)
        if (PatternRules::selectFillPattern(0, 0.8f, s) == 18) { sawMedium = true; break; }
    REQUIRE(sawMedium);
    // The loud tier also reaches the big fill, and seed varies the choice.
    bool sawBig = false;
    for (unsigned s = 0; s < 64; ++s)
        if (PatternRules::selectFillPattern(0, 0.8f, s) == 19) { sawBig = true; break; }
    REQUIRE(sawBig);
}

// ══════════════════════════════════════════════════════════════════════════
// G1-now / G2: genre-aware selection & style steering
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("PatternRules::stylePatternPoolForGenre rock-leaning genres use the rock-first vocabulary", "[pattern_rules][G2]")
{
    // Rock (0) and Punk (2) steer to rock patterns; Metal/Sludge (>=3) keep metal.
    const auto rockPalm = PatternRules::stylePatternPoolForGenre(0, 0);   // palm-mute chugs
    REQUIRE(rockPalm.count == 3);
    REQUIRE(rockPalm.indices[0] == 23);   // Rock Half-Time
    REQUIRE(rockPalm.indices[2] == 9);    // Sparse Breakdown (shared)

    const auto rockOpen = PatternRules::stylePatternPoolForGenre(1, 0);   // open chord
    REQUIRE(rockOpen.count == 3);
    REQUIRE(rockOpen.indices[0] == 22);   // Rock Backbeat

    const auto punkSingle = PatternRules::stylePatternPoolForGenre(2, 2); // single-note runs, Punk
    REQUIRE(punkSingle.count == 3);
    REQUIRE(punkSingle.indices[1] == 24); // Rock Shuffle
    REQUIRE(punkSingle.indices[2] == 25); // Punk D-Beat

    const auto rockSustain = PatternRules::stylePatternPoolForGenre(3, 1); // sustain/drone, Hard Rock
    REQUIRE(rockSustain.count == 3);
    REQUIRE(rockSustain.indices[0] == 26); // Rock Ballad

    // Metal keeps the original metal style pools.
    const auto metalPalm = PatternRules::stylePatternPoolForGenre(0, 3);
    REQUIRE(metalPalm.count == 3);
    REQUIRE(metalPalm.indices[0] == 7);   // half-time (metal)
    const auto metalOpen = PatternRules::stylePatternPoolForGenre(1, 3);
    REQUIRE(metalOpen.indices[0] == 4);   // chorus mid (metal)

    // Silence and empty styles behave like the base function.
    REQUIRE(PatternRules::stylePatternPoolForGenre(4, 0).count == 1);
    REQUIRE(PatternRules::stylePatternPoolForGenre(4, 0).indices[0] == 0);
    REQUIRE(PatternRules::stylePatternPoolForGenre(9, 0).count == 0);
}

TEST_CASE("PatternRules::diversifyPatternForStyle genre-aware steers rock to rock grooves", "[pattern_rules][G2]")
{
    // Rock (genre 0), palm-mute chugs, SOFT: pool {23,22,9} → SOFT filter {23,22}.
    FeatureVector soft = makeFD(StructureState::SOFT, 100.0f, 0.08f, 400.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(1, 0, 0, soft.state, 0) == 23);
    REQUIRE(PatternRules::diversifyPatternForStyle(1, 0, 1, soft.state, 0) == 22);
    // Same style, Metal (genre 3): keeps the metal half-time pool.
    REQUIRE(PatternRules::diversifyPatternForStyle(1, 0, 0, soft.state, 3) == 7);

    // Rock (genre 0), open-chord, LOUD: pool {22,4,14} → LOUD filter {4,14}.
    FeatureVector loud = makeFD(StructureState::LOUD, 120.0f, 0.2f, 600.0f);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 1, 0, loud.state, 0) == 4);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 1, 1, loud.state, 0) == 14);

    // Silence / unknown style still pass through to base.
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 4, 0, loud.state, 0) == 4);
    REQUIRE(PatternRules::diversifyPatternForStyle(4, 5, 0, loud.state, 0) == 4);
}

TEST_CASE("PatternRules::diversifyPatternForGenre re-homes mel indices 7-21 for rock genres", "[pattern_rules][G1]")
{
    // Rock (genre 0): a metal pick (8 = blast) is re-homed to rock vocabulary.
    FeatureVector soft = makeFD(StructureState::SOFT, 110.0f, 0.03f, 500.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(8, soft, 0, 0) == 22);  // Rock Backbeat
    REQUIRE(PatternRules::diversifyPatternForGenre(8, soft, 1, 0) == 23);  // Rock Half-Time

    // Rock, LOUD low-energy mid-tempo → shuffle / d-beat.
    FeatureVector mid = makeFD(StructureState::LOUD, 110.0f, 0.07f, 600.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(21, mid, 0, 0) == 24);  // Rock Shuffle
    REQUIRE(PatternRules::diversifyPatternForGenre(21, mid, 1, 0) == 25);  // Punk D-Beat

    // Rock, LOUD high-tempo → fast hard-rock/punk d-beat / shuffle.
    FeatureVector fast = makeFD(StructureState::LOUD, 175.0f, 0.2f, 900.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(8, fast, 0, 0) == 25);
    REQUIRE(PatternRules::diversifyPatternForGenre(8, fast, 1, 0) == 24);

    // Rock, LOUD mid-tempo high-energy → rock chorus (mid / open).
    FeatureVector chorus = makeFD(StructureState::LOUD, 120.0f, 0.2f, 600.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(8, chorus, 0, 0) == 4);
    REQUIRE(PatternRules::diversifyPatternForGenre(8, chorus, 1, 0) == 14);

    // SILENT always collapses to 0.
    REQUIRE(PatternRules::diversifyPatternForGenre(8, makeF(StructureState::SILENT, 120.0f), 0, 0) == 0);

    // Metal (genre 3) keeps the original metal routing (base >= 7 → itself).
    FeatureVector metalF = makeFD(StructureState::LOUD, 175.0f, 0.2f, 900.0f);
    REQUIRE(PatternRules::diversifyPatternForGenre(8, metalF, 0, 3)
            == PatternRules::diversifyPattern(8, metalF, 0));
}
