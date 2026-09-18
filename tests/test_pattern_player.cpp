#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>
#include <utility>
#include <juce_audio_basics/juce_audio_basics.h>
#include "midi/MidiPatternLibrary.h"
#include "midi/PatternPlayer.h"
#include "fixtures/MidiProbe.h"

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

bool patternHasNote(const MidiPatternLibrary& lib, int patternIndex, int note)
{
    const auto& p = lib.getPattern(patternIndex);
    for (const auto& ev : p.drumEvents)
        if (ev.note == note)
            return true;
    return false;
}

std::set<int> patternCellsForNote(const MidiPatternLibrary& lib, int patternIndex, int note)
{
    std::set<int> cells;
    const auto& p = lib.getPattern(patternIndex);
    for (const auto& ev : p.drumEvents)
        if (ev.note == note)
            cells.insert(Groove::grid16Of(ev.beatOffset));
    return cells;
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

TEST_CASE("armBarFill 17 emits toms on beat 4 independent of block size", "[midi][fill]")
{
    auto collectBeat4 = [](int block) -> std::vector<int64_t>
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, block);
        player.snapBpm(120.0f);
        player.setStructureSilent(false);
        player.setPatternIndex(1);
        player.setHumanize(0.0f);   // authored fill 17 fallback (A1 grammar off)
        player.armBarFill(17);

        std::vector<int64_t> samples;
        int64_t pos = 0;
        const int64_t bar = 96000;
        while (pos < bar)
        {
            juce::MidiBuffer midi;
            player.process(midi, block, pos);
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (!msg.isNoteOn() || msg.getChannel() != 10)
                    continue;
                const int note = msg.getNoteNumber();
                if (note == 45 || note == 48 || note == 43 || note == 41 || note == 47)
                    samples.push_back(pos + meta.samplePosition);
            }
            pos += block;
        }
        return samples;
    };

    const auto a = collectBeat4(512);
    const auto b = collectBeat4(256);
    REQUIRE_FALSE(a.empty());
    REQUIRE_FALSE(b.empty());
    const int64_t d = (a.front() > b.front()) ? a.front() - b.front() : b.front() - a.front();
    REQUIRE(d <= 1);
    REQUIRE(a.front() >= 72000 - 2400);  // beat 4 at 120 BPM / 48 kHz = 72000
}

TEST_CASE("pattern change does not auto-crash; armTransitionCrash does", "[midi][fill]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setPatternIndex(1);

    auto hasCrash = [](const juce::MidiBuffer& midi) {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 49)
                return true;
        }
        return false;
    };

    {
        juce::MidiBuffer midi;
        player.process(midi, 96000, 0);
        REQUIRE_FALSE(hasCrash(midi));
    }

    player.setPatternIndex(2);
    {
        juce::MidiBuffer midi;
        player.process(midi, 96000, 96000);
        REQUIRE_FALSE(hasCrash(midi));
    }

    player.armTransitionCrash();
    {
        juce::MidiBuffer midi;
        player.process(midi, 512, 192000);
        REQUIRE(hasCrash(midi));
    }
}

TEST_CASE("armBarFill 19 fromNextBar defers until the next bar downbeat", "[midi][fill]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setPatternIndex(1);
    player.setHumanize(0.0f);   // authored fill 19 fallback (A1 grammar off)

    auto isTom = [](int note) {
        return note == 41 || note == 43 || note == 45 || note == 47 || note == 48;
    };

    constexpr int block = 512;
    constexpr int64_t armSample = 72000;   // beat 3 of bar 0
    constexpr int64_t bar1 = 96000;
    int64_t pos = 0;
    bool armed = false;
    int tomsAfterArmOnBar0 = 0;
    int tomsOnBar1Downbeat = 0;

    while (pos < bar1 + 4800)
    {
        if (!armed && pos + block > armSample)
        {
            player.armBarFill(19, true);
            armed = true;
        }
        juce::MidiBuffer midi;
        player.process(midi, block, pos);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10 || !isTom(msg.getNoteNumber()))
                continue;
            const int64_t abs = pos + meta.samplePosition;
            if (armed && abs >= armSample && abs < bar1)
                ++tomsAfterArmOnBar0;
            if (abs >= bar1 - 2400 && abs <= bar1 + 2400)
                ++tomsOnBar1Downbeat;
        }
        pos += block;
    }

    REQUIRE(armed);
    REQUIRE(tomsAfterArmOnBar0 == 0);
    REQUIRE(tomsOnBar1Downbeat >= 1);
}

TEST_CASE("armBarFill 17 fromNextBar lands beat-4 toms on the next bar", "[midi][fill]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setPatternIndex(1);
    player.setHumanize(0.0f);   // authored fill 17 fallback (A1 grammar off)

    auto isTom = [](int note) {
        return note == 41 || note == 43 || note == 45 || note == 47 || note == 48;
    };

    constexpr int block = 512;
    constexpr int64_t bar1 = 96000;
    constexpr double kSamplesPerBeat = 24000.0;
    int64_t pos = 0;
    bool armed = false;
    int tomsBar0Beat4 = 0;
    int tomsBar1Beat4 = 0;

    while (pos < bar1 * 2)
    {
        if (!armed && pos + block > 95744)
        {
            player.armBarFill(17, true);
            armed = true;
        }
        juce::MidiBuffer midi;
        player.process(midi, block, pos);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10 || !isTom(msg.getNoteNumber()))
                continue;
            const int64_t abs = pos + meta.samplePosition;
            const int hitBar = static_cast<int>(abs / bar1);
            double beatInBar = std::fmod(static_cast<double>(abs) / kSamplesPerBeat, 4.0);
            if (beatInBar < 0.0)
                beatInBar += 4.0;
            if (hitBar == 0 && beatInBar >= 3.0 && beatInBar < 4.0)
                ++tomsBar0Beat4;
            if (hitBar == 1 && beatInBar >= 3.0 && beatInBar < 4.0)
                ++tomsBar1Beat4;
        }
        pos += block;
    }

    REQUIRE(armed);
    REQUIRE(tomsBar0Beat4 == 0);
    REQUIRE(tomsBar1Beat4 >= 1);
}

// ── Phase 37 A1: generated fill grammar ──────────────────────────────────────

