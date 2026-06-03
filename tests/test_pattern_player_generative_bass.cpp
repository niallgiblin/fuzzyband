#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include "midi/MidiPatternLibrary.h"
#include "midi/PatternPlayer.h"

// PatternPlayer constants (mirrored from PatternPlayer.h)
static constexpr int kBassChannel = 2;
static constexpr int kDrumChannel = 10;

// At 120 BPM, 48000 Hz: one bar = 4 beats × 0.5 s × 48000 = 96000 samples.
static constexpr double kSampleRate = 48000.0;
static constexpr float  kBpm        = 120.0f;
static constexpr int    kOneBeat    = 24000; // 0.5 s at 48 kHz
static constexpr int    kOneBar     = 96000;
static constexpr int    kTwoBars    = 192000;

// ─── Library bass is suppressed when generative mode is active ───────────────

TEST_CASE("PatternPlayer: library bass (ch2) is suppressed when generative bass is active", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 512);
    player.setBpm(kBpm);
    player.setPatternIndex(1); // VerseSlow — bass root note 40
    player.setStructureSilent(false);

    // Use a distinct root note so we can distinguish generative from library output
    player.setGenerativeBassActive(true, 45, 1.0f);

    juce::MidiBuffer midi;
    player.process(midi, kTwoBars, 0);

    bool sawNote40OnCh2 = false;
    bool sawNote45OnCh2 = false;

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == kBassChannel)
        {
            if (msg.getNoteNumber() == 40)
                sawNote40OnCh2 = true;
            if (msg.getNoteNumber() == 45)
                sawNote45OnCh2 = true;
        }
    }

    // Library note 40 must not appear; generative root 45 must appear
    REQUIRE_FALSE(sawNote40OnCh2);
    REQUIRE(sawNote45OnCh2);
}

// ─── Generative root MIDI note appears on channel 2 ─────────────────────────

TEST_CASE("PatternPlayer: generative bass emits the configured root MIDI note on channel 2", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 512);
    player.setBpm(kBpm);
    player.setPatternIndex(1);
    player.setStructureSilent(false);
    player.setGenerativeBassActive(true, 42, 1.0f);

    juce::MidiBuffer midi;
    player.process(midi, kOneBar, 0);

    bool sawRoot = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == kBassChannel && msg.getNoteNumber() == 42)
            sawRoot = true;
    }

    REQUIRE(sawRoot);
}

// ─── Note-off is emitted after the generative bass note ─────────────────────

TEST_CASE("PatternPlayer: generative bass note-on is followed by a note-off", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 512);
    player.setBpm(kBpm);
    player.setPatternIndex(1);
    player.setStructureSilent(false);
    player.setGenerativeBassActive(true, 40, 1.0f); // 1.0 beat duration

    juce::MidiBuffer midi;
    player.process(midi, kTwoBars, 0);

    int noteOns  = 0;
    int noteOffs = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.getChannel() == kBassChannel)
        {
            if (msg.isNoteOn())  ++noteOns;
            if (msg.isNoteOff()) ++noteOffs;
        }
    }

    // At least one note-on must be paired with a note-off
    REQUIRE(noteOns > 0);
    REQUIRE(noteOffs > 0);
}

// ─── Drum events are still emitted when generative bass is active ─────────────

TEST_CASE("PatternPlayer: drum events (ch10) still fire with generative bass active", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 512);
    player.setBpm(kBpm);
    player.setPatternIndex(1); // VerseSlow has drum events
    player.setStructureSilent(false);
    player.setGenerativeBassActive(true, 40, 1.0f);

    juce::MidiBuffer midi;
    player.process(midi, kTwoBars, 0);

    bool sawDrum = false;
    for (const auto meta : midi)
    {
        if (meta.getMessage().getChannel() == kDrumChannel)
            sawDrum = true;
    }

    REQUIRE(sawDrum);
}

// ─── Reverting to library bass re-enables library events ─────────────────────

TEST_CASE("PatternPlayer: reverting to library bass after generative mode restores library notes", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 512);
    player.setBpm(kBpm);
    player.setPatternIndex(1); // VerseSlow bass root = 40
    player.setStructureSilent(false);

    // First: generative mode active — library note 40 is suppressed
    {
        player.setGenerativeBassActive(true, 55, 1.0f);
        juce::MidiBuffer midi;
        player.process(midi, kOneBar, 0);

        bool sawLib = false;
        for (const auto m : midi)
            if (m.getMessage().isNoteOn() && m.getMessage().getChannel() == kBassChannel
                && m.getMessage().getNoteNumber() == 40)
                sawLib = true;
        REQUIRE_FALSE(sawLib);
    }

    // Second: revert to library — note 40 should appear again
    {
        player.setGenerativeBassActive(false, 55, 1.0f);
        juce::MidiBuffer midi;
        player.process(midi, kOneBar, static_cast<int64_t>(kOneBar));

        bool sawLib = false;
        for (const auto m : midi)
            if (m.getMessage().isNoteOn() && m.getMessage().getChannel() == kBassChannel
                && m.getMessage().getNoteNumber() == 40)
                sawLib = true;
        REQUIRE(sawLib);
    }
}

// ─── Generative bass semitone offset is NOT applied (root is absolute) ────────

