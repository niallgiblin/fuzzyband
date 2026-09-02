#include <catch2/catch_test_macros.hpp>
#include <string>

#include "analysis/StructureSequencer.h"

TEST_CASE("StructureSequencer: serialize/parse round-trips a custom form", "[structure][sequencer]")
{
    SongForm form;
    form.name = "Custom";
    form.sections = {
        { "INTRO", 4 }, { "VERSE", 8 }, { "CHORUS", 8 }, { "BREAKDOWN", 4 },
        { "SOLO", 8 }, { "OUTRO", 4 },
    };

    const std::string serialized = StructureSequencer::serializeForm(form);
    REQUIRE(serialized == "INTRO:4,VERSE:8,CHORUS:8,BREAKDOWN:4,SOLO:8,OUTRO:4");

    const SongForm parsed = StructureSequencer::parseFormString(serialized);
    REQUIRE(parsed.sections.size() == form.sections.size());
    for (size_t i = 0; i < form.sections.size(); ++i)
    {
        REQUIRE(parsed.sections[i].name == form.sections[i].name);
        REQUIRE(parsed.sections[i].bars == form.sections[i].bars);
    }
}

TEST_CASE("StructureSequencer: parse clamps bars and skips unknown sections", "[structure][sequencer]")
{
    const SongForm parsed = StructureSequencer::parseFormString(
        "VERSE:0,CHORUS:999,BOGUS:8,BRIDGE:4,SOLO:4");

    // VERSE clamped to 1, CHORUS clamped to 64, BOGUS/BRIDGE skipped.
    REQUIRE(parsed.sections.size() == 3);
    REQUIRE(parsed.sections[0].name == "VERSE");
    REQUIRE(parsed.sections[0].bars == 1);
    REQUIRE(parsed.sections[1].name == "CHORUS");
    REQUIRE(parsed.sections[1].bars == 64);
    REQUIRE(parsed.sections[2].name == "SOLO");
    REQUIRE(parsed.sections[2].bars == 4);
}

TEST_CASE("StructureSequencer: parse of an empty/invalid string falls back to preset 0", "[structure][sequencer]")
{
    const SongForm parsed = StructureSequencer::parseFormString(",,,garbage,,");
    REQUIRE_FALSE(parsed.sections.empty());
    REQUIRE(parsed.sections[0].name == "INTRO");  // Standard Metal preset starts with INTRO
}

TEST_CASE("StructureSequencer: loadForm resets to the first section", "[structure][sequencer]")
{
    StructureSequencer seq;
    SongForm form;
    form.sections = { { "VERSE", 4 }, { "CHORUS", 4 } };
    seq.loadForm(form);

    REQUIRE(seq.getCurrentSectionIndex() == 0);
    REQUIRE(std::string(seq.getCurrentSectionName()) == "VERSE");
    REQUIRE(seq.getBarsInSection() == 4);
    REQUIRE_FALSE(seq.isComplete());
}

TEST_CASE("StructureSequencer: non-looping form completes and stops advancing", "[structure][sequencer]")
{
    StructureSequencer seq;
    seq.setLooping(false);
    SongForm form;
    form.sections = { { "INTRO", 1 }, { "OUTRO", 1 } };
    seq.loadForm(form);

    // 1 bar at 120 BPM / 48 kHz = 96000 samples.
    seq.advance(96000, 120.0f, 48000.0);
    REQUIRE(std::string(seq.getCurrentSectionName()) == "OUTRO");
    REQUIRE_FALSE(seq.isComplete());

    seq.advance(96000, 120.0f, 48000.0);
    REQUIRE(seq.isComplete());
    const int barsAtComplete = seq.getGlobalBarCount();

    seq.advance(96000, 120.0f, 48000.0);
    REQUIRE(seq.isComplete());
    REQUIRE(seq.getGlobalBarCount() == barsAtComplete);
}
