#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────
//
// Feature values that drive each StructureState:
//
//   SILENT:    rms < kSilentRms(0.012)                          → rms = 0.005
//   AMBIENT:   rms >= 0.012 and rms < kAmbientCeil(0.025)       → rms = 0.018
//   SOFT:      rms >= 0.025 and rms < kLoudRms(0.075)           → rms = 0.040
//   LOUD:      rms >= 0.075 and rms < kBreakdownRms(0.12)       → rms = 0.090
//   BREAKDOWN: rms >= 0.12                                      → rms = 0.150
//
// Centroid is ignored for state classification (pure RMS model).
//
// Timing at 44100 Hz, 512-sample blocks:
//   blockSec = 512 / 44100 ≈ 0.011610 s
//   SILENT exit = immediate
//   SOFT→LOUD hold = 1.6s  → ≥ 138 blocks
//   SOFT→SILENT hold = 6.0s  → ≥ 517 blocks
//   LOUD→SOFT hold = 6.0s   → ≥ 517 blocks
//   LOUD→SILENT hold = 8.0s → ≥ 689 blocks
//   AMBIENT→SILENT hold = 8.0s → ≥ 689 blocks
//   BREAKDOWN→LOUD hold = 8.0s → ≥ 689 blocks
//   LOUD→BREAKDOWN hold = 0.4s → ≥ 35 blocks

namespace {
constexpr double kSr      = 44100.0;
constexpr int    kBlock   = 512;
constexpr float  kRmsAmbient = 0.018f;  // between kSilentRms(0.012) and kAmbientCeil(0.025)
constexpr float  kRmsSoft    = 0.040f;  // between kAmbientCeil(0.025) and kLoudRms(0.075)
constexpr float  kRmsLoud    = 0.090f;  // between kLoudRms(0.075) and kBreakdownRms(0.12)
constexpr float  kRmsBreakdown = 0.150f; // ≥ kBreakdownRms(0.12)
constexpr float  kRmsSilent  = 0.005f;
} // namespace

// ─── SILENT exits ────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: SILENT to SOFT is immediate (kHoldSilentSec == 0)", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // One block with SOFT-like features — no hold required from SILENT
    const StructureState s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: SILENT to LOUD is immediate", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    const StructureState s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);
}

// ─── SOFT exits ──────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: SOFT to LOUD is blocked before hold, then permitted", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);  // enter SOFT (immediate)

    // 137 LOUD blocks ≈ 1.590 s < 1.6 s → must stay in SOFT
    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 137; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    // One more block → total exceeds hold → LOUD
    s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureTagger: SOFT to SILENT is blocked during short DI phrase gaps", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);  // enter SOFT

    // 50 silent blocks ≈ 0.58 s < 6.0 s → must stay in SOFT
    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 50; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

// ─── LOUD exits ──────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: LOUD to SOFT after 6.0 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD with minimal accumulated LOUD time.
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 138; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // Feed SOFT while in LOUD — 6.0 s hold required
    // 516 SOFT blocks ≈ 5.991 s < 6.0 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 516; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // One more block → total ≈ 517 * 0.01161 = 6.002 s > 6.0 s → SOFT
    s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: LOUD to SILENT after 8.0 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD: SILENT→SOFT (1 block) then enough LOUD blocks to exceed the SOFT→LOUD hold
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 138; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // 689 silent blocks ≈ 7.988 s < 8.00 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 689; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // 690th block → cumulative ≈ 8.012 s → SILENT
    s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SILENT);
}

TEST_CASE("StructureTagger: clean DI loud strum reaches LOUD quickly", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);

    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 138; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock, 0.12f);

    REQUIRE(s == StructureState::LOUD);
}

// ─── 5-state transition tests (S02) ───────────────────────────────────────────

TEST_CASE("StructureTagger: SILENT to AMBIENT is immediate (kHoldSilentSec == 0)", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    const StructureState s = t.update(kRmsAmbient, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::AMBIENT);
}

TEST_CASE("StructureTagger: AMBIENT to SILENT is held for 8.0s", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsAmbient, 0.0f, 0.0f, kBlock);  // enter AMBIENT (immediate)

    // 689 silent blocks ≈ 7.998 s < 8.0 s → still AMBIENT
    StructureState s = StructureState::AMBIENT;
    for (int i = 0; i < 689; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::AMBIENT);

    // 690th block → cumulative ≈ 8.012 s → SILENT
    s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SILENT);
}

TEST_CASE("StructureTagger: AMBIENT to SOFT is held for 6.0s", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsAmbient, 0.0f, 0.0f, kBlock);  // enter AMBIENT

    // 516 SOFT blocks ≈ 5.991 s < 6.0 s → still AMBIENT
    StructureState s = StructureState::AMBIENT;
    for (int i = 0; i < 516; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::AMBIENT);

    // 517th block → cumulative ≈ 6.002 s → SOFT
    s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: SOFT to AMBIENT is held for 6.0s", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);  // enter SOFT (immediate from SILENT)

    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 516; ++i)
        s = t.update(kRmsAmbient, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    s = t.update(kRmsAmbient, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::AMBIENT);
}

TEST_CASE("StructureTagger: BREAKDOWN entry from LOUD after 0.4s", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Enter LOUD first: SILENT→SOFT (immediate), then enough LOUD blocks
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 138; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // 34 BREAKDOWN blocks ≈ 0.395 s < 0.4 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 34; ++i)
        s = t.update(kRmsBreakdown, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // 35th block → cumulative ≈ 0.406 s → BREAKDOWN
    s = t.update(kRmsBreakdown, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::BREAKDOWN);
}

TEST_CASE("StructureTagger: BREAKDOWN to LOUD after 8.0s", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Enter BREAKDOWN via LOUD
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 138; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 35; ++i)
        t.update(kRmsBreakdown, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::BREAKDOWN);

    // 689 LOUD blocks ≈ 7.999 s < 8.0 s → still BREAKDOWN
    StructureState s = StructureState::BREAKDOWN;
    for (int i = 0; i < 689; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::BREAKDOWN);

    // 690th block → LOUD
    s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureTagger: BREAKDOWN to SILENT after 8.0s", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Enter BREAKDOWN
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 138; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 35; ++i)
        t.update(kRmsBreakdown, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::BREAKDOWN);

    StructureState s = StructureState::BREAKDOWN;
    for (int i = 0; i < 689; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::BREAKDOWN);

    s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SILENT);
}

TEST_CASE("StructureTagger: Full 5-state ramp SILENT→AMBIENT→SOFT→LOUD→BREAKDOWN", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // SILENT → AMBIENT (immediate)
    StructureState s = t.update(kRmsAmbient, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::AMBIENT);

    // AMBIENT → SOFT (after 6.0s hold): 517 blocks
    for (int i = 0; i < 517; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    // SOFT → LOUD (after 1.6s hold): 138 blocks
    for (int i = 0; i < 138; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // LOUD → BREAKDOWN (after 0.4s hold): 35 blocks
    for (int i = 0; i < 35; ++i)
        s = t.update(kRmsBreakdown, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::BREAKDOWN);
}

TEST_CASE("StructureTagger: SILENT to BREAKDOWN direct is immediate", "[structure]")
{
    // Since SILENT hold is 0.0s, direct jump to BREAKDOWN is allowed
    StructureTagger t;
    t.prepare(kSr);

    const StructureState s = t.update(kRmsBreakdown, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::BREAKDOWN);
}
