#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureHoldSmoother.h"

// Hold times (mirrored from StructureHoldSmoother.h constants):
//   kHoldSilentSec    = 0.0  → immediate
//   kHoldVerseSec     = 2.0
//   kHoldChorusSec    = 2.5
//   kHoldBreakdownSec = 2.5
//
// Step size 0.05 s per advance() call.
// Calls to exceed 2.0 s: 40 × 0.05 = 2.00 s  (first blocked at 39, allowed at 40)
// Calls to exceed 2.5 s: 50 × 0.05 = 2.50 s  (first blocked at 49, allowed at 50)

namespace {

// Drive the smoother into the desired state from its initial SILENT state.
void driveToState(StructureHoldSmoother& h, StructureState target)
{
    // SILENT has 0.0 s hold — any desired state transitions immediately.
    const StructureState s = h.advance(target, 0.05);
    (void)s;
}

} // namespace

// ─── Transitions out of SILENT (hold = 0.0 s, always immediate) ──────────────

TEST_CASE("StructureHoldSmoother: SILENT to VERSE is immediate", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    REQUIRE(h.advance(StructureState::VERSE, 0.05) == StructureState::VERSE);
}

TEST_CASE("StructureHoldSmoother: SILENT to CHORUS is immediate", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    REQUIRE(h.advance(StructureState::CHORUS, 0.05) == StructureState::CHORUS);
}

TEST_CASE("StructureHoldSmoother: SILENT to BREAKDOWN is immediate", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    REQUIRE(h.advance(StructureState::BREAKDOWN, 0.05) == StructureState::BREAKDOWN);
}

// ─── Transitions out of VERSE (hold = 2.0 s) ─────────────────────────────────

TEST_CASE("StructureHoldSmoother: VERSE to CHORUS blocked until 2.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::VERSE);

    // 39 steps × 0.05 = 1.95 s < 2.0 s → blocked
    StructureState s = StructureState::VERSE;
    for (int i = 0; i < 39; ++i)
        s = h.advance(StructureState::CHORUS, 0.05);
    REQUIRE(s == StructureState::VERSE);

    // Step 40 → 2.00 s elapsed → allows transition
    s = h.advance(StructureState::CHORUS, 0.05);
    REQUIRE(s == StructureState::CHORUS);
}

TEST_CASE("StructureHoldSmoother: VERSE to BREAKDOWN blocked until 2.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::VERSE);

    StructureState s = StructureState::VERSE;
    for (int i = 0; i < 39; ++i)
        s = h.advance(StructureState::BREAKDOWN, 0.05);
    REQUIRE(s == StructureState::VERSE);

    s = h.advance(StructureState::BREAKDOWN, 0.05);
    REQUIRE(s == StructureState::BREAKDOWN);
}

TEST_CASE("StructureHoldSmoother: VERSE to SILENT blocked until 2.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::VERSE);

    StructureState s = StructureState::VERSE;
    for (int i = 0; i < 39; ++i)
        s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::VERSE);

    s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::SILENT);
}

// ─── Transitions out of CHORUS (hold = 2.5 s) ────────────────────────────────

TEST_CASE("StructureHoldSmoother: CHORUS to VERSE blocked until 2.5 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::CHORUS);

    StructureState s = StructureState::CHORUS;
    for (int i = 0; i < 49; ++i)
        s = h.advance(StructureState::VERSE, 0.05);
    REQUIRE(s == StructureState::CHORUS);

    s = h.advance(StructureState::VERSE, 0.05);
    REQUIRE(s == StructureState::VERSE);
}

TEST_CASE("StructureHoldSmoother: CHORUS to BREAKDOWN blocked until 2.5 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::CHORUS);

    StructureState s = StructureState::CHORUS;
    for (int i = 0; i < 49; ++i)
        s = h.advance(StructureState::BREAKDOWN, 0.05);
    REQUIRE(s == StructureState::CHORUS);

    s = h.advance(StructureState::BREAKDOWN, 0.05);
    REQUIRE(s == StructureState::BREAKDOWN);
}

TEST_CASE("StructureHoldSmoother: CHORUS to SILENT blocked until 2.5 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::CHORUS);

    StructureState s = StructureState::CHORUS;
    for (int i = 0; i < 49; ++i)
        s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::CHORUS);

    s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::SILENT);
}

// ─── Transitions out of BREAKDOWN (hold = 2.5 s) ─────────────────────────────

TEST_CASE("StructureHoldSmoother: BREAKDOWN to VERSE blocked until 2.5 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::BREAKDOWN);

    StructureState s = StructureState::BREAKDOWN;
    for (int i = 0; i < 49; ++i)
        s = h.advance(StructureState::VERSE, 0.05);
    REQUIRE(s == StructureState::BREAKDOWN);

    s = h.advance(StructureState::VERSE, 0.05);
    REQUIRE(s == StructureState::VERSE);
}

TEST_CASE("StructureHoldSmoother: BREAKDOWN to SILENT blocked until 2.5 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::BREAKDOWN);

    StructureState s = StructureState::BREAKDOWN;
    for (int i = 0; i < 49; ++i)
        s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::BREAKDOWN);

    s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::SILENT);
}

// ─── reset() ────────────────────────────────────────────────────────────────

TEST_CASE("StructureHoldSmoother: reset() restores SILENT state and clears elapsed time", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();

    // Advance deep into VERSE hold without completing it
    driveToState(h, StructureState::VERSE);
    for (int i = 0; i < 20; ++i)
        h.advance(StructureState::CHORUS, 0.05);
    REQUIRE(h.getCurrentState() == StructureState::VERSE);

    // Reset wipes state and accumulated time
    h.reset();
    REQUIRE(h.getCurrentState() == StructureState::SILENT);

    // Immediate transition from fresh SILENT is possible again
    REQUIRE(h.advance(StructureState::CHORUS, 0.05) == StructureState::CHORUS);
}

TEST_CASE("StructureHoldSmoother: reset() mid-CHORUS allows immediate re-entry", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::CHORUS);

    // Partially spend CHORUS hold
    for (int i = 0; i < 25; ++i)
        h.advance(StructureState::VERSE, 0.05);  // still blocked (1.25 s < 2.5 s)
    REQUIRE(h.getCurrentState() == StructureState::CHORUS);

    h.reset();
    REQUIRE(h.getCurrentState() == StructureState::SILENT);

    // From SILENT the time counter is 0 — any transition is instant
    REQUIRE(h.advance(StructureState::VERSE, 0.05) == StructureState::VERSE);
}
