#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────
//
// Feature values that drive each StructureState:
//
//   SILENT:  rms < kSilentRms(0.012)   →  rms = 0.005
//   SOFT:    rms >= 0.012 and rms < clean-DI loud threshold   →  rms = 0.035
//   LOUD:    rms >= clean-DI loud threshold   →  rms = 0.08
//
// Centroid is ignored for state classification (pure RMS model).
//
// Timing at 44100 Hz, 512-sample blocks:
//   blockSec = 512 / 44100 ≈ 0.011610 s
//   SOFT→LOUD hold = 0.20 s  →  ≥ 18 blocks to exceed
//   SOFT→SILENT hold = 1.50 s  →  ≥ 130 blocks to exceed
//   LOUD→SOFT hold = 1.50 s   →  ≥ 130 blocks to exceed
//   LOUD→SILENT hold = 2.00 s   →  ≥ 173 blocks to exceed

namespace {
constexpr double kSr      = 44100.0;
constexpr int    kBlock   = 512;
constexpr float  kRmsSoft  = 0.035f;
constexpr float  kRmsLoud  = 0.08f;
constexpr float  kRmsSilent = 0.005f;
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

TEST_CASE("StructureTagger: SOFT to LOUD is blocked before clean-DI hold, then permitted", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);  // enter SOFT (immediate)

    // 17 LOUD blocks ≈ 0.197 s < 0.20 s → must stay in SOFT
    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 17; ++i)
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

    // 50 silent blocks ≈ 0.58 s < 1.50 s → must stay in SOFT
    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 50; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

// ─── LOUD exits ──────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: LOUD to SOFT after 1.5 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD with minimal accumulated LOUD time:
    // SILENT→SOFT (1 block) then enough LOUD blocks to exceed the SOFT→LOUD hold.
    // After transition, timeInState_LOUD = 1 block (≈ 0.0116 s).
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 18; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // Feed SOFT while in LOUD — 1.5 s hold required
    // 129 SOFT blocks ≈ 1.497 s < 1.5 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 129; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // One more block → total ≈ 130 * 0.01161 = 1.509 s > 1.5 s → SOFT
    s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: LOUD to SILENT after 2.5 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD: SILENT→SOFT (1 block) then enough LOUD blocks to exceed the SOFT→LOUD hold
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 18; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // 172 silent blocks ≈ 1.997 s < 2.00 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 172; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SILENT);
}

TEST_CASE("StructureTagger: clean DI loud strum reaches LOUD quickly", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);

    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 18; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock, 0.09f);

    REQUIRE(s == StructureState::LOUD);
}
