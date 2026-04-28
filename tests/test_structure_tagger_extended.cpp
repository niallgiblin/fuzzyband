#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────
//
// Feature values that drive each StructureState:
//
//   SILENT:  rms < kSilentRms(0.05)   →  rms = 0.01
//   SOFT:    rms >= 0.05 and rms < kLoudRms(0.35)   →  rms = 0.2
//   LOUD:    rms >= kLoudRms(0.35)   →  rms = 0.5
//
// Centroid is ignored for state classification (pure RMS model).
//
// Timing at 44100 Hz, 512-sample blocks:
//   blockSec = 512 / 44100 ≈ 0.011610 s
//   kHoldSoftSec = 2.0 s  →  ≥ 173 blocks to exceed
//   kHoldLoudSec = 2.5 s  →  ≥ 216 blocks to exceed

namespace {
constexpr double kSr      = 44100.0;
constexpr int    kBlock   = 512;
constexpr float  kRmsSoft  = 0.2f;   // above kSilentRms, below kLoudRms
constexpr float  kRmsLoud  = 0.5f;   // above kLoudRms(0.35)
constexpr float  kRmsSilent = 0.01f;
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

TEST_CASE("StructureTagger: SOFT to LOUD is blocked before 2.0 s hold, then permitted", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);  // enter SOFT (immediate)

    // 172 LOUD blocks ≈ 1.997 s < 2.0 s → must stay in SOFT
    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 172; ++i)
        s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);

    // One more block → total exceeds 2.0 s hold → LOUD
    s = t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureTagger: SOFT to SILENT is blocked before 2.0 s hold", "[structure]")
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

TEST_CASE("StructureTagger: LOUD to SOFT after 2.5 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD: SILENT→SOFT (1 block) then 200 LOUD blocks (> 2.0 s SOFT hold)
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 200; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // Feed SOFT while in LOUD — 2.5 s hold required
    // 200 SOFT blocks ≈ 2.322 s < 2.5 s → still LOUD
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 200; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::LOUD);

    // 20 more blocks → total ≈ 220 * 0.01160 = 2.55 s > 2.5 s → SOFT
    for (int i = 0; i < 20; ++i)
        s = t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: LOUD to SILENT after 2.5 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach LOUD: SILENT→SOFT (1 block) then 200 LOUD blocks (> 2.0 s SOFT hold)
    t.update(kRmsSoft, 0.0f, 0.0f, kBlock);
    for (int i = 0; i < 200; ++i)
        t.update(kRmsLoud, 0.0f, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::LOUD);

    // 220 silent blocks ≈ 2.554 s > 2.5 s → should transition to SILENT
    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 220; ++i)
        s = t.update(kRmsSilent, 0.0f, 0.0f, kBlock);
    REQUIRE(s == StructureState::SILENT);
}
