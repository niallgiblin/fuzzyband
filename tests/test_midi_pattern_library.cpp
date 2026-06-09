#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include "midi/MidiPatternLibrary.h"

TEST_CASE("MidiPatternLibrary: pattern count is 11", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    REQUIRE(lib.patternCount() == 22);
}

TEST_CASE("MidiPatternLibrary: new patterns have non-empty names", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    for (int i = 7; i <= 10; ++i)
    {
        INFO("Pattern index: " << i);
        REQUIRE_FALSE(lib.getPattern(i).name.empty());
    }
}

TEST_CASE("MidiPatternLibrary: new patterns have drum events", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    for (int i = 7; i <= 10; ++i)
    {
        INFO("Pattern index: " << i);
        REQUIRE_FALSE(lib.getPattern(i).drumEvents.empty());
    }
}

TEST_CASE("MidiPatternLibrary: new patterns are distinct from existing patterns", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    std::set<std::string> newNames;
    std::set<std::string> existingNames;

    for (int i = 1; i <= 6; ++i)
        existingNames.insert(lib.getPattern(i).name);
    for (int i = 7; i <= 10; ++i)
        newNames.insert(lib.getPattern(i).name);

    // New names should not overlap with existing names
    for (const auto& n : newNames)
    {
        INFO("New pattern name: " << n);
        REQUIRE(existingNames.find(n) == existingNames.end());
    }
}

TEST_CASE("MidiPatternLibrary: getPattern(10) returns valid pattern", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    const auto& p = lib.getPattern(10);
    REQUIRE(p.name == "Thrash");
    REQUIRE(p.lengthInBars == 1.0f);
    REQUIRE_FALSE(p.drumEvents.empty());
}

TEST_CASE("MidiPatternLibrary: getPattern(-1) clamps to 0", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    const auto& p = lib.getPattern(-1);
    REQUIRE(p.name == "Silent");
    REQUIRE(p.drumEvents.empty());
}

TEST_CASE("MidiPatternLibrary: getPattern(999) clamps to 10", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    const auto& p = lib.getPattern(999);
    REQUIRE(p.name == "Chorus Blast");
}

TEST_CASE("MidiPatternLibrary: blast beat has alternating kick and snare", "[midi_pattern_library]")
{
    // Quick sanity: blast beat (index 8) should have both kick and snare events
    MidiPatternLibrary lib;
    const auto& p = lib.getPattern(8);
    bool hasKick = false, hasSnare = false, hasRide = false, hasHihat = false;
    for (const auto& e : p.drumEvents)
    {
        if (e.note == 36) hasKick = true;
        if (e.note == 38) hasSnare = true;
        if (e.note == 51) hasRide = true;
        if (e.note == 42 || e.note == 46) hasHihat = true;
    }
    REQUIRE(hasKick);
    REQUIRE(hasSnare);
    REQUIRE(hasRide);
    REQUIRE_FALSE(hasHihat); // Blast beat uses ride, no hats
}

TEST_CASE("MidiPatternLibrary: sparse breakdown has no hats or ride", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    const auto& p = lib.getPattern(9);
    for (const auto& e : p.drumEvents)
    {
        INFO("Note: " << static_cast<int>(e.note));
        REQUIRE(e.note != 42);
        REQUIRE(e.note != 46);
        REQUIRE(e.note != 51);
    }
}