TEST_CASE("A1: generated fill is buffer-invariant and deterministic", "[midi][fill][A1]")
{
    MidiPatternLibrary lib;
    auto render = [&](int block)
    {
        PatternPlayer p;
        p.setPatternLibrary(&lib);
        p.prepare(48000.0, block);
        p.setRandomSeed(7);
        p.snapBpm(120.0f);
        p.setStructureSilent(false);
        p.setPatternIndex(1);
        p.setHumanize(1.0f);            // generated path
        p.setFillEnergy(0.7f);
        p.setFillDensity(2.5f);
        p.setSection(Groove::SongSectionId::Verse);
        p.armBarFillAtBeat(19, 0.0);    // full-bar fill at bar 0
        const int64_t span = 2048 * 188; // ~4 bars, divisible by 128/512/2048
        return MidiProbe::render(p, static_cast<int>(span / block), block, 0);
    };
    const auto a = render(128);
    const auto b = render(512);
    const auto c = render(2048);
    const auto d = render(512);
    REQUIRE_FALSE(a.empty());
    REQUIRE(MidiProbe::fingerprint(a) == MidiProbe::fingerprint(b));
    REQUIRE(MidiProbe::fingerprint(a) == MidiProbe::fingerprint(c));
    REQUIRE(MidiProbe::fingerprint(b) == MidiProbe::fingerprint(d));
}

TEST_CASE("A1: generated fill lands the phrase before the next downbeat", "[midi][fill][A1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(11);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setPatternIndex(1);
    player.setHumanize(1.0f);
    player.setFillEnergy(0.7f);      // dense
    player.setFillDensity(2.5f);
    player.setSection(Groove::SongSectionId::Verse);
    player.armBarFillAtBeat(19, 0.0);

    constexpr double kSamplesPerBeat = 24000.0;
    constexpr int block = 512;
    int inWindow = 0, landing = 0;
    int64_t pos = 0;
    while (pos < 96000)
    {
        juce::MidiBuffer midi;
        player.process(midi, block, pos);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10)
                continue;
            const double beat = static_cast<double>(pos + meta.samplePosition) / kSamplesPerBeat;
            if (beat >= 0.0 && beat < 4.0) ++inWindow;
            if (beat >= 3.5 && beat < 4.0) ++landing;
        }
        pos += block;
    }
    REQUIRE(inWindow > 0);
    REQUIRE(landing >= 1);   // the phrase always lands into the downbeat
}

TEST_CASE("A1: humanize=0 falls back to the authored fill", "[midi][fill][A1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setPatternIndex(1);
    player.setHumanize(0.0f);        // authored path
    player.armBarFillAtBeat(17, 0.0);

    constexpr double kSamplesPerBeat = 24000.0;
    constexpr int block = 512;
    bool tomAt3 = false, tomAt325 = false;
    int64_t pos = 0;
    while (pos < 96000)
    {
        juce::MidiBuffer midi;
        player.process(midi, block, pos);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10)
                continue;
            if (msg.getNoteNumber() != 48 && msg.getNoteNumber() != 45)
                continue;
            const double beat = static_cast<double>(pos + meta.samplePosition) / kSamplesPerBeat;
            if (std::abs(beat - 3.0) < 0.1)  tomAt3 = true;
            if (std::abs(beat - 3.25) < 0.1) tomAt325 = true;
        }
        pos += block;
    }
    REQUIRE(tomAt3);
    REQUIRE(tomAt325);
}

// ── Musicality pivot: bass engine (A1) ───────────────────────────────────────

TEST_CASE("A1: authored bass intervals leak, transposed to the live root", "[midi][A1][t5.1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(99);
    player.snapBpm(120.0f);
    player.setPatternIndex(4);  // Chorus Mid — library has authored intervals
    player.setStructureSilent(false);
    player.setBeatGridBassEnabled(true);
    player.setBassParams(40, 2);  // E2 live root, beats 1/3

    juce::MidiBuffer midi;
    player.process(midi, 192000, 0);  // 2 bars at 120 BPM (Chorus Mid is a 2-bar pattern)

    std::set<int> bassNotes;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 2)
            bassNotes.insert(msg.getNoteNumber());
    }
    // T5.1: authored bass lines play, transposed to the live root
    // (pattern 4 is C2/F2/C2/G2 at kBassRoot=36 → E2/A2/E2/B2 at root 40).
    REQUIRE(bassNotes.count(40) > 0);  // root (kBassRoot + 0 → 40)
    REQUIRE(bassNotes.count(45) > 0);  // library +5, transposed
    REQUIRE(bassNotes.count(47) > 0);  // library +7, transposed
}

TEST_CASE("T5.1: Play verse pattern 22 leaks authored +5 at a live E root", "[midi][A1][t5.1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(3);
    player.snapBpm(120.0f);
    player.setPatternIndex(22);  // Rock Backbeat — last event is kBassRoot + 5
    player.setSection(Groove::SongSectionId::Verse);
    player.setStructureSilent(false);
    player.setBeatGridBassEnabled(true);
    player.setBassParams(40, 2);  // E2

    juce::MidiBuffer midi;
    player.process(midi, 192000, 0);

    std::set<int> bassNotes;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 2)
            bassNotes.insert(msg.getNoteNumber());
    }
    REQUIRE(bassNotes.count(40) > 0);  // authored root → E
    REQUIRE(bassNotes.count(45) > 0);  // authored +5 → E+5
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
    player.setPatternIndex(4);
    player.setStructureSilent(false);
    player.setBeatGridBassEnabled(true);
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
    player.setBeatGridBassEnabled(false);
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

TEST_CASE("stopped-to-rolling: click stays on the host bar grid (no seek dump)", "[midi][transport][click]")
{
    // DAW Record starts the playhead after a stopped (frozen) run. The free-run
    // sampleCounter is unrelated to host 0; treating that as a seek used to dump
    // click state and restart count-in. After the handoff, one bar of click
    // must still be kick+3 sticks — not 0, not machine-gun.
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setStructureSilent(true);
    player.setClickTrack(true);

    constexpr int block = 512;
    constexpr int barSamples = 96000;  // 1 bar at 120 BPM / 48 kHz
    const int blocksPerBar = barSamples / block;

    auto countClickOns = [](const juce::MidiBuffer& midi) -> int
    {
        int n = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 10)
                ++n;
        }
        return n;
    };

    int frozenClicks = 0;
    for (int b = 0; b < blocksPerBar; ++b)
    {
        juce::MidiBuffer midi;
        player.process(midi, block, 0, false);
        frozenClicks += countClickOns(midi);
    }
    REQUIRE(frozenClicks >= 3);
    REQUIRE(frozenClicks <= 5);

    // Count-in schedules from previewResolvedHostSample *before* process().
    // That preview must already be on the host grid (0), not the free-run
    // counter (~1 bar in), or WaitBar/CountIn last the wrong number of beats.
    REQUIRE(player.previewResolvedHostSample(0, block, true) == 0);

    int rollingClicks = 0;
    int64_t pos = 0;
    for (int b = 0; b < blocksPerBar; ++b)
    {
        juce::MidiBuffer midi;
        player.process(midi, block, pos, true);
        rollingClicks += countClickOns(midi);
        pos += block;
    }
    REQUIRE(rollingClicks >= 3);
    REQUIRE(rollingClicks <= 5);
}

