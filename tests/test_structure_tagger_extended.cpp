#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────
//
// Feature values that drive each StructureState:
//   SILENT: rms < kSilentRms(0.012)                      → rms = 0.005
//   SOFT:   rms >= 0.012 and rms < kLoudRms(0.075)       → rms = 0.040
//   LOUD:   rms >= 0.075                                 → rms = 0.090
//
// Timings at 44100 Hz, 512-sample blocks:
//   blockSec = 512 / 44100 ≈ 0.011610 s
//   SILENT exit = immediate (kHoldSilentSec = 0.0)
//   SOFT→LOUD hold = 0.4s  → ≥ 35 blocks
//   SOFT→SILENT hold = 2.0s → ≥ 173 blocks
//   LOUD→SOFT hold = 2.0s   → ≥ 173 blocks
//   LOUD→SILENT hold = 3.0s → ≥ 259 blocks

namespace {
constexpr double kSr      = 44100.0;
constexpr int    kBlock   = 512;
constexpr float  kRmsSoft    = 0.040f;
constexpr float  kRmsLoud    = 0.090f;
constexpr float  kRmsSilent  = 0.005f;
} // namespace

// ─── SILENT exits ────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: SILENT to SOFT is immediate (kHoldSilentSec == 0)", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

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

    // 34 LOUD blocks ≈ 0.395 s < 0.4 s → must stay in SOFT
    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 34; ++i)
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

    // 50 silent blocks ≈ 0.58 s < 2.0 s → must stay in SOFT
    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 50; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

// ─── LOUD exits ──────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: LOUD to SOFT after 2.0 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD with minimal accumulated LOUD time.
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 35; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // Feed SOFT while in LOUD — 2.0 s hold required
    // 172 SOFT blocks ≈ 1.997 s < 2.0 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 172; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // One more block → total ≈ 173 * 0.01161 = 2.009 s > 2.0 s → SOFT
    s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: LOUD to SILENT after 3.0 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD: SILENT→SOFT (1 block) then enough LOUD blocks to exceed the SOFT→LOUD hold
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 35; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // 258 silent blocks ≈ 2.996 s < 3.0 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 258; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // 259th block → cumulative ≈ 3.008 s → SILENT
    s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SILENT);
}

TEST_CASE("StructureTagger: clean DI loud strum reaches LOUD quickly", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);

    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 35; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock, 0.12f);

    REQUIRE(s == StructureState::LOUD);
}

// ─── 3-state transition tests ───────────────────────────────────────────────

TEST_CASE("StructureTagger: Full 3-state ramp SILENT→SOFT→LOUD", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // SILENT → SOFT (immediate)
    StructureState s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    // SOFT → LOUD (after 0.4s hold): 35 blocks
    for (int i = 0; i < 35; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureTagger: LOUD→SOFT→SILENT ramp down", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Enter LOUD
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 35; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // LOUD → SOFT after 2.0s: 173 blocks
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 173; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    // SOFT → SILENT after 2.0s: 173 blocks
    for (int i = 0; i < 173; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SILENT);
}

TEST_CASE("StructureTagger: revert on desired change mid-hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Enter SOFT
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);

    // Start transitioning to LOUD
    for (int i = 0; i < 20; ++i)  // 20 * 0.01161 ≈ 0.232 s < 0.4 s
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::SOFT);

    // Before hold completes, revert to SOFT — timer resets
    StructureState s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    // Now try LOUD again — full hold required
    for (int i = 0; i < 34; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureTagger: sub-bass ratio discriminates SOFT/LOUD", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // RMS in the overlap zone (0.055) but high sub-bass → should bias LOUD
    t.setSubBassRatio(0.40f);  // > kSubBassLoudFloor (0.35)
    StructureState s = t.update(0.055f, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // Reset and try with low sub-bass → should bias SOFT
    t.prepare(kSr);
    t.setSubBassRatio(0.15f);  // < kSubBassSoftCeil (0.20)
    s = t.update(0.055f, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}
