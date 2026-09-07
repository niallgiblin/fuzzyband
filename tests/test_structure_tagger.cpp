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

    // Feed enough silent blocks to exceed the SOFT→SILENT hold (1.0s).
    // At 44.1kHz/512 blocks: 1.0 / (512/44100) ≈ 87 blocks
    for (int i = 0; i < 517; ++i)
        s = tagger.update(0.01f, 0.0f, 0.0f, 512);

    REQUIRE(s == StructureState::SILENT);
}

TEST_CASE("StructureTagger: ringing note stays non-silent below the noise floor", "[structure]")
{
    // Regression: a held/sustained note whose RMS decays below the silent floor
    // must NOT be classified SILENT (which drops the drums) while it is still
    // ringing. The noise floor must also not creep up to swallow the note.
    StructureTagger tagger;
    tagger.prepare(44100.0);

    StructureState s = tagger.update(0.035f, 0.0f, 0.0f, 512, 0.0f, true);
    REQUIRE(s == StructureState::SOFT);

    for (int i = 0; i < 517; ++i)
        s = tagger.update(0.01f, 0.0f, 0.0f, 512, 0.0f, true);
    REQUIRE(s == StructureState::SOFT);                 // noteRinging suppresses SILENT
    REQUIRE(tagger.getNoiseFloorRms() < 0.02f);         // floor did not creep up

    // The same low-RMS blocks WITHOUT the ringing flag now decay to SILENT.
    for (int i = 0; i < 517; ++i)
        s = tagger.update(0.01f, 0.0f, 0.0f, 512, 0.0f, false);
    REQUIRE(s == StructureState::SILENT);
}