TEST_CASE("T2.2 consumeTransportJumped is set on a host seek and clears on read", "[midi][transport][phase2][t2.2]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setPatternIndex(1);
    player.setStructureSilent(false);

    juce::MidiBuffer midi;
    player.process(midi, 512, 0, true);
    REQUIRE_FALSE(player.consumeTransportJumped());

    midi.clear();
    player.process(midi, 512, 512, true);
    REQUIRE_FALSE(player.consumeTransportJumped());

    midi.clear();
    player.process(midi, 512, 512 * 40, true);  // jump well past ±2-block slack
    REQUIRE(player.consumeTransportJumped());
    REQUIRE_FALSE(player.consumeTransportJumped());
}

TEST_CASE("T8.2 pending learned overrun drops the newest note", "[midi][T8.2]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setPatternIndex(0);
    player.setBeatGridBassEnabled(false);
    player.setStructureSilent(false);

    for (int i = 0; i < 8; ++i)
        player.triggerLearnedBassNote(36 + i, 0.6f, i, 1000);
    player.triggerLearnedBassNote(50, 0.9f, 8, 1000);

    juce::MidiBuffer midi;
    player.process(midi, 512, 0, true);

    std::set<int> notes;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getChannel() == 2)
            notes.insert(m.getNoteNumber());
    }
    REQUIRE(notes.count(50) == 0);
    for (int i = 0; i < 8; ++i)
        REQUIRE(notes.count(36 + i) == 1);
}

TEST_CASE("A1.2: armed bass lead-in fires a pickup on the bar's last beat", "[midi][A1]")
{
    // Section hand-off: the processor arms a lead-in on the last bar of a section;
    // the player emits a bass pickup on the "and" of the last beat, then disarms.
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setPatternIndex(1);  // Verse Groove — has authored bass
    player.setStructureSilent(false);
    player.setSection(Groove::SongSectionId::Verse);
    player.setBassParams(40, 2);  // E2 root → pickup is root − 5 = 35 (B1)
    player.armBassLeadIn();

    // One bar = 96000 samples at 120 BPM. pickupBeat = 3.5 → offset 84000.
    juce::MidiBuffer midi;
    player.process(midi, 96000, 0);

    bool sawPickup = false;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 2
            && msg.getNoteNumber() == 35 && msg.getTimeStamp() == 84000)
            sawPickup = true;
    }
    REQUIRE(sawPickup);
}

TEST_CASE("A1.2: bass lead-in re-arms and fires on each successive bar", "[midi][A1]")
{
    // A section's last bar repeats for every section, so the pickup must re-fire
    // on each new last bar (not be a one-shot forever after the first).
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setPatternIndex(1);
    player.setStructureSilent(false);
    player.setSection(Groove::SongSectionId::Verse);
    player.setBassParams(40, 2);

    auto pickupThisBar = [&](int64_t barBase) -> bool
    {
        player.armBassLeadIn();
        juce::MidiBuffer midi;
        player.process(midi, 96000, barBase);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2
                && msg.getNoteNumber() == 35
                && msg.getTimeStamp() == 84000)
                return true;
        }
        return false;
    };

    REQUIRE(pickupThisBar(0));          // bar 0 (samples 0..96000) → pickup at 84000
    REQUIRE(pickupThisBar(96000));      // bar 1 (samples 96000..192000) → pickup at 84000
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

TEST_CASE("A3.1: guitarist-energy dynamic scales the kit loudness", "[midi][A3]")
{
    // setGuitarEnergy feeds a multiplier into sectionVelMul, so a hotter signal
    // must render a louder kit. Fixed seed keeps the humanisation deterministic.
    auto sumDrumVel = [](float energy) -> int
    {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.setRandomSeed(5);
        player.snapBpm(120.0f);
        player.setSection(Groove::SongSectionId::Chorus);
        player.setGenrePreset(0);
        player.setPatternIndex(1);
        player.setStructureSilent(false);
        player.setGuitarEnergy(energy);

        juce::MidiBuffer midi;
        player.process(midi, 96000, 0);

        int sum = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 10)
                sum += static_cast<int>(msg.getVelocity());
        }
        return sum;
    };

    const int quiet = sumDrumVel(0.85f);  // clamped lower bound (T3.2)
    const int loud  = sumDrumVel(1.20f);  // clamped upper bound
    REQUIRE(quiet > 0);
    REQUIRE(loud > quiet + 4);
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

// ══════════════════════════════════════════════════════════════════════════
// Tier-0 ornamentation (computeOrnamentation) — determinism + safety invariants
// ══════════════════════════════════════════════════════════════════════════

TEST_CASE("Tier-0 ornamentation: Silent pattern never ornaments", "[midi][ornament]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);

    const Groove::SongSectionId sections[] = {
        Groove::SongSectionId::Verse,     Groove::SongSectionId::Chorus,
        Groove::SongSectionId::Breakdown, Groove::SongSectionId::Solo,
        Groove::SongSectionId::Intro,     Groove::SongSectionId::Outro,
    };
    for (const auto section : sections)
    {
        player.setSection(section);
        for (int64_t bar = 0; bar < 200; ++bar)
        {
            const auto o = player.computeOrnamentation(bar, 0);
            REQUIRE(!o.openHat);
            REQUIRE(!o.rideSwitch);
            REQUIRE(!o.extraGhost);
            REQUIRE(!o.dropKick);
            REQUIRE(!o.microFill);
        }
    }
}

TEST_CASE("Tier-0 ornamentation is deterministic per (bar, pattern)", "[midi][ornament]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setSection(Groove::SongSectionId::Verse);

    for (int64_t bar = 0; bar < 500; ++bar)
    {
        const auto a = player.computeOrnamentation(bar, 1);
        const auto b = player.computeOrnamentation(bar, 1);
        REQUIRE(a.openHat == b.openHat);
        REQUIRE(a.openHatCell == b.openHatCell);
        REQUIRE(a.rideSwitch == b.rideSwitch);
        REQUIRE(a.extraGhost == b.extraGhost);
        REQUIRE(a.extraGhostCell == b.extraGhostCell);
        REQUIRE(a.dropKick == b.dropKick);
        REQUIRE(a.dropKickCell == b.dropKickCell);
        REQUIRE(a.microFill == b.microFill);
    }
}

