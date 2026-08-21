#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <algorithm>
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

TEST_CASE("PatternPlayer velocity hierarchy: ghost notes land in the 30-55 band", "[midi][A2]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(12345);
    player.setBpm(120.0f);
    player.setPatternIndex(1);  // Verse Groove — no authored ghosts
    player.setSection(Groove::SongSectionId::Verse);  // ghost injection enabled (A3.2)
    player.setGenrePreset(0);   // Rock — ghostDensity 0.35
    player.setStructureSilent(false);

    juce::MidiBuffer midi;
    player.process(midi, 96000, 0);

    bool sawGhost = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            REQUIRE(msg.getVelocity() >= 1);
            REQUIRE(msg.getVelocity() <= 127);
            if (msg.getChannel() == 10 && msg.getNoteNumber() == 38
                && msg.getVelocity() >= 30 && msg.getVelocity() <= 55)
                sawGhost = true;
        }
    }
    REQUIRE(sawGhost);
}

TEST_CASE("PatternPlayer ghost injection is section-gated (no ghosts in chorus)", "[midi][A3]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(12345);
    player.setBpm(120.0f);
    player.setPatternIndex(1);  // Verse Groove — no authored ghosts
    player.setSection(Groove::SongSectionId::Chorus);
    player.setGenrePreset(0);
    player.setStructureSilent(false);

    juce::MidiBuffer midi;
    player.process(midi, 96000, 0);

    bool sawGhost = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 38
            && msg.getVelocity() >= 30 && msg.getVelocity() <= 55)
            sawGhost = true;
    }
    REQUIRE_FALSE(sawGhost);
}

TEST_CASE("PatternPlayer timing offsets fit within the block (bounded microtiming)", "[midi]")
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

// ── Musicality pivot: bass engine (A1) ───────────────────────────────────────

TEST_CASE("A1: authored bassEvents play, transposed to the live root", "[midi][A1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(99);
    player.snapBpm(120.0f);
    player.setPatternIndex(4);  // Chorus Mid — bassEvents: root, +5, root, +7
    player.setStructureSilent(false);
    player.setBassParams(40, 2);  // E2 live root

    juce::MidiBuffer midi;
    player.process(midi, 192000, 0);  // 2 bars at 120 BPM (Chorus Mid is a 2-bar pattern)

    std::set<int> bassNotes;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 2)
            bassNotes.insert(msg.getNoteNumber());
    }
    // Authored notes 36, 41, 43 transposed by (liveRoot - 36): 40, 45, 47.
    REQUIRE(bassNotes.count(40) > 0);  // root
    REQUIRE(bassNotes.count(45) > 0);  // +5 (fourth)
    REQUIRE(bassNotes.count(47) > 0);  // +7 (fifth)
}

TEST_CASE("A1: pattern without bassEvents uses the harmonic fallback (root + dynamics)", "[midi][A1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(7);
    player.snapBpm(120.0f);
    player.setPatternIndex(11);  // Intro Build — no authored bass
    player.setStructureSilent(false);
    player.setBassParams(40, 2);

    juce::MidiBuffer midi;
    player.process(midi, 96000, 0);

    bool sawRoot = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 2)
        {
            REQUIRE(msg.getNoteNumber() >= 28);
            REQUIRE(msg.getNoteNumber() <= 55);
            if (msg.getNoteNumber() == 40)
                sawRoot = true;
        }
    }
    REQUIRE(sawRoot);
}

