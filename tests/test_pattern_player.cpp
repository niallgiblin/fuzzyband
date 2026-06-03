#include <catch2/catch_test_macros.hpp>
#include <map>
#include <vector>
#include <utility>
#include <juce_audio_basics/juce_audio_basics.h>
#include "midi/MidiPatternLibrary.h"
#include "midi/PatternPlayer.h"

namespace
{

bool hasDrumNoteOn(const juce::MidiBuffer& midi, int note)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == note)
            return true;
    }
    return false;
}

std::vector<int> drumNoteOns(const juce::MidiBuffer& midi)
{
    std::vector<int> notes;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 10)
            notes.push_back(msg.getNoteNumber());
    }
    return notes;
}

juce::MidiBuffer renderQueuedFill(PatternPlayer::TransitionFillKind kind, int patternIndex)
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);

    PatternPlayer::GrooveCommit commit{};
    commit.patternIndex = patternIndex;
    commit.fillKind = kind;
    player.queueGrooveCommit(commit);

    juce::MidiBuffer midi;
    player.process(midi, 512, 0);
    return midi;
}

} // namespace

TEST_CASE("PatternPlayer emits MIDI for non-silent pattern", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setBpm(120.0f);
    player.setPatternIndex(1);
    player.setStructureSilent(false);

    juce::MidiBuffer midi;
    player.process(midi, 4800, 0);

    REQUIRE(midi.getNumEvents() > 0);
}

TEST_CASE("PatternPlayer humanization velocity stays within +/-10 of base", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setBpm(120.0f);
    player.setPatternIndex(1);  // VerseSlow
    player.setStructureSilent(false);

    for (int trial = 0; trial < 200; ++trial)
    {
        juce::MidiBuffer midi;
        player.process(midi, 512, static_cast<int64_t>(trial) * 512);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                const int vel = msg.getVelocity();
                REQUIRE(vel >= 1);
                REQUIRE(vel <= 127);
                // VerseSlow base velocities are 78-110; humanization delta in [-10, +10].
                // Bounds: min = 78-10=68, max = 110+10=120.
                REQUIRE(vel >= 68);
                REQUIRE(vel <= 120);
            }
        }
    }
}

TEST_CASE("PatternPlayer timing offsets fit within the block (+/-2ms humanization bounded)", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);

    const double sr = 48000.0;
    player.prepare(sr, 512);
    player.setBpm(120.0f);
    player.setPatternIndex(1);
    player.setStructureSilent(false);

    // One bar at 120 BPM = 4 beats * 0.5s/beat * 48000 Hz = 96000 samples
    const int blockSamples = 96000;
    juce::MidiBuffer midi;
    player.process(midi, blockSamples, 0);

    REQUIRE(midi.getNumEvents() > 0);
    for (const auto meta : midi)
    {
        REQUIRE(meta.samplePosition >= 0);
        REQUIRE(meta.samplePosition < blockSamples);
    }
}

TEST_CASE("PatternPlayer notes do not overlap at BPM extremes", "[midi]")
{
    for (float bpm : { 40.0f, 300.0f })
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.setBpm(bpm);
        player.setPatternIndex(1);  // VerseSlow
        player.setStructureSilent(false);

        // 8 bars at this BPM
        const double samplesPerBar = (60.0 / static_cast<double>(bpm)) * 4.0 * 48000.0;
        const int blockSamples = static_cast<int>(samplesPerBar * 8);

        juce::MidiBuffer midi;
        player.process(midi, blockSamples, 0);

        // Track (channel, note) → is-active; if NoteOn arrives while active, flag overlap.
        std::map<std::pair<int, int>, bool> activeNotes;
        bool anyOverlap = false;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const auto key = std::make_pair(msg.getChannel(), msg.getNoteNumber());
            if (msg.isNoteOn())
            {
                if (activeNotes[key])
                    anyOverlap = true;
                activeNotes[key] = true;
            }
            else if (msg.isNoteOff())
            {
                activeNotes[key] = false;
            }
        }
        REQUIRE_FALSE(anyOverlap);
    }
}

TEST_CASE("PatternPlayer::snapBpm sets BPM immediately without EMA", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 256);
    // Internal bpm starts at 120.0f after reset().
    // At 100 BPM, one beat = 48000*60/100 = 28800 samples. Beat 0 kick fires at sample 0.
    player.snapBpm(100.0f);
    player.setPatternIndex(1); // VerseSlow — kick at beat 0
    player.setStructureSilent(false);

    juce::MidiBuffer midi;
    player.process(midi, 29000, 0);
    bool foundKick = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 36)
            foundKick = true;
    }
    REQUIRE(foundKick);
}

TEST_CASE("PatternPlayer applies bass semitone offset", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setBpm(120.0f);
    player.setPatternIndex(1); // VerseSlow — first bass note is kBassRoot (40)
    player.setStructureSilent(false);
    player.setBassSemitoneOffset(2);

    juce::MidiBuffer midi;
    player.process(midi, 96000, 0);

    bool sawShiftedRoot = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 2) // kBassChannel
        {
            if (msg.getNoteNumber() == 42)
                sawShiftedRoot = true;
        }
    }
    REQUIRE(sawShiftedRoot);
}

TEST_CASE("RHY-FILL-01: Entry fill emits crash and kick at a queued groove commit", "[midi]")
{
    const auto midi = renderQueuedFill(PatternPlayer::TransitionFillKind::Entry, 1);

    REQUIRE(hasDrumNoteOn(midi, 49));
    REQUIRE(hasDrumNoteOn(midi, 36));
}

TEST_CASE("RHY-FILL-01: BuildUp fill emits a distinct snare and tom gesture", "[midi]")
{
    const auto midi = renderQueuedFill(PatternPlayer::TransitionFillKind::BuildUp, 0);
    const auto notes = drumNoteOns(midi);

    REQUIRE(hasDrumNoteOn(midi, 38));
    REQUIRE(hasDrumNoteOn(midi, 45));
    REQUIRE(notes.size() == 2);
}

TEST_CASE("RHY-FILL-01: Release fill emits a distinct snare and closed-hat gesture", "[midi]")
{
    const auto midi = renderQueuedFill(PatternPlayer::TransitionFillKind::Release, 0);
    const auto notes = drumNoteOns(midi);

    REQUIRE(hasDrumNoteOn(midi, 38));
    REQUIRE(hasDrumNoteOn(midi, 42));
    REQUIRE(notes.size() == 2);
}

TEST_CASE("RHY-FILL-01: BreakdownOrImpact fill emits a kick crash and floor-tom gesture", "[midi]")
{
    const auto midi = renderQueuedFill(PatternPlayer::TransitionFillKind::BreakdownOrImpact, 6);

    REQUIRE(hasDrumNoteOn(midi, 36));
    REQUIRE(hasDrumNoteOn(midi, 49));
    REQUIRE(hasDrumNoteOn(midi, 43));
}