TEST_CASE("PatternPlayer: setBassSemitoneOffset does not affect generative bass root", "[midi]")
{
    // Per docs/BASS_ONNX_IO.md: Y_bass root is absolute; bassSemitoneOffset is for library path only.
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 512);
    player.setBpm(kBpm);
    player.setPatternIndex(1);
    player.setStructureSilent(false);
    player.setBassSemitoneOffset(5);           // +5 applied to library path
    player.setGenerativeBassActive(true, 40, 1.0f); // absolute root 40

    juce::MidiBuffer midi;
    player.process(midi, kOneBar, 0);

    bool sawRoot40 = false;
    bool sawRoot45 = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == kBassChannel)
        {
            if (msg.getNoteNumber() == 40) sawRoot40 = true;
            if (msg.getNoteNumber() == 45) sawRoot45 = true;
        }
    }

    REQUIRE(sawRoot40);      // absolute root — no semitone shift applied
    REQUIRE_FALSE(sawRoot45); // shifted root must not appear
}

TEST_CASE("PatternPlayer: bar-aligned sixteen-step bass (RHY-BASSSEQ-01 / D-28-06)", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 256);
    player.setBpm(kBpm);
    player.setPatternIndex(1);
    player.setStructureSilent(false);
    player.snapToBarStart();

    float pitch[16] = {};
    float vel[16] = {};
    vel[0] = 100.0f;
    vel[4] = 100.0f;
    pitch[0] = 0.0f;
    pitch[4] = 2.0f;

    player.setGenerativeBassSteps(pitch, vel, 40.0f);

    juce::MidiBuffer midi;
    player.process(midi, kOneBar, 0);

    int bassOnCount = 0;
    bool sawDistinctNotes = false;
    std::vector<int> notesHit;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == kBassChannel)
        {
            ++bassOnCount;
            notesHit.push_back(msg.getNoteNumber());
        }
    }

    REQUIRE(bassOnCount >= 2);

    // Two different MIDI notes — steps 0 and 4 resolved to different pitches (+2 semitone offset).
    for (size_t i = 0; i < notesHit.size() && !sawDistinctNotes; ++i)
        for (size_t j = i + 1; j < notesHit.size(); ++j)
            if (notesHit[i] != notesHit[j])
                sawDistinctNotes = true;
    REQUIRE(sawDistinctNotes);
}

TEST_CASE("PatternPlayer: RHY-SYNC-01 queueGrooveCommit activates drums and generated bass together", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 256);
    player.snapBpm(kBpm);
    player.setPatternIndex(0);
    player.setStructureSilent(false);
    player.snapToBarStart();

    juce::MidiBuffer warmup;
    player.process(warmup, kOneBar - kOneBeat, 0);

    PatternPlayer::GrooveCommit commit{};
    commit.patternIndex = 1;
    commit.hasBassFrame = true;
    commit.bassPitchOffset[0] = 0.0f;
    commit.bassVelocity[0] = 100.0f;
    commit.bassRootMidi = 43.0f;
    player.queueGrooveCommit(commit);

    juce::MidiBuffer beforeBoundary;
    player.process(beforeBoundary, kOneBeat, kOneBar - kOneBeat);

    bool sawEarlyGeneratedBass = false;
    for (const auto meta : beforeBoundary)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == kBassChannel && msg.getNoteNumber() == 43)
            sawEarlyGeneratedBass = true;
    }
    REQUIRE_FALSE(sawEarlyGeneratedBass);

    juce::MidiBuffer boundary;
    player.process(boundary, kOneBar, kOneBar);

    bool sawDrum = false;
    bool sawGeneratedBass = false;
    for (const auto meta : boundary)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == kDrumChannel)
            sawDrum = true;
        if (msg.isNoteOn() && msg.getChannel() == kBassChannel && msg.getNoteNumber() == 43)
            sawGeneratedBass = true;
    }

    REQUIRE(sawDrum);
    REQUIRE(sawGeneratedBass);
}

TEST_CASE("PatternPlayer: clearing a pending groove commit prevents deferred generated bass activation", "[midi]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(kSampleRate, 256);
    player.snapBpm(kBpm);
    player.setPatternIndex(0);
    player.setStructureSilent(false);
    player.snapToBarStart();

    juce::MidiBuffer warmup;
    player.process(warmup, kOneBar - kOneBeat, 0);

    PatternPlayer::GrooveCommit commit{};
    commit.patternIndex = 1;
    commit.hasBassFrame = true;
    commit.bassPitchOffset[0] = 0.0f;
    commit.bassVelocity[0] = 100.0f;
    commit.bassRootMidi = 43.0f;
    player.queueGrooveCommit(commit);

    player.setGenerativeBassActive(false, 40, 1.0f);
    player.clearPendingGrooveCommit();

    juce::MidiBuffer boundary;
    player.process(boundary, kOneBeat, kOneBar - kOneBeat);
    player.process(boundary, kOneBar, kOneBar);

    bool sawGeneratedBass = false;
    for (const auto meta : boundary)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == kBassChannel && msg.getNoteNumber() == 43)
            sawGeneratedBass = true;
    }

    REQUIRE_FALSE(sawGeneratedBass);
}