TEST_CASE("A1: multi-pitch bass lines never leave stuck notes across small blocks", "[midi][A1]")
{
    // Regression: authored bass events with different pitches (root/fourth/fifth)
    // deferred note-offs across small audio blocks must always close the correct
    // note. Before the fix, the single deferred slot emitted noteOff for the
    // *last* note played, so the first note stayed stuck on (harsh drone).
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(11);
    player.snapBpm(120.0f);
    player.setPatternIndex(4);  // Chorus Mid — bassEvents: root, +5, root, +7
    player.setStructureSilent(false);
    player.setSection(Groove::SongSectionId::Chorus);
    player.setBassParams(43, 4);

    std::map<int, int> open;
    int noteOns = 0;
    int noteOffs = 0;
    for (int64_t pos = 0; pos < 192000; pos += 512)  // 2 bars; all note-offs land in range
    {
        juce::MidiBuffer midi;
        player.process(midi, 512, pos);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() && !msg.isNoteOff())
                continue;
            if (msg.getChannel() != 2)
                continue;
            const int n = msg.getNoteNumber();
            if (msg.isNoteOn())
            {
                ++noteOns;
                ++open[n];
            }
            else
            {
                ++noteOffs;
                if (open[n] > 0)
                    --open[n];
            }
        }
    }

    REQUIRE(noteOns > 0);
    REQUIRE(noteOffs == noteOns);  // every note-on matched by a note-off
    for (const auto& kv : open)
        REQUIRE(kv.second == 0);   // nothing left stuck
}

TEST_CASE("silent pattern: bass stops when switching to Silent (no droning root)", "[midi][A1]")
{
    // Regression: when the guitarist stops, the state drops to SILENT and the
    // pattern switches to 0 (Silent). The old code kept playing the harmonic
    // fallback root note during pattern 0 — a drone under hum-level input.
    auto countBassOns = [](PatternPlayer& player, int numSamples, int64_t pos) -> int
    {
        juce::MidiBuffer midi;
        player.process(midi, numSamples, pos);
        int count = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
                ++count;
        }
        return count;
    };

    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(6);
    player.snapBpm(120.0f);
    player.setPatternIndex(1);  // Verse Groove — has authored bass
    player.setStructureSilent(false);
    player.setSection(Groove::SongSectionId::Verse);

    // Bar 1 (beats 0-2): bass plays.
    REQUIRE(countBassOns(player, 96000, 0) > 0);

    // Switch to Silent; render 4 more bars. The change lands at the next bar
    // boundary (beat 4.0), so bars 2-4 must be bass-free.
    player.setPatternIndex(0);
    int bassInBar[4] = {};
    for (int i = 0; i < 4; ++i)
        bassInBar[i] = countBassOns(player, 96000, 96000 + static_cast<int64_t>(i) * 96000);

    REQUIRE(bassInBar[1] == 0);
    REQUIRE(bassInBar[2] == 0);
    REQUIRE(bassInBar[3] == 0);
}

TEST_CASE("frozen transport: internal beat clock keeps patterns musical (no machine-gun)", "[midi][transport]")
{
    // Regression: when the DAW transport is stopped, getTimeInSamples() stays
    // constant. The old jump detection saw a "jump" every block, wiped pending
    // pattern changes (drums stuck on Silent) and anchored the beat clock to a
    // fixed phase — bass/drums machine-gunned at block rate (the harsh constant
    // drone). The player must detect the frozen position and run its own clock.
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(3);
    player.snapBpm(120.0f);
    player.setPatternIndex(1);  // Verse Groove — dense drums + authored bass
    player.setStructureSilent(false);
    player.setSection(Groove::SongSectionId::Verse);

    int drumOns = 0;
    int bassOns = 0;
    int bassOffs = 0;
    std::map<int, int> open;

    // 2 bars (192000 samples) at 120 BPM, host position frozen at 0.
    const int totalBlocks = 192000 / 512;
    for (int b = 0; b < totalBlocks; ++b)
    {
        juce::MidiBuffer midi;
        player.process(midi, 512, 0);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                if (msg.getChannel() == 10)
                    ++drumOns;
                if (msg.getChannel() == 2)
                {
                    ++bassOns;
                    ++open[msg.getNoteNumber()];
                }
            }
            else if (msg.isNoteOff() && msg.getChannel() == 2)
            {
                ++bassOffs;
                if (open[msg.getNoteNumber()] > 0)
                    --open[msg.getNoteNumber()];
            }
        }
    }

    // Two bars of Verse Groove ≈ 18 drum hits — not 375 (block-rate machine-gun),
    // and not 0 (pattern stuck on Silent).
    REQUIRE(drumOns >= 12);
    REQUIRE(drumOns <= 26);
    // Bass: 4 authored notes per 2 bars, every note-on closed.
    REQUIRE(bassOns >= 2);
    REQUIRE(bassOns <= 6);
    REQUIRE(bassOffs == bassOns);
    for (const auto& kv : open)
        REQUIRE(kv.second == 0);
}

