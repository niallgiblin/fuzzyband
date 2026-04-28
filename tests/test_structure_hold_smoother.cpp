#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureHoldSmoother.h"

TEST_CASE("StructureHoldSmoother holds SOFT before LOUD until hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();

    REQUIRE(h.advance(StructureState::SOFT, 0.05) == StructureState::SOFT);

    StructureState s = StructureState::SOFT;
    s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::SOFT);

    // First LOUD attempt leaves time at 0.05; need 38 more × 0.05 to reach 1.95 (< 2.0 hold) before transitioning.
    for (int i = 0; i < 38; ++i)
        s = h.advance(StructureState::LOUD, 0.05);

    REQUIRE(s == StructureState::SOFT);

    s = h.advance(StructureState::LOUD, 0.05);
    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureHoldSmoother allows immediate exit from SILENT toward SOFT", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();

    StructureState s = h.advance(StructureState::SOFT, 0.05);
    REQUIRE(s == StructureState::SOFT);
}