TEST_CASE("Tier-0 ornamentation never fires on a voice the pattern lacks", "[midi][ornament]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);

    // Pattern 4 (Chorus Mid) has open hats + ride bell but NO closed hat (42).
    REQUIRE(!patternHasNote(lib, 4, 42));
    // Pattern 1 (Verse Groove) has closed hats + ride bell.
    REQUIRE(patternHasNote(lib, 1, 42));
    REQUIRE(patternHasNote(lib, 1, 53));
    // Pattern 22 (Rock Backbeat) has closed hats and no ride/bell.
    REQUIRE(patternHasNote(lib, 22, 42));
    REQUIRE(!patternHasNote(lib, 22, 51));
    REQUIRE(!patternHasNote(lib, 22, 53));

    const std::set<int> closedHatCells1 = patternCellsForNote(lib, 1, 42);
    REQUIRE(!closedHatCells1.empty());

    for (int64_t bar = 0; bar < 5000; ++bar)
    {
        // No closed hat -> openHat and rideSwitch are impossible.
        player.setSection(Groove::SongSectionId::Verse);
        auto o = player.computeOrnamentation(bar, 4);
        REQUIRE(!o.openHat);
        REQUIRE(!o.rideSwitch);

        // Pattern 1 has a ride bell, so rideSwitch must be gated off; openHat
        // (when it fires) must land on a cell that really has a closed hat.
        o = player.computeOrnamentation(bar, 1);
        REQUIRE(!o.rideSwitch);
        if (o.openHat)
            REQUIRE(closedHatCells1.count(o.openHatCell) == 1);

        // Pattern 22 has no ride/bell, so rideSwitch may fire in chorus/solo.
        player.setSection(Groove::SongSectionId::Chorus);
        o = player.computeOrnamentation(bar, 22);
        REQUIRE(!(o.rideSwitch && (patternHasNote(lib, 22, 51) || patternHasNote(lib, 22, 53))));
    }
}

TEST_CASE("Tier-0 ornamentation respects section and phrase gating", "[midi][ornament]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);

    for (int64_t bar = 0; bar < 3000; ++bar)
    {
        player.setSection(Groove::SongSectionId::Verse);
        auto o = player.computeOrnamentation(bar, 1);
        REQUIRE(!o.rideSwitch);   // ride switch is chorus/solo only
        REQUIRE(!o.dropKick);     // drop kick is breakdown/outro only
        if (bar % 4 != 3) REQUIRE(!o.microFill);

        player.setSection(Groove::SongSectionId::Chorus);
        o = player.computeOrnamentation(bar, 1);
        REQUIRE(!o.extraGhost);   // ghosts are verse/breakdown only
        REQUIRE(!o.dropKick);

        player.setSection(Groove::SongSectionId::Solo);
        o = player.computeOrnamentation(bar, 1);
        REQUIRE(!o.extraGhost);
        REQUIRE(!o.dropKick);

        player.setSection(Groove::SongSectionId::Breakdown);
        o = player.computeOrnamentation(bar, 1);
        REQUIRE(!o.rideSwitch);
    }
}

TEST_CASE("Tier-0 dropKick never removes the downbeat or beat-3", "[midi][ornament]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setSection(Groove::SongSectionId::Breakdown);

    // Pattern 25 (Punk D-Beat) has off-8th kicks at grid16 cells {2, 6, 10, 14},
    // so the ornament has eligible non-downbeat/beat-3 cells to drop.
    bool fired = false;
    for (int64_t bar = 0; bar < 20000; ++bar)
    {
        const auto o = player.computeOrnamentation(bar, 25);
        if (o.dropKick)
        {
            fired = true;
            REQUIRE(o.dropKickCell != 0);
            REQUIRE(o.dropKickCell != 8);
        }
    }
    REQUIRE(fired);  // 12% of 20000 bars — vanishingly unlikely to never fire
}

TEST_CASE("Tier-0 microFill only on phrase-end bars and never on fill patterns", "[midi][ornament]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setSection(Groove::SongSectionId::Verse);

    for (int64_t bar = 0; bar < 100; ++bar)
    {
        const auto o = player.computeOrnamentation(bar, 1);
        if (bar % 4 != 3)
            REQUIRE(!o.microFill);
    }

    // Fill patterns 17/18/19 never micro-fill, even on phrase-end bars.
    for (const int p : { 17, 18, 19 })
        for (int64_t bar = 3; bar < 1000; bar += 4)
            REQUIRE(!player.computeOrnamentation(bar, p).microFill);
}

TEST_CASE("Tier-0 ornamentation activates across bars (not a static no-op)", "[midi][ornament]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setSection(Groove::SongSectionId::Verse);

    bool anyOpenHat = false, anyGhost = false, anyMicroFill = false;
    for (int64_t bar = 1; bar < 4000; ++bar)
    {
        const auto o = player.computeOrnamentation(bar, 1);
        anyOpenHat  |= o.openHat;
        anyGhost    |= o.extraGhost;
        anyMicroFill |= o.microFill;
    }

    // Verse + pattern 1: openHat 8%, extraGhost 15%, microFill 12% (phrase-end
    // bars). All should fire at least once across 4000 bars.
    REQUIRE(anyOpenHat);
    REQUIRE(anyGhost);
    REQUIRE(anyMicroFill);
}

TEST_CASE("T4.3: humanize=0 disables every ornament", "[midi][ornament][T4.3]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setHumanize(0.0f);

    const Groove::SongSectionId sections[] = {
        Groove::SongSectionId::Verse,     Groove::SongSectionId::Chorus,
        Groove::SongSectionId::Breakdown, Groove::SongSectionId::Solo,
        Groove::SongSectionId::Outro,
    };
    for (const auto section : sections)
    {
        player.setSection(section);
        for (int64_t bar = 0; bar < 400; ++bar)
        {
            const auto o = player.computeOrnamentation(bar, 1);
            REQUIRE(!o.openHat);
            REQUIRE(!o.rideSwitch);
            REQUIRE(!o.extraGhost);
            REQUIRE(!o.dropKick);
            REQUIRE(!o.microFill);
            const auto p22 = player.computeOrnamentation(bar, 22);
            REQUIRE(!p22.rideSwitch);
        }
    }
}

