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

// ── Musicality pivot: bass engine (A1) ───────────────────────────────────────

TEST_CASE("A1: listen-grid bass is harmonic root from setBassParams", "[midi][A1]")
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
    // Listen grid is harmonic root from setBassParams, not authored intervals.
    REQUIRE(bassNotes.count(40) > 0);  // root
    REQUIRE(bassNotes.count(45) == 0); // library +5 must not leak
    REQUIRE(bassNotes.count(47) == 0); // library +7 must not leak
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

TEST_CASE("A1.2: live-mirror bass snaps to the section's chord tones", "[midi][A1]")
{
    // The listening bass follows the guitarist's rhythm but resolves each note to
    // the current section's chord tones around the live root, so it belongs to the
    // section (A1.2). Render snapBassToSectionHarmony directly.
    MidiPatternLibrary lib;
    PatternPlayer player;
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, 512);
    player.snapBpm(120.0f);
    player.setBassParams(40, 2);  // E2 live root (pc 4)

    player.setSection(Groove::SongSectionId::Verse);
    // Verse palette {root, fourth}: root stays root; a B (pc 11) snaps to the fourth (A).
    REQUIRE(player.snapBassToSectionHarmony(40) == 40);  // E → root
    REQUIRE(player.snapBassToSectionHarmony(59) == 45);  // B → A (fourth)

    player.setSection(Groove::SongSectionId::Chorus);
    // Chorus palette {root, fourth, fifth}: the same B snaps to the fifth (B).
    REQUIRE(player.snapBassToSectionHarmony(59) == 47);  // B → B (fifth)
    REQUIRE(player.snapBassToSectionHarmony(43) == 45);  // G# → A (fourth)

    player.setSection(Groove::SongSectionId::Breakdown);
    // Breakdown is root-only: every note collapses to the root.
    REQUIRE(player.snapBassToSectionHarmony(59) == 40);
    REQUIRE(player.snapBassToSectionHarmony(43) == 40);
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

    const int quiet = sumDrumVel(0.75f);  // clamped lower bound
    const int loud  = sumDrumVel(1.30f);  // clamped upper bound
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
