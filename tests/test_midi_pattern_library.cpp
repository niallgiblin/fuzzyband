#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include "midi/MidiPatternLibrary.h"

TEST_CASE("MidiPatternLibrary: pattern count matches the shared constant", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    REQUIRE(lib.patternCount() == MidiPatternLibrary::kPatternCount);
    REQUIRE(lib.patternCount() == 28);  // 22 metal + 6 rock-first (A4.1)
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

TEST_CASE("MidiPatternLibrary: getPattern(999) clamps to the last pattern", "[midi_pattern_library]")
{
    MidiPatternLibrary lib;
    const auto& p = lib.getPattern(999);
    REQUIRE(p.name == "Rock 6/8 Feel");
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

// ── Rock-first set (A4.1) ────────────────────────────────────────────────────

TEST_CASE("MidiPatternLibrary: rock patterns 22-27 exist with drum and bass content", "[midi_pattern_library][rock]")
{
    MidiPatternLibrary lib;
    const char* expectedNames[] = { "Rock Backbeat", "Rock Half-Time", "Rock Shuffle",
                                    "Punk D-Beat", "Rock Ballad", "Rock 6/8 Feel" };
    for (int i = 0; i < 6; ++i)
    {
        const int idx = 22 + i;
        const auto& p = lib.getPattern(idx);
        REQUIRE(p.name == expectedNames[i]);
        REQUIRE_FALSE(p.drumEvents.empty());
        REQUIRE_FALSE(p.bassEvents.empty());  // authored bass lines must reach the player (A1.1)
    }
}

TEST_CASE("MidiPatternLibrary: rock backbeat has kick 1/3 and snare 2/4", "[midi_pattern_library][rock]")
{
    MidiPatternLibrary lib;
    const auto& p = lib.getPattern(22);
    bool kick1 = false, kick3 = false, snare2 = false, snare4 = false;
    for (const auto& e : p.drumEvents)
    {
        if (e.note == 36 && e.beatOffset < 0.5f) kick1 = true;
        if (e.note == 36 && e.beatOffset >= 1.75f && e.beatOffset <= 2.25f) kick3 = true;
        if (e.note == 38 && e.beatOffset >= 0.75f && e.beatOffset <= 1.25f) snare2 = true;
        if (e.note == 38 && e.beatOffset >= 2.75f && e.beatOffset <= 3.25f) snare4 = true;
    }
    REQUIRE(kick1);
    REQUIRE(kick3);
    REQUIRE(snare2);
    REQUIRE(snare4);
}

TEST_CASE("MidiPatternLibrary: open hats have a musical gate and ghosts are marked", "[midi_pattern_library][phase1]")
{
    MidiPatternLibrary lib;
    for (int i = 0; i < lib.patternCount(); ++i)
    {
        for (const auto& e : lib.getPattern(i).drumEvents)
        {
            if (e.note == 46)
            {
                INFO("pattern " << i << " open hat at " << e.beatOffset);
                REQUIRE(e.durationBeats >= 1.0f);
            }
            if (e.isGhost)
                REQUIRE(e.velocity <= 62);
        }
    }
}

// Regression: "Sparse Breakdown" used to be 4 hits over 2 bars — a ~5 s
// near-silence that read as a drum dropout (measured on a real Play take).
// It must stay sparse but never leave a full beat without a timekeeper.
TEST_CASE("MidiPatternLibrary: Sparse Breakdown has no dead beat", "[midi_pattern_library][genre]")
{
    MidiPatternLibrary lib;
    MidiPattern p{};
    for (int i = 0; i < lib.patternCount(); ++i)
        if (lib.getPattern(i).name == std::string("Sparse Breakdown"))
            p = lib.getPattern(i);

    REQUIRE(p.name == std::string("Sparse Breakdown"));
    const int totalBeats = static_cast<int>(p.lengthInBars * 4.0f);

    // Every beat in the 2-bar loop must contain at least one drum hit.
    for (int beat = 0; beat < totalBeats; ++beat)
    {
        bool any = false;
        for (const auto& e : p.drumEvents)
            if (static_cast<int>(e.beatOffset) == beat)
                any = true;
        INFO("beat " << beat);
        REQUIRE(any);
    }

    // Still sparse: far fewer hits than a normal groove of the same length.
    REQUIRE(p.drumEvents.size() <= 14u);
}