TEST_CASE("T4.3: ornaments fire near their stated rate and never share a cell", "[midi][ornament][T4.3]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setHumanize(1.0f);
    player.setGenrePreset(0);  // Rock: ghostDensity 0.35 → injected ghost is cell 9 only

    constexpr int kBars = 400;
    int openHat = 0, extraGhost = 0, dropKick = 0, microFill = 0, rideSwitch = 0;
    player.setSection(Groove::SongSectionId::Verse);
    for (int64_t bar = 0; bar < kBars; ++bar)
    {
        const auto o = player.computeOrnamentation(bar, 1);
        if (o.openHat) ++openHat;
        if (o.extraGhost) ++extraGhost;
        if (o.microFill) ++microFill;
        if (o.openHat && o.extraGhost)
            REQUIRE(o.openHatCell != o.extraGhostCell);
        if (o.extraGhost)
            REQUIRE(o.extraGhostCell != 9);  // injected-ghost cell
    }

    player.setSection(Groove::SongSectionId::Breakdown);
    for (int64_t bar = 0; bar < kBars; ++bar)
    {
        const auto o = player.computeOrnamentation(bar, 25);
        if (o.dropKick) ++dropKick;
        if (o.openHat && o.dropKick)
            REQUIRE(o.openHatCell != o.dropKickCell);
        if (o.extraGhost && o.dropKick)
            REQUIRE(o.extraGhostCell != o.dropKickCell);
    }

    player.setSection(Groove::SongSectionId::Chorus);
    for (int64_t bar = 0; bar < kBars; ++bar)
        if (player.computeOrnamentation(bar, 22).rideSwitch)
            ++rideSwitch;

    // Stated rates at humanize=1: openHat 8%, extraGhost 15%, dropKick 12%,
    // microFill 12% of phrase-end bars (≈3% of all bars), rideSwitch 0.
    REQUIRE(openHat >= 8);
    REQUIRE(openHat <= 80);
    REQUIRE(extraGhost >= 15);
    REQUIRE(extraGhost <= 120);
    REQUIRE(dropKick >= 12);
    REQUIRE(dropKick <= 100);
    REQUIRE(microFill >= 1);
    REQUIRE(rideSwitch == 0);
}

TEST_CASE("T4.3: humanize=0 MIDI matches a second render and differs from humanize=1", "[midi][ornament][T4.3]")
{
    MidiPatternLibrary lib;
    auto prepare = [&](PatternPlayer& player, float humanize)
    {
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.setRandomSeed(0xC0FFEE);
        player.snapBpm(120.0f);
        player.setPatternIndex(1);
        player.setSection(Groove::SongSectionId::Verse);
        player.setGenrePreset(0);
        player.setStructureSilent(false);
        player.setSwing(0.0f);
        player.setHumanize(humanize);
        player.setBeatGridBassEnabled(false);
    };

    PatternPlayer a, b, c;
    prepare(a, 0.0f);
    prepare(b, 0.0f);
    prepare(c, 1.0f);

    constexpr int kBlock = 512;
    constexpr int kBlocks = 188 * 32;  // 32 bars at 120 BPM / 48 kHz
    const auto fa = MidiProbe::fingerprint(MidiProbe::render(a, kBlocks, kBlock, 0));
    const auto fb = MidiProbe::fingerprint(MidiProbe::render(b, kBlocks, kBlock, 0));
    const auto fc = MidiProbe::fingerprint(MidiProbe::render(c, kBlocks, kBlock, 0));
    REQUIRE(fa == fb);
    REQUIRE(fa != fc);
}

// ── B: additive Tier-0 ornaments (extra tom / kick double / snare flam /
//       ride-bell accent). New notes, not modifications of authored events. ──

TEST_CASE("B: additive ornaments fire, never collide, and lead correctly",
          "[midi][ornament][B]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setHumanize(1.0f);
    player.setGenrePreset(0);
    player.setSection(Groove::SongSectionId::Verse);

    auto allCells = [&](int pat)
    {
        std::set<int> cells;
        for (const auto& ev : lib.getPattern(pat).drumEvents)
            cells.insert(Groove::grid16Of(ev.beatOffset));
        return cells;
    };

    constexpr int kBars = 400;
    int extraTom = 0, kickDouble = 0, snareFlam = 0, rideBell = 0;

    for (int pat : {1, 2, 3, 7, 20})
    {
        const auto occupied   = allCells(pat);
        const auto kickCells  = patternCellsForNote(lib, pat, 36);
        const auto snareCells = patternCellsForNote(lib, pat, 38);
        for (int64_t bar = 0; bar < kBars; ++bar)
        {
            const auto o = player.computeOrnamentation(bar, pat);
            if (o.extraTom)
            {
                ++extraTom;
                REQUIRE(!occupied.count(o.extraTomCell));   // no clash with any voice
            }
            if (o.kickDouble)
            {
                ++kickDouble;
                REQUIRE(!occupied.count(o.kickDoubleCell));
                const int lead = (o.kickDoubleCell + 1) % 16;
                REQUIRE((lead == 0 || lead == 8));          // leads a beat-1/beat-3 kick
                REQUIRE(kickCells.count(lead));
            }
            if (o.snareFlam)
            {
                ++snareFlam;
                REQUIRE(snareCells.count(o.snareFlamCell)); // grace before an authored backbeat
            }
            if (o.rideBellAccent)
            {
                ++rideBell;
                REQUIRE((o.rideBellCell == 0 || o.rideBellCell == 8));
                REQUIRE(!patternCellsForNote(lib, pat, 53).count(o.rideBellCell));
            }
        }
    }

    // Reachability: each addition actually fires somewhere in the sweep.
    REQUIRE(extraTom >= 1);
    REQUIRE(kickDouble >= 1);
    REQUIRE(snareFlam >= 1);
    REQUIRE(rideBell >= 1);

    // Ceiling: additions stay "a little variety", not a new groove (~<=12%/bar).
    REQUIRE(extraTom <= 2 * kBars);
    REQUIRE(kickDouble <= 2 * kBars);
    REQUIRE(snareFlam <= 2 * kBars);
}

