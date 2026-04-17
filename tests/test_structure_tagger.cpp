#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

TEST_CASE("StructureTagger hysteresis holds state", "[structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    StructureState s = tagger.update(0.2f, 900.0f, 0.0f, 512);
    REQUIRE(s == StructureState::BREAKDOWN);

    s = tagger.update(0.01f, 900.0f, 0.0f, 512);
    REQUIRE(s == StructureState::BREAKDOWN);

    // kHoldBreakdownSec is 2.5s; need enough 512-sample blocks at 44.1 kHz to exceed hold.
    for (int i = 0; i < 260; ++i)
        s = tagger.update(0.01f, 1500.0f, 0.0f, 512);

    REQUIRE(s == StructureState::SILENT);
}
