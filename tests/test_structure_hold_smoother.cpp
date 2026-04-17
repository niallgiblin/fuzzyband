#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureHoldSmoother.h"

TEST_CASE("StructureHoldSmoother holds VERSE before CHORUS until hold elapses", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();

    REQUIRE(h.advance(StructureState::VERSE, 0.05) == StructureState::VERSE);

    StructureState s = StructureState::VERSE;
    s = h.advance(StructureState::CHORUS, 0.05);
    REQUIRE(s == StructureState::VERSE);

    // First CHORUS attempt leaves time at 0.05; need 38 more × 0.05 to reach 1.95 (< 2.0 hold) before transitioning.
    for (int i = 0; i < 38; ++i)
        s = h.advance(StructureState::CHORUS, 0.05);

    REQUIRE(s == StructureState::VERSE);

    s = h.advance(StructureState::CHORUS, 0.05);
    REQUIRE(s == StructureState::CHORUS);
}

TEST_CASE("StructureHoldSmoother allows immediate exit from SILENT toward VERSE", "[structure]")
{
    StructureHoldSmoother h;
    h.reset();

    StructureState s = h.advance(StructureState::VERSE, 0.05);
    REQUIRE(s == StructureState::VERSE);
}