TEST_CASE("B: additive ornaments are disabled at humanize=0", "[midi][ornament][B]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setHumanize(0.0f);

    const Groove::SongSectionId sections[] = {
        Groove::SongSectionId::Verse,     Groove::SongSectionId::Chorus,
        Groove::SongSectionId::Breakdown, Groove::SongSectionId::Solo,
        Groove::SongSectionId::Outro,
    };
    for (const auto section : sections)
    {
        player.setSection(section);
        for (int64_t bar = 0; bar < 400; ++bar)
        {
            for (int pat : {1, 3, 22, 25})
            {
                const auto o = player.computeOrnamentation(bar, pat);
                REQUIRE(!o.extraTom);
                REQUIRE(!o.kickDouble);
                REQUIRE(!o.snareFlam);
                REQUIRE(!o.rideBellAccent);
            }
        }
    }
}

TEST_CASE("B: additive ornaments are buffer-size invariant and deterministic",
          "[midi][ornament][B]")
{
    MidiPatternLibrary lib;
    PatternPlayer a, b, c, d, e;
    auto prep = [&](PatternPlayer& p, int block)
    {
        p.setPatternLibrary(&lib);
        p.prepare(48000.0, block);
        p.setRandomSeed(0xB0B0);
        p.snapBpm(120.0f);
        p.setPatternIndex(1);   // Verse Groove: all four additions are possible
        p.setSection(Groove::SongSectionId::Verse);
        p.setGenrePreset(0);
        p.setStructureSilent(false);
        p.setSwing(0.0f);
        p.setHumanize(1.0f);
        p.setBeatGridBassEnabled(false);
    };
    prep(a, 128);
    prep(b, 512);
    prep(c, 2048);
    prep(d, 512);   // identical config to b: determinism check
    prep(e, 4096);  // large block: flam grace + authored backbeat land in ONE callback

    constexpr int64_t kSpan = 2048 * 188;  // ~4 bars, divisible by 128/512/2048/4096
    const auto ea = MidiProbe::render(a, static_cast<int>(kSpan / 128),  128,  0);
    const auto eb = MidiProbe::render(b, static_cast<int>(kSpan / 512),  512,  0);
    const auto ec = MidiProbe::render(c, static_cast<int>(kSpan / 2048), 2048, 0);
    const auto ed = MidiProbe::render(d, static_cast<int>(kSpan / 512),  512,  0);
    const auto ee = MidiProbe::render(e, static_cast<int>(kSpan / 4096), 4096, 0);

    REQUIRE_FALSE(ea.empty());
    REQUIRE(MidiProbe::fingerprint(ea) == MidiProbe::fingerprint(eb));
    REQUIRE(MidiProbe::fingerprint(ea) == MidiProbe::fingerprint(ec));
    REQUIRE(MidiProbe::fingerprint(eb) == MidiProbe::fingerprint(ed));
    // Same-note retrigger ordering must not depend on the host block size: the
    // flam grace must close when the authored backbeat arrives, never the reverse.
    REQUIRE(MidiProbe::fingerprint(ea) == MidiProbe::fingerprint(ee));
}

TEST_CASE("B: additive ornaments never fire on Silent and scale with humanize",
          "[midi][ornament][B]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setSection(Groove::SongSectionId::Verse);
    player.setGenrePreset(0);

    // Pattern 0 (Silent) must never receive an additive ornament at any amount.
    player.setHumanize(1.0f);
    for (int64_t bar = 0; bar < 400; ++bar)
    {
        const auto o = player.computeOrnamentation(bar, 0);
        REQUIRE(!o.extraTom);
        REQUIRE(!o.kickDouble);
        REQUIRE(!o.snareFlam);
        REQUIRE(!o.rideBellAccent);
    }

    // Rates scale with humanizeAmount: the pct=6 firing set is a strict subset
    // of pct=12 for the same (bar, salt), so the half-rate count can never
    // exceed the full-rate count, and the full rate must be non-zero.
    int full = 0, half = 0;
    for (int pat : {1, 2, 3, 7, 20})
        for (int64_t bar = 0; bar < 400; ++bar)
        {
            player.setHumanize(1.0f);
            if (player.computeOrnamentation(bar, pat).extraTom) ++full;
            player.setHumanize(0.5f);
            if (player.computeOrnamentation(bar, pat).extraTom) ++half;
        }
    REQUIRE(full > 0);
    REQUIRE(half <= full);
}

TEST_CASE("B: additive ornaments fire at the shipped default humanize (0.35)",
          "[midi][ornament][B]")
{
    // The APVTS default is 0.35, not 1.0, and it is NOT mode-gated: Record and
    // Play both go through the same PatternPlayer emission. This locks that the
    // additions are reachable at the real shipped setting (and in the Record-mode
    // section mapping: LOUD -> Chorus, else Verse).
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.setHumanize(0.35f);

    int extraTom = 0, kickDouble = 0, snareFlam = 0, rideBell = 0;

    // VERSE (Record SOFT/SILENT mapping) — all four are permitted here.
    player.setSection(Groove::SongSectionId::Verse);
    for (int pat : {1, 2, 3, 7, 10, 20})
        for (int64_t bar = 0; bar < 2000; ++bar)
        {
            const auto o = player.computeOrnamentation(bar, pat);
            extraTom += o.extraTom;
            kickDouble += o.kickDouble;
            snareFlam += o.snareFlam;
            rideBell += o.rideBellAccent;
        }

    REQUIRE(extraTom >= 1);
    REQUIRE(kickDouble >= 1);
    REQUIRE(snareFlam >= 1);
    REQUIRE(rideBell >= 1);

    // CHORUS (Record LOUD mapping): extraTom is intentionally section-gated off,
    // but the three non-section-gated additions still fire.
    player.setSection(Groove::SongSectionId::Chorus);
    int chorusTom = 0, chorusKick = 0, chorusFlam = 0, chorusBell = 0;
    for (int pat : {4, 5, 14, 21})
        for (int64_t bar = 0; bar < 2000; ++bar)
        {
            const auto o = player.computeOrnamentation(bar, pat);
            chorusTom += o.extraTom;
            chorusKick += o.kickDouble;
            chorusFlam += o.snareFlam;
            chorusBell += o.rideBellAccent;
        }
    REQUIRE(chorusTom == 0);      // by design
    REQUIRE(chorusKick >= 1);
    REQUIRE(chorusFlam >= 1);
    REQUIRE(chorusBell >= 1);
}

