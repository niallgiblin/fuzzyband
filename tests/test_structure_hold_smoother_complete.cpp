#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureHoldSmoother.h"

// Hold times (mirrored from StructureHoldSmoother.h constants):
//   kHoldSilentSec    = 0.0
//   kHoldAmbientSec   = 6.0
//   kHoldSoftSec      = 6.0
//   kHoldLoudSec      = 6.0
//   kHoldBreakdownSec = 8.0
//
// Step size 0.05 s per advance() call.
// Calls to exceed 6.0 s: 120 × 0.05 = 6.00 s  (first blocked at 119, allowed at 120)
// Calls to exceed 8.0 s: 160 × 0.05 = 8.00 s  (first blocked at 159, allowed at 160)

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

TEST_CASE("StructureHoldSmoother: SILENT to SOFT is immediate", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    REQUIRE(h.advance(StructureState::SOFT, 0.05) == StructureState::SOFT);
}

TEST_CASE("StructureHoldSmoother: SILENT to LOUD is immediate", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    REQUIRE(h.advance(StructureState::LOUD, 0.05) == StructureState::LOUD);
}

TEST_CASE("StructureHoldSmoother: SILENT to BREAKDOWN is immediate", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    REQUIRE(h.advance(StructureState::BREAKDOWN, 0.05) == StructureState::BREAKDOWN);
}

// ─── Transitions out of AMBIENT (hold = 6.0 s) ───────────────────────────────

TEST_CASE("StructureHoldSmoother: AMBIENT to SOFT blocked until 6.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::AMBIENT);

    // 119 steps × 0.05 = 5.95 s < 6.0 s → blocked
    StructureState s = StructureState::AMBIENT;
    for (int i = 0; i < 119; ++i)
        s = h.advance(StructureState::SOFT, 0.05);
    REQUIRE(s == StructureState::AMBIENT);

    // Step 120 → 6.00 s elapsed → allows transition
    s = h.advance(StructureState::SOFT, 0.05);
    REQUIRE(s == StructureState::SOFT);
}

// ─── Transitions out of SOFT (hold = 6.0 s) ──────────────────────────────────

TEST_CASE("StructureHoldSmoother: SOFT to LOUD blocked until 6.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::SOFT);

    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 119; ++i)
        s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::SOFT);

    s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureHoldSmoother: SOFT to SILENT blocked until 6.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::SOFT);

    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 119; ++i)
        s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::SOFT);

    s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::SILENT);
}

// ─── Transitions out of LOUD (hold = 6.0 s) ──────────────────────────────────

TEST_CASE("StructureHoldSmoother: LOUD to SOFT blocked until 6.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::LOUD);

    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 119; ++i)
        s = h.advance(StructureState::SOFT, 0.05);
    REQUIRE(s == StructureState::LOUD);

    s = h.advance(StructureState::SOFT, 0.05);
    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureHoldSmoother: LOUD to SILENT blocked until 6.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::LOUD);

    StructureState s = StructureState::LOUD;
    for (int i = 0; i < 119; ++i)
        s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::LOUD);

    s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::SILENT);
}

// ─── Transitions out of BREAKDOWN (hold = 8.0 s) ─────────────────────────────

TEST_CASE("StructureHoldSmoother: BREAKDOWN to LOUD blocked until 8.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::BREAKDOWN);

    // 159 steps × 0.05 = 7.95 s < 8.0 s → blocked
    StructureState s = StructureState::BREAKDOWN;
    for (int i = 0; i < 159; ++i)
        s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::BREAKDOWN);

    // Step 160 → 8.00 s → allows transition
    s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureHoldSmoother: BREAKDOWN to SILENT blocked until 8.0 s hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::BREAKDOWN);

    StructureState s = StructureState::BREAKDOWN;
    for (int i = 0; i < 159; ++i)
        s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::BREAKDOWN);

    s = h.advance(StructureState::SILENT, 0.05);
    REQUIRE(s == StructureState::SILENT);
}

// ─── Additional SOFT to LOUD transition test ─────────────────────────────────

TEST_CASE("StructureHoldSmoother: SOFT to LOUD after 6.0 s hold", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::SOFT);

    StructureState s = StructureState::SOFT;
    for (int i = 0; i < 119; ++i)
        s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::SOFT);

    s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::LOUD);
}

// ─── reset() ────────────────────────────────────────────────────────────────

TEST_CASE("StructureHoldSmoother: reset() restores SILENT state and clears elapsed time", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();

    // Advance deep into SOFT hold without completing it
    driveToState(h, StructureState::SOFT);
    for (int i = 0; i < 60; ++i)
        h.advance(StructureState::LOUD, 0.05);
    REQUIRE(h.getCurrentState() == StructureState::SOFT);

    // Reset wipes state and accumulated time
    h.reset();
    REQUIRE(h.getCurrentState() == StructureState::SILENT);

    // Immediate transition from fresh SILENT is possible again
    REQUIRE(h.advance(StructureState::LOUD, 0.05) == StructureState::LOUD);
}

TEST_CASE("StructureHoldSmoother: reset() mid-LOUD allows immediate re-entry", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();
    driveToState(h, StructureState::LOUD);

    // Partially spend LOUD hold
    for (int i = 0; i < 60; ++i)
        h.advance(StructureState::SOFT, 0.05);  // still blocked (3.0 s < 6.0 s)
    REQUIRE(h.getCurrentState() == StructureState::LOUD);

    h.reset();
    REQUIRE(h.getCurrentState() == StructureState::SILENT);

    // From SILENT the time counter is 0 — any transition is instant
    REQUIRE(h.advance(StructureState::SOFT, 0.05) == StructureState::SOFT);
}
