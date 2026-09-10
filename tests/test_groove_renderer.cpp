#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include "midi/GrooveGrid.h"
#include "midi/MidiPatternLibrary.h"
#include "midi/PatternPlayer.h"
#include "inference/GrooveRenderer.h"

namespace
{

int noteOnVelocity(const juce::MidiBuffer& midi, int note)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getChannel() == 10 && m.getNoteNumber() == note)
            return static_cast<int>(std::round(m.getFloatVelocity() * 127.0f));
    }
    return -1;
}

} // namespace

TEST_CASE("GrooveGrid voice + step mapping", "[groove_renderer]")
{
    REQUIRE(GrooveGridUtil::voiceForNote(36) == 0);
    REQUIRE(GrooveGridUtil::voiceForNote(35) == 0);
    REQUIRE(GrooveGridUtil::voiceForNote(38) == 1);
    REQUIRE(GrooveGridUtil::voiceForNote(42) == 2);
    REQUIRE(GrooveGridUtil::voiceForNote(46) == 3);
    REQUIRE(GrooveGridUtil::voiceForNote(51) == 4);
    REQUIRE(GrooveGridUtil::voiceForNote(53) == 5);
    REQUIRE(GrooveGridUtil::voiceForNote(49) == 6);
    REQUIRE(GrooveGridUtil::voiceForNote(48) == 7);
    REQUIRE(GrooveGridUtil::voiceForNote(45) == 8);
    REQUIRE(GrooveGridUtil::voiceForNote(41) == 9);
    REQUIRE(GrooveGridUtil::voiceForNote(60) == -1);  // unmapped

    REQUIRE(GrooveGridUtil::stepForBeatInBar(0.0f) == 0);
    REQUIRE(GrooveGridUtil::stepForBeatInBar(0.25f) == 1);
    REQUIRE(GrooveGridUtil::stepForBeatInBar(1.0f) == 4);
    REQUIRE(GrooveGridUtil::stepForBeatInBar(2.0f) == 8);
    REQUIRE(GrooveGridUtil::stepForBeatInBar(3.75f) == 15);
}

TEST_CASE("GrooveRenderer::buildScoreGrid quantizes a pattern into the 10-voice grid", "[groove_renderer]")
{
    MidiPatternLibrary lib;
    const MidiPattern& p = lib.getPattern(1);  // Verse Groove (kick/snare/hat/ride bell)

    ScoreGrid score{};
    GrooveRenderer::buildScoreGrid(p, score);

    REQUIRE(score[0][0] == 1.0f);   // kick on the downbeat
    REQUIRE(score[0][8] == 1.0f);   // kick on beat 3
    REQUIRE(score[1][4] == 1.0f);   // snare backbeat (beat 2)
    REQUIRE(score[1][12] == 1.0f);  // snare backbeat (beat 4)
    REQUIRE(score[2][2] == 1.0f);   // closed hat on the "and" of 1
    REQUIRE(score[2][14] == 1.0f);  // closed hat on the "and" of 4
    REQUIRE(score[5][0] == 1.0f);   // ride bell on the downbeat
    REQUIRE(score[3][0] == 0.0f);   // no open hat on the downbeat
}

TEST_CASE("GrooveRenderer::buildCondition packs bpm/genre/phase, neutralises guitar dims", "[groove_renderer]")
{
    FeatureVector f;
    f.bpm = 120.0f;
    f.rmsEnergy = 0.5f;
    f.spectralCentroid = 2000.0f;
    f.onsetDensityPerBeat = 2.0f;
    f.state = StructureState::LOUD;

    float cond[GrooveGridUtil::kCondDim]{};
    GrooveRenderer::buildCondition(f, /*genreId*/ 3, /*barNumber*/ 7, /*styleIndex*/ 2, cond);

    REQUIRE(cond[0] == Catch::Approx((120.0f - 40.0f) / 260.0f));
    // v2: guitar-derived dims (rms/centroid/density/style/state) are neutralised
    // so the groove model sees exactly the distribution it trained on (GMD).
    for (int i = 1; i <= 11; ++i)
        REQUIRE(cond[i] == 0.0f);

    REQUIRE(cond[15] == 1.0f);  // genre one-hot (3 = Metal) at 12+3
    REQUIRE(cond[17] == Catch::Approx(3.0f / 4.0f));  // bar phase (7 % 4 = 3)
}

TEST_CASE("PatternPlayer uses a rendered GrooveGrid for velocity (template fallback otherwise)", "[groove_renderer]")
{
    // Pattern 1 (Verse Groove) kick on the downbeat, authored velocity 115.
    // Section = Verse, Rock preset 0 -> sectionVelMul = 0.92 × kVelocityTrim.

    // (a) With a valid grid: velocity is taken from the grid (no jitter).
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.snapBpm(120.0f);
        player.setStructureSilent(false);
        player.setSection(Groove::SongSectionId::Verse);
        player.setGenrePreset(0);
        player.setRandomSeed(7);

        GrooveGrid grid;
        grid.valid = true;
        grid.patternIndex = 1;
        grid.velocity[0][0] = 0.5f;   // kick multiplier
        player.setGrooveGrid(grid);
        player.setPatternIndex(1);

        juce::MidiBuffer midi;
        player.process(midi, 4800, 0);
        // authored 115 × 0.5 × verse 0.92 × kVelocityTrim 0.80 = 42
        REQUIRE(noteOnVelocity(midi, 36) == 42);
    }

    // (b) Without a grid (invalid): template path -> authored 115 * hierarchy.
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.snapBpm(120.0f);
        player.setStructureSilent(false);
        player.setSection(Groove::SongSectionId::Verse);
        player.setGenrePreset(0);
        player.setRandomSeed(7);
        player.setPatternIndex(1);

        juce::MidiBuffer midi;
        player.process(midi, 4800, 0);
        REQUIRE(noteOnVelocity(midi, 36) > 70);
    }

    // (c) After reset(), a previously injected grid is cleared (live-path disconnect).
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.snapBpm(120.0f);
        player.setStructureSilent(false);
        player.setSection(Groove::SongSectionId::Verse);
        player.setGenrePreset(0);
        player.setRandomSeed(7);

        GrooveGrid grid;
        grid.valid = true;
        grid.patternIndex = 1;
        grid.velocity[0][0] = 0.5f;
        player.setGrooveGrid(grid);
        player.reset();
        player.setPatternIndex(1);

        juce::MidiBuffer midi;
        player.process(midi, 4800, 0);
        REQUIRE(noteOnVelocity(midi, 36) > 70);
    }
}

TEST_CASE("genre preset list stays 13", "[groove_renderer][genre]")
{
    REQUIRE(Groove::kPresetCount == 13);
    REQUIRE(Groove::presetCount() == 13);
}