// ── Musicality pivot: swing (A2.3) ───────────────────────────────────────────

TEST_CASE("A2.3: swing delays off-8th events", "[midi][A2]")
{
    auto render = [](float swingVal, int seed) -> int
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.setRandomSeed(seed);
        player.snapBpm(120.0f);
        player.setSwing(swingVal);
        player.setSection(Groove::SongSectionId::Unknown);  // no ghost injection
        player.setPatternIndex(1);  // Verse Groove — closed hat at beat 0.5 (off-8th)
        player.setStructureSilent(false);

        juce::MidiBuffer midi;
        player.process(midi, 24000, 0);  // beat 0..1

        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 42)
                return meta.samplePosition;  // first closed hat = the off-8th at 0.5
        }
        return -1;
    };

    const int straight = render(0.0f, 7);
    const int swung = render(0.5f, 7);
    REQUIRE(straight >= 0);
    REQUIRE(swung >= 0);
    // Swing delays the "and" by swing × 1/6 beat = 0.5 × 4000 = 2000 samples at
    // 120 BPM / 48 kHz. Bounded jitter (±2.5σ ≈ ±360 samples) is identical across
    // renders (same seed), so the delta is exactly the swing delay.
    REQUIRE(swung - straight >= 1500);
}

// ── Musicality pivot: section velocity contrast (A3.1) ───────────────────────

TEST_CASE("A3.1: chorus renders louder than verse for the same pattern", "[midi][A3]")
{
    auto maxDrumVel = [](Groove::SongSectionId section, int seed) -> int
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.setRandomSeed(seed);
        player.snapBpm(120.0f);
        player.setSection(section);
        player.setGenrePreset(0);
        player.setPatternIndex(1);
        player.setStructureSilent(false);

        juce::MidiBuffer midi;
        player.process(midi, 96000, 0);

        int maxVel = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 10)
                maxVel = juce::jmax(maxVel, static_cast<int>(msg.getVelocity()));
        }
        return maxVel;
    };

    const int verseMax = maxDrumVel(Groove::SongSectionId::Verse, 5);
    const int chorusMax = maxDrumVel(Groove::SongSectionId::Chorus, 5);
    REQUIRE(verseMax > 0);
    REQUIRE(chorusMax > verseMax + 5);
}

TEST_CASE("PatternPlayer click track: kick on 1, side-stick on 2/3/4, no pattern drums", "[midi][click]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setPatternIndex(1);
    player.setStructureSilent(false);
    player.setClickTrack(true);

    juce::MidiBuffer acc;
    constexpr int block = 512;
    constexpr int64_t barSamples = 96000;  // 1 bar at 120 BPM / 48 kHz
    for (int64_t pos = 0; pos < barSamples; pos += block)
    {
        juce::MidiBuffer midi;
        const int n = static_cast<int>(std::min<int64_t>(block, barSamples - pos));
        player.process(midi, n, pos);
        acc.addEvents(midi, 0, -1, 0);
    }

    int kicks = 0, sticks = 0, snares = 0, hats = 0;
    for (const auto meta : acc)
    {
        const auto msg = meta.getMessage();
        if (!msg.isNoteOn() || msg.getChannel() != 10)
            continue;
        const int note = msg.getNoteNumber();
        if (note == 36) ++kicks;
        else if (note == 37) ++sticks;
        else if (note == 38) ++snares;
        else if (note == 42) ++hats;
    }

    REQUIRE(kicks == 1);
    REQUIRE(sticks == 3);
    REQUIRE(snares == 0);
    REQUIRE(hats == 0);
}