TEST_CASE("T5.3: a ringing mirror owns the bass; the grid resumes after its gate", "[midi][bass][t5.3]")
{
    // Mirror-primary contract. Live-mirror notes are 0.85 beat long, so a pick
    // on the "and of 4" rings past the next downbeat. The monophonic bass voice
    // belongs to the mirror for that whole gate — the grid must not double it —
    // and the grid fallback returns at the first grid hit after the gate.
    // (Supersedes the pre-mirror-primary rule, which retriggered the grid at the
    // downbeat and made Play sound like a root/harmony line, not a mirror.)
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.setRandomSeed(1);
    player.snapBpm(120.0f);
    player.setPatternIndex(11);  // Intro Build — empty bassEvents, harmonic fallback
    player.setSection(Groove::SongSectionId::Verse);
    player.setStructureSilent(false);
    player.setBeatGridBassEnabled(true);
    player.setBassParams(40, 2);  // E2, beats 1 and 3
    player.setHumanize(0.0f);
    player.setSwing(0.0f);

    constexpr double kSr = 48000.0;
    constexpr float kBpm = 120.0f;
    const double spb = 60.0 / static_cast<double>(kBpm) * kSr;
    const int block = 512;
    const int duration = juce::jmax(1, static_cast<int>(0.85 * spb));
    const int pickup = static_cast<int>(std::lround(3.5 * spb));  // and of 4
    const int downbeat = static_cast<int>(std::lround(4.0 * spb)); // next bar beat 1
    const int nextGrid = static_cast<int>(std::lround(6.0 * spb)); // bar 2 beat 3
    const int64_t span = static_cast<int64_t>(8.0 * spb);

    int pickupOns = 0;
    int downbeatOns = 0;
    int nextGridOns = 0;
    const int64_t win = static_cast<int64_t>(0.080 * kSr);
    for (int64_t pos = 0; pos < span; pos += block)
    {
        if (pos <= pickup && pos + block > pickup)
            player.triggerLearnedBassNote(40, 0.58f, static_cast<int>(pickup - pos), duration);
        juce::MidiBuffer midi;
        player.process(midi, block, pos, true);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 2 || msg.getVelocity() <= 0)
                continue;
            const int64_t abs = pos + meta.samplePosition;
            if (std::llabs(abs - pickup) <= win)
                ++pickupOns;
            if (std::llabs(abs - downbeat) <= win)
                ++downbeatOns;
            if (std::llabs(abs - nextGrid) <= win)
                ++nextGridOns;
        }
    }
    REQUIRE(pickupOns >= 1);      // the mirrored pickup sounds
    REQUIRE(downbeatOns == 0);    // no grid doubled inside the mirror's gate
    REQUIRE(nextGridOns >= 1);    // the fallback resumes once the gate expires
}

TEST_CASE("T6.1: GrooveCommit alignToBeat applies at the next beat, not the next bar",
          "[midi][t6.1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setPatternIndex(1);

    constexpr int block = 512;
    constexpr int64_t beat = 24000;  // 1 beat at 120 BPM / 48 kHz
    int64_t pos = 0;
    juce::MidiBuffer midi;
    player.process(midi, block, pos, true);
    pos += block;
    REQUIRE(player.getActivePatternIndex() == 1);

    while (pos + block <= beat / 2)
    {
        midi.clear();
        player.process(midi, block, pos, true);
        pos += block;
    }
    REQUIRE(player.getActivePatternIndex() == 1);

    PatternPlayer::GrooveCommit commit{};
    commit.patternIndex = 4;
    commit.alignToBeat = true;
    player.queueGrooveCommit(commit);

    bool applied = false;
    const int64_t bar = beat * 4;
    while (pos < beat * 2)
    {
        midi.clear();
        player.process(midi, block, pos, true);
        if (player.getActivePatternIndex() == 4)
            applied = true;
        pos += block;
    }
    REQUIRE(applied);
    REQUIRE(pos < bar);
}

// A pattern change at a bar (or beat) line must not emit the boundary hit twice:
// the outgoing pattern's downbeat and the incoming pattern's downbeat are the
// SAME musical event, and two note-ons on the same note at the same sample make
// a drum sampler choke or flam. Found while auditing the Play-mode drum stream
// (measured ~10 % of drum note-ons duplicated on a real DI).
TEST_CASE("pattern change at a bar line does not double-emit the downbeat",
          "[midi][duplicate]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 256);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setHumanize(0.0f);
    player.setSwing(0.0f);
    player.setRandomSeed(0);
    player.setPatternIndex(1);            // Verse Groove — 2-bar, kick on 0/2/4/6

    constexpr int block = 256;
    constexpr int64_t bar = 96000;        // 4 beats @ 120 BPM / 48 kHz
    std::map<std::pair<int64_t, int>, int> hits;
    int64_t pos = 0;
    juce::MidiBuffer midi;

    auto runBlocks = [&](int n)
    {
        for (int i = 0; i < n; ++i)
        {
            midi.clear();
            player.process(midi, block, pos, true);
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn() && m.getChannel() == 10)
                    ++hits[{ pos + meta.samplePosition, m.getNoteNumber() }];
            }
            pos += block;
        }
    };

    runBlocks(static_cast<int>(bar / block) - 1);   // stop one block before bar 2
    PatternPlayer::GrooveCommit commit{};
    commit.patternIndex = 4;                    // Chorus Mid — kick on the downbeat
    commit.alignToBeat = false;                 // applies at the next bar line
    player.queueGrooveCommit(commit);
    runBlocks(static_cast<int>(bar / block) + 3);

    int duplicates = 0;
    for (const auto& kv : hits)
        if (kv.second > 1)
            ++duplicates;
    REQUIRE(duplicates == 0);
}

TEST_CASE("T7.2 armBarFillAtBeat keeps fill 17 on a late-latched last bar", "[midi][fill][t7.2]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 2048);
    player.snapBpm(120.0f);
    player.setStructureSilent(false);
    player.setPatternIndex(1);
    player.setHumanize(0.0f);
    player.setRandomSeed(7);

    auto isTom = [](int note) {
        return note == 41 || note == 43 || note == 45 || note == 47 || note == 48;
    };

    constexpr int block = 2048;
    constexpr int64_t bar = 96000;
    constexpr double kSamplesPerBeat = 24000.0;
    int tomsBar0Beat4 = 0;
    int tomsBar1Beat4 = 0;
    int64_t pos = 0;
    bool armed = false;

    while (pos < bar * 2)
    {
        if (!armed && pos >= 2048)
        {
            player.armBarFillAtBeat(17, 0.0);
            REQUIRE(player.getBarFillStartBeat() == 0.0);
            armed = true;
        }
        juce::MidiBuffer midi;
        player.process(midi, block, pos);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10 || !isTom(msg.getNoteNumber()))
                continue;
            const int64_t abs = pos + meta.samplePosition;
            const int hitBar = static_cast<int>(abs / bar);
            double beatInBar = std::fmod(static_cast<double>(abs) / kSamplesPerBeat, 4.0);
            if (beatInBar < 0.0)
                beatInBar += 4.0;
            if (hitBar == 0 && beatInBar >= 3.0 && beatInBar < 4.0)
                ++tomsBar0Beat4;
            if (hitBar == 1 && beatInBar >= 3.0 && beatInBar < 4.0)
                ++tomsBar1Beat4;
        }
        pos += block;
    }

    REQUIRE(armed);
    REQUIRE(tomsBar0Beat4 >= 1);
    REQUIRE(tomsBar1Beat4 == 0);
}

