#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────
//
// Feature values that drive each StructureState:
//
//   SILENT:    rms < max(0.05, peakRms * 0.15)   →  rms = 0.01
//   BREAKDOWN: rms ≥ silent floor, centroid < 1000 Hz
//   VERSE:     rms ≥ silent floor, 1000 ≤ centroid < 2200 Hz
//   CHORUS:    rms ≥ silent floor, centroid ≥ 2200 Hz
//
// Timing at 44100 Hz, 512-sample blocks:
//   blockSec = 512 / 44100 ≈ 0.011610 s
//   kHoldVerseSec    = 2.0 s  →  ≥ 173 blocks to exceed
//   kHoldChorusSec   = 2.5 s  →  ≥ 216 blocks to exceed
//   kHoldBreakdownSec = 2.5 s →  ≥ 216 blocks to exceed

namespace {
constexpr double kSr    = 44100.0;
constexpr int    kBlock = 512;
constexpr float  kRmsActive    = 0.5f;
constexpr float  kCentBreakdown = 800.0f;
constexpr float  kCentVerse    = 1500.0f;
constexpr float  kCentChorus   = 3000.0f;
constexpr float  kRmsSilent    = 0.01f;
} // namespace

// ─── SILENT exits ────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: SILENT to VERSE is immediate (kHoldSilentSec == 0)", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // One block with VERSE-like features — no hold required from SILENT
    const StructureState s = t.update(kRmsActive, kCentVerse, 0.0f, kBlock);
    REQUIRE(s == StructureState::VERSE);
}

TEST_CASE("StructureTagger: SILENT to BREAKDOWN is immediate", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    const StructureState s = t.update(kRmsActive, kCentBreakdown, 0.0f, kBlock);
    REQUIRE(s == StructureState::BREAKDOWN);
}

TEST_CASE("StructureTagger: SILENT to CHORUS is immediate", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    const StructureState s = t.update(kRmsActive, kCentChorus, 0.0f, kBlock);
    REQUIRE(s == StructureState::CHORUS);
}

// ─── VERSE exits ─────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: VERSE to CHORUS is blocked before 2.0 s hold, then permitted", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsActive, kCentVerse, 0.0f, kBlock);  // enter VERSE (immediate)

    // 100 CHORUS blocks ≈ 1.161 s < 2.0 s → must stay in VERSE
    StructureState s = StructureState::VERSE;
    for (int i = 0; i < 100; ++i)
        s = t.update(kRmsActive, kCentChorus, 0.0f, kBlock);
    REQUIRE(s == StructureState::VERSE);

    // Feed until 2.0 s hold is exceeded (≥ 173 blocks total)
    for (int i = 100; i < 200; ++i)
        s = t.update(kRmsActive, kCentChorus, 0.0f, kBlock);
    REQUIRE(s == StructureState::CHORUS);
}

TEST_CASE("StructureTagger: VERSE to SILENT is blocked before 2.0 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);
    t.update(kRmsActive, kCentVerse, 0.0f, kBlock);  // enter VERSE

    // 50 silent blocks ≈ 0.58 s < 2.0 s → must stay in VERSE
    StructureState s = StructureState::VERSE;
    for (int i = 0; i < 50; ++i)
        s = t.update(kRmsSilent, kCentVerse, 0.0f, kBlock);
    REQUIRE(s == StructureState::VERSE);
}

// ─── CHORUS exits ────────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: CHORUS to BREAKDOWN after 2.5 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach CHORUS: SILENT→VERSE (1 block) then 200 CHORUS blocks (> 2.0 s VERSE hold)
    t.update(kRmsActive, kCentVerse, 0.0f, kBlock);
    for (int i = 0; i < 200; ++i)
        t.update(kRmsActive, kCentChorus, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::CHORUS);

    // Feed BREAKDOWN while in CHORUS — 2.5 s hold required
    // 200 BREAKDOWN blocks ≈ 2.322 s < 2.5 s → still CHORUS
    StructureState s = StructureState::CHORUS;
    for (int i = 0; i < 200; ++i)
        s = t.update(kRmsActive, kCentBreakdown, 0.0f, kBlock);
    REQUIRE(s == StructureState::CHORUS);

    // 20 more blocks → total ≈ 220 * 0.01160 = 2.55 s > 2.5 s → BREAKDOWN
    for (int i = 0; i < 20; ++i)
        s = t.update(kRmsActive, kCentBreakdown, 0.0f, kBlock);
    REQUIRE(s == StructureState::BREAKDOWN);
}

// ─── BREAKDOWN exits ─────────────────────────────────────────────────────────

TEST_CASE("StructureTagger: BREAKDOWN to VERSE after 2.5 s hold", "[structure]")
{
    StructureTagger t;
    t.prepare(kSr);

    // Reach BREAKDOWN directly from SILENT (immediate)
    t.update(kRmsActive, kCentBreakdown, 0.0f, kBlock);
    REQUIRE(t.getCurrentState() == StructureState::BREAKDOWN);

    // 220 VERSE blocks ≈ 2.554 s > 2.5 s → should transition to VERSE
    StructureState s = StructureState::BREAKDOWN;
    for (int i = 0; i < 220; ++i)
        s = t.update(kRmsActive, kCentVerse, 0.0f, kBlock);
    REQUIRE(s == StructureState::VERSE);
}
