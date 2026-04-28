#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

TEST_CASE("StructureTagger hysteresis holds SOFT state", "[structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // Drive into SOFT: rms 0.2 is above kSilentRms(0.05) and below kLoudRms(0.35)
    StructureState s = tagger.update(0.2f, 0.0f, 0.0f, 512);
    REQUIRE(s == StructureState::SOFT);

    // Still SOFT after one silent block (hold hasn't expired)
    s = tagger.update(0.01f, 0.0f, 0.0f, 512);
    REQUIRE(s == StructureState::SOFT);

    // Feed 173 silent blocks to exceed kHoldSoftSec=2.0s
    for (int i = 0; i < 173; ++i)
        s = tagger.update(0.01f, 0.0f, 0.0f, 512);

    REQUIRE(s == StructureState::SILENT);
}
