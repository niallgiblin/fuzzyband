#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include "midi/GrooveTemplate.h"
#include "midi/MidiPatternLibrary.h"
#include "midi/PatternPlayer.h"

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

TEST_CASE("Groove: structured microtiming — swung 'e' late, beats pushed early", "[groove]")
{
    // C1 (data-derived from GMD rock): the real microtiming signature is a light
    // 16th swing — the "e" of each beat (cells 1/5/9/13) lands laid-back while the
    // beats themselves (incl. the backbeat) are pushed slightly early. This
    // replaces the old hand-authored "backbeat late" assumption, which the pooled
    // GMD data did not confirm.
    const auto t = Groove::rock();
    REQUIRE(t.timingMs[1] > 0.0f);          // swung 'e' of beat 1 is laid-back
    REQUIRE(t.timingMs[0] < 0.0f);          // downbeat pushed early (punch)
    REQUIRE(t.timingMs[1] > t.timingMs[0]); // 'e' later than the beat it follows
}

TEST_CASE("Groove: C1 data-derived templates — tightening hierarchy + sane ranges", "[groove][C1]")
{
    const auto rock  = Groove::rock();
    const auto metal = Groove::metal();
    const auto punk  = Groove::punk();

    // Metal/punk are derived from rock by pulling timing toward the grid, so
    // their per-cell offsets are strictly smaller in magnitude (tighter feel),
    // and jitter shrinks rock -> metal -> punk.
    for (int c = 0; c < 16; ++c)
    {
        REQUIRE(std::abs(metal.timingMs[c]) <= std::abs(rock.timingMs[c]) + 1.0e-4f);
        REQUIRE(std::abs(punk.timingMs[c])  <= std::abs(metal.timingMs[c]) + 1.0e-4f);
    }
    REQUIRE(metal.timingJitterMs < rock.timingJitterMs);
    REQUIRE(punk.timingJitterMs  < metal.timingJitterMs);

    // Data-derived values must stay in musically sane, RT-safe ranges.
    for (int c = 0; c < 16; ++c)
    {
        REQUIRE(rock.velocityMul[c] > 0.2f);
        REQUIRE(rock.velocityMul[c] < 2.0f);
        REQUIRE(std::abs(rock.timingMs[c]) < 30.0f);
    }
    REQUIRE(rock.timingJitterMs > 0.0f);
    REQUIRE(rock.timingJitterMs <= 3.0f);
    REQUIRE(metal.timingJitterMs <= 2.0f);
    REQUIRE(rock.velocityJitter > 0.0f);

    REQUIRE(std::abs(Groove::timingMeanMs(Groove::data::kRockTimingMs)) < 1.5f);
    REQUIRE(std::abs(Groove::timingMeanMs(Groove::data::kMetalTimingMs)) < 1.5f);
    REQUIRE(std::abs(Groove::timingMeanMs(Groove::data::kPunkTimingMs)) < 1.5f);

    // The backbeats (cells 4 and 12) must remain the loudest primary accents —
    // the whole point of a data-derived velocity hierarchy.
    REQUIRE(rock.velocityMul[4]  > rock.velocityMul[2]);
    REQUIRE(rock.velocityMul[12] > rock.velocityMul[14]);
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
    // C1: metal is "tighter" = its microtiming sits closer to the grid (smaller
    // magnitude) than rock, and its jitter is smaller — not necessarily earlier.
    const auto metalTmpl = Groove::templateFor(metal.templateId);
    REQUIRE(std::abs(metalTmpl.timingMs[4]) <= std::abs(Groove::rock().timingMs[4]));
    REQUIRE(metalTmpl.timingJitterMs <= Groove::rock().timingJitterMs);
    REQUIRE(Groove::rock().ghostThreshold == 62);
    REQUIRE(Groove::rock().ghostVelocityLo == 30.0f);
    REQUIRE(Groove::rock().ghostVelocityHi == 55.0f);
}

TEST_CASE("Groove: A3.1 dynamic contrast — chorus backbeat >= verse backbeat + 15", "[groove][A3]")
{
    // Metric from the plan (§7): measured velocity delta between verse and
    // chorus backbeat >= 15. Authored backbeats: Verse Groove = 110,
    // Chorus Mid = 118. Effective = authored × section multiplier × backbeat accent.
    const auto& rock = Groove::presetFor(0);
    const float backbeatAccent = Groove::rock().velocityMul[4];
    const float trim = PatternPlayer::kVelocityTrim;
    const float verseVel = 110.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Verse)
                         * backbeatAccent * trim;
    const float chorusVel = 118.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Chorus)
                          * backbeatAccent * trim;
    REQUIRE(chorusVel - verseVel >= 15.0f);
}

TEST_CASE("Groove: pattern count constant matches the library", "[groove][A4]")
{
    MidiPatternLibrary lib;
    REQUIRE(lib.patternCount() == MidiPatternLibrary::kPatternCount);
}
