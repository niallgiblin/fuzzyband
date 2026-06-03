#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"

TEST_CASE("StructureTagger hysteresis holds SOFT state", "[structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // Drive into SOFT: clean DI rms 0.035 is above silence and below the loud threshold.
    StructureState s = tagger.update(0.035f, 0.0f, 0.0f, 512);
    REQUIRE(s == StructureState::SOFT);

    // Still SOFT after one silent block (hold hasn't expired)
    s = tagger.update(0.01f, 0.0f, 0.0f, 512);
    REQUIRE(s == StructureState::SOFT);

    // Feed enough silent blocks to exceed the SOFT→SILENT hold (6.0s).
    // At 44.1kHz/512 blocks: 6.0 / (512/44100) ≈ 517 blocks
    for (int i = 0; i < 517; ++i)
        s = tagger.update(0.01f, 0.0f, 0.0f, 512);

    REQUIRE(s == StructureState::SILENT);
}