TEST_CASE("T7.3 fill 18 replaces groove kicks at 3.75 and inherits swing/section vel", "[midi][fill][t7.3]")
{
    auto isTom = [](int note) {
        return note == 41 || note == 43 || note == 45 || note == 47 || note == 48;
    };

    auto renderFill18 = [&](float swing, Groove::SongSectionId section) {
        MidiPatternLibrary lib;
        PatternPlayer player;
        player.setPatternLibrary(&lib);
        player.prepare(48000.0, 512);
        player.setRandomSeed(11);
        player.snapBpm(120.0f);
        player.setStructureSilent(false);
        player.setPatternIndex(21);
        player.setHumanize(0.0f);
        player.setSwing(swing);
        player.setSection(section);
        player.armBarFill(18, false);

        struct Hit { int64_t sample; int note; int vel; };
        std::vector<Hit> kicks;
        std::vector<Hit> toms;
        int64_t pos = 0;
        const int block = 512;
        const int64_t bar = 96000;
        while (pos < bar)
        {
            juce::MidiBuffer midi;
            player.process(midi, block, pos);
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (!msg.isNoteOn() || msg.getChannel() != 10)
                    continue;
                Hit h{ pos + meta.samplePosition, msg.getNoteNumber(), msg.getVelocity() };
                if (h.note == 36)
                    kicks.push_back(h);
                if (isTom(h.note))
                    toms.push_back(h);
            }
            pos += block;
        }
        return std::make_pair(kicks, toms);
    };

    {
        const auto kicks = renderFill18(0.0f, Groove::SongSectionId::Chorus).first;
        int near375 = 0;
        constexpr int64_t target = 90000;  // beat 3.75 at 120 BPM / 48 kHz
        constexpr int64_t window = 240;    // 5 ms
        for (const auto& k : kicks)
            if (std::llabs(k.sample - target) <= window)
                ++near375;
        REQUIRE(near375 <= 1);
    }

    {
        const auto straight = renderFill18(0.0f, Groove::SongSectionId::Chorus).second;
        const auto swung = renderFill18(1.0f, Groove::SongSectionId::Chorus).second;
        REQUIRE_FALSE(straight.empty());
        REQUIRE_FALSE(swung.empty());
        int64_t straightAnd = -1;
        int64_t swungAnd = -1;
        constexpr int64_t andOf3 = 60000;  // beat 2.5
        for (const auto& t : straight)
            if (std::llabs(t.sample - andOf3) < 6000)
                if (straightAnd < 0 || std::llabs(t.sample - andOf3) < std::llabs(straightAnd - andOf3))
                    straightAnd = t.sample;
        for (const auto& t : swung)
            if (std::llabs(t.sample - andOf3) < 12000)
                if (swungAnd < 0 || std::llabs(t.sample - andOf3) < std::llabs(swungAnd - andOf3))
                    swungAnd = t.sample;
        REQUIRE(straightAnd >= 0);
        REQUIRE(swungAnd >= 0);
        REQUIRE(swungAnd > straightAnd + 1000);
    }

    {
        const auto verseToms = renderFill18(0.0f, Groove::SongSectionId::Verse).second;
        const auto chorusToms = renderFill18(0.0f, Groove::SongSectionId::Chorus).second;
        REQUIRE_FALSE(verseToms.empty());
        REQUIRE_FALSE(chorusToms.empty());
        auto meanVel = [](const auto& hits) {
            int sum = 0;
            for (const auto& h : hits)
                sum += h.vel;
            return static_cast<double>(sum) / static_cast<double>(hits.size());
        };
        REQUIRE(meanVel(chorusToms) > meanVel(verseToms) + 1.0);
    }
}

// ── Phase 37 C1: per-pattern feel applied at render time ─────────────────────

TEST_CASE("C1: per-pattern feel tightens a dense pattern but stays humanised", "[midi][C1]")
{
    // Render Blast Beat (pattern 8, the densest) with and without the feel and
    // compare the timing spread about the 16th grid. C1 must tighten it (smaller
    // spread) while leaving audible humanisation (not a machine grid).
    auto renderSpread = [](float humanize)
    {
        MidiPatternLibrary lib;
        PatternPlayer p;
        p.setPatternLibrary(&lib);
        p.prepare(48000.0, 2048);
        p.setRandomSeed(0xC0FFEE);
        p.snapBpm(120.0f);
        p.setPatternIndex(8);
        p.setSection(Groove::SongSectionId::Chorus);
        p.setGenrePreset(0);
        p.setStructureSilent(false);
        p.setSwing(0.0f);
        p.setHumanize(humanize);
        p.setBeatGridBassEnabled(false);

        const double spb = 24000.0;
        const int64_t span = static_cast<int64_t>(spb * 32.0);
        const auto events = MidiProbe::render(p, static_cast<int>((span + 2047) / 2048), 2048, 0);

        std::vector<double> err;
        for (const auto& e : events)
        {
            if (!e.isNoteOn || e.channel != 10)
                continue;
            const double beat = static_cast<double>(e.sample) / spb;
            const double grid = std::round(beat * 4.0) / 4.0;
            const double ms = (beat - grid) * (60000.0 / 120.0);
            if (std::abs(ms) > 25.0)
                continue;
            err.push_back(ms);
        }
        if (err.size() < 8)
            return 999.0;
        double m = 0.0;
        for (double x : err) m += x;
        m /= static_cast<double>(err.size());
        double v = 0.0;
        for (double x : err) { const double d = x - m; v += d * d; }
        return std::sqrt(v / static_cast<double>(err.size()));
    };

    const double base = renderSpread(0.0f);   // baked base template
    const double felt = renderSpread(1.0f);   // + per-pattern feel

    REQUIRE(base >= 3.0);      // the base template is audibly humanised (T3.3)
    REQUIRE(felt < base);      // C1 tightens the dense pattern
    REQUIRE(felt >= 1.0);      // ...but it is still humanised, not a grid
}
