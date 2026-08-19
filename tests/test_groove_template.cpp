#include <catch2/catch_test_macros.hpp>
#include <string>
#include "midi/GrooveTemplate.h"
#include "midi/MidiPatternLibrary.h"

TEST_CASE("Groove: grid16Of maps beats to 16th cells within a 4/4 bar", "[groove]")
{
    REQUIRE(Groove::grid16Of(0.0f) == 0);    // beat 1
    REQUIRE(Groove::grid16Of(1.0f) == 4);    // beat 2 (backbeat)
    REQUIRE(Groove::grid16Of(2.5f) == 10);   // "and" of 3
    REQUIRE(Groove::grid16Of(3.75f) == 15);  // "a" of 4
    REQUIRE(Groove::grid16Of(4.0f) == 0);    // bar 2 downbeat
    REQUIRE(Groove::grid16Of(5.0f) == 4);    // bar 2 backbeat
    REQUIRE(Groove::grid16Of(7.75f) == 15);  // bar 2 "a" of 4
}

TEST_CASE("Groove: rock velocity hierarchy — downbeat/backbeat above 8ths above off-16ths", "[groove]")
{
    const auto t = Groove::rock();
    REQUIRE(t.velocityMul[0] >= t.velocityMul[2]);    // downbeat >= off-8th
    REQUIRE(t.velocityMul[4] >= t.velocityMul[2]);    // backbeat >= off-8th
    REQUIRE(t.velocityMul[12] >= t.velocityMul[2]);   // backbeat 4 >= off-8th
    REQUIRE(t.velocityMul[2] >= t.velocityMul[1]);    // off-8th > off-16th
    REQUIRE(t.velocityMul[4] >= t.velocityMul[8]);    // backbeat >= beat 3
}

TEST_CASE("Groove: structured microtiming — backbeat late, kick early", "[groove]")
{
    const auto t = Groove::rock();
    REQUIRE(t.timingMs[4] > 0.0f);   // laid-back backbeat
    REQUIRE(t.timingMs[12] > 0.0f);
    REQUIRE(t.timingMs[0] <= 0.0f);  // punchy kick
}

TEST_CASE("Groove: genre presets select templates and section dynamics", "[groove][B1]")
{
    REQUIRE(Groove::presetCount() >= 4);
    const auto& rock = Groove::presetFor(0);
    REQUIRE(std::string(rock.name) == "Rock");
    REQUIRE(rock.verseVel < rock.chorusVel);   // dynamic contrast
    REQUIRE(Groove::presetFor(99).name != nullptr);  // out-of-range clamps
    REQUIRE(Groove::presetFor(-1).name != nullptr);

    REQUIRE(Groove::sectionIdFromName("VERSE") == Groove::SongSectionId::Verse);
    REQUIRE(Groove::sectionIdFromName("CHORUS") == Groove::SongSectionId::Chorus);
    REQUIRE(Groove::sectionIdFromName("BREAKDOWN") == Groove::SongSectionId::Breakdown);
    REQUIRE(Groove::sectionIdFromName("INTRO") == Groove::SongSectionId::Intro);
    REQUIRE(Groove::sectionIdFromName("OUTRO") == Groove::SongSectionId::Outro);
    REQUIRE(Groove::sectionIdFromName("SOLO") == Groove::SongSectionId::Solo);
    REQUIRE(Groove::sectionIdFromName("NOPE") == Groove::SongSectionId::Unknown);
    REQUIRE(Groove::sectionIdFromName(nullptr) == Groove::SongSectionId::Unknown);
}

TEST_CASE("Groove: metal preset is heavier than rock", "[groove][B1]")
{
    const auto& metal = Groove::presetFor(3);
    REQUIRE(metal.templateId != 0);
    REQUIRE(Groove::templateFor(metal.templateId).timingMs[4] <= Groove::rock().timingMs[4]);
}

TEST_CASE("Groove: A3.1 dynamic contrast — chorus backbeat >= verse backbeat + 15", "[groove][A3]")
{
    // Metric from the plan (§7): measured velocity delta between verse and
    // chorus backbeat >= 15. Authored backbeats: Verse Groove = 110,
    // Chorus Mid = 118. Effective = authored × section multiplier × backbeat accent.
    const auto& rock = Groove::presetFor(0);
    const float backbeatAccent = Groove::rock().velocityMul[4];
    const float verseVel = 110.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Verse) * backbeatAccent;
    const float chorusVel = 118.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Chorus) * backbeatAccent;
    REQUIRE(chorusVel - verseVel >= 15.0f);
}

TEST_CASE("Groove: pattern count constant matches the library", "[groove][A4]")
{
    MidiPatternLibrary lib;
    REQUIRE(lib.patternCount() == MidiPatternLibrary::kPatternCount);
}
