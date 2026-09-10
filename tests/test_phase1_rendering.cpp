/**
 * Phase 1 — rendering correctness (T1.1–T1.6).
 *
 * Asserts on MidiProbe-rendered MIDI at absolute sample positions so the
 * suite is independent of block size (review §5.2, §5.3, §5.8).
 */

#include <catch2/catch_test_macros.hpp>

#include "fixtures/MidiProbe.h"
#include "midi/GrooveTemplate.h"
#include "midi/MidiPatternLibrary.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

namespace
{

constexpr double kSr = 48000.0;
constexpr float kBpm = 120.0f;

void preparePlayer(PatternPlayer& player, MidiPatternLibrary& lib, int blockSize, int patternIndex)
{
    player.setPatternLibrary(&lib);
    player.prepare(kSr, blockSize);
    player.setRandomSeed(0xC0FFEE);
    player.snapBpm(kBpm);
    player.setPatternIndex(patternIndex);
    player.setSection(Groove::SongSectionId::Chorus);  // no injected ghosts
    player.setGenrePreset(3);                          // Metal
    player.setStructureSilent(false);
    player.setSwing(0.0f);
    player.setBeatGridBassEnabled(false);
    player.setBassParams(40, 2);
}

int blocksFor(int64_t span, int blockSize)
{
    return static_cast<int>(span / blockSize);
}

int64_t samplesPerBeat()
{
    return static_cast<int64_t>(std::llround((60.0 / static_cast<double>(kBpm)) * kSr));
}

int drumOnCount(const std::vector<MidiProbe::Event>& events, int note)
{
    int n = 0;
    for (const auto& e : events)
        if (e.isNoteOn && e.channel == 10 && e.note == note)
            ++n;
    return n;
}

} // namespace

// ── T1.1 deferred drum note-offs ─────────────────────────────────────────────

TEST_CASE("T1.1 note-off ledger: every drum/bass note-on has a matching off", "[midi][phase1][t1.1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 128, 4);  // Chorus Mid — crash durationBeats = 2.5

    const int64_t span = 2048 * 188;  // ~4 bars
    auto events = MidiProbe::render(player, blocksFor(span, 128), 128, 0);

    juce::MidiBuffer tail;
    player.flushAllPendingNoteOffs(tail, 0);
    MidiProbe::collect(tail, span, events);

    std::map<std::pair<int, int>, int> balance;  // (ch, note) → on-off
    for (const auto& e : events)
    {
        if (e.isNoteOn)
            ++balance[{ e.channel, e.note }];
        else if (e.isNoteOff)
            --balance[{ e.channel, e.note }];
    }
    for (const auto& [key, n] : balance)
    {
        INFO("ch=" << key.first << " note=" << key.second << " balance=" << n);
        REQUIRE(n == 0);
    }
}

TEST_CASE("T1.1 long notes release in a later block at size 128", "[midi][phase1][t1.1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 128, 4);

    const auto& pat = lib.getPattern(4);
    REQUIRE(pat.drumEvents.front().note == 49);
    REQUIRE(pat.drumEvents.front().durationBeats >= 1.0f);

    const int blockSize = 128;
    const int64_t span = 2048 * 188;
    const auto events = MidiProbe::render(player, blocksFor(span, blockSize), blockSize, 0);

    bool found = false;
    for (const auto& on : MidiProbe::noteOns(events))
    {
        if (on.channel != 10 || on.note != 49)
            continue;
        for (const auto& off : MidiProbe::noteOffs(events))
        {
            if (off.channel != 10 || off.note != 49 || off.sample <= on.sample)
                continue;
            const int64_t onBlock = on.sample / blockSize;
            const int64_t offBlock = off.sample / blockSize;
            REQUIRE(offBlock > onBlock);
            found = true;
            break;
        }
        if (found)
            break;
    }
    REQUIRE(found);
}

TEST_CASE("T1.1 dual-block-size golden: 2.5-beat crash is buffer-invariant", "[midi][phase1][t1.1][golden]")
{
    MidiPatternLibrary lib;
    PatternPlayer a, b;
    preparePlayer(a, lib, 128, 4);
    preparePlayer(b, lib, 2048, 4);

    const int64_t span = 2048 * 188;
    const auto e128  = MidiProbe::render(a, blocksFor(span, 128),  128,  0);
    const auto e2048 = MidiProbe::render(b, blocksFor(span, 2048), 2048, 0);
    REQUIRE_FALSE(e128.empty());

    auto crashHolds = [](const std::vector<MidiProbe::Event>& ev) {
        std::vector<int64_t> holds;
        std::vector<int64_t> ons;
        for (const auto& e : ev)
        {
            if (e.channel != 10 || e.note != 49)
                continue;
            if (e.isNoteOn)
                ons.push_back(e.sample);
            else if (e.isNoteOff && !ons.empty())
            {
                holds.push_back(e.sample - ons.front());
                ons.erase(ons.begin());
            }
        }
        return holds;
    };

    const auto h128 = crashHolds(e128);
    const auto h2048 = crashHolds(e2048);
    REQUIRE_FALSE(h128.empty());
    REQUIRE(h128 == h2048);
    const int64_t spb = samplesPerBeat();
    for (auto h : h128)
        REQUIRE(h >= spb);  // crash rings at least a beat at every block size
}

TEST_CASE("T1.1 open hats and crashes hold at least one beat", "[midi][phase1][t1.1]")
{
    MidiPatternLibrary lib;
    const auto& pat = lib.getPattern(4);
    for (const auto& ev : pat.drumEvents)
    {
        if (ev.note == 46)
            REQUIRE(ev.durationBeats >= 1.0f);
        if (ev.note == 49)
            REQUIRE(ev.durationBeats >= 1.5f);
    }

    PatternPlayer player;
    preparePlayer(player, lib, 512, 4);
    const int64_t spb = samplesPerBeat();
    const auto events = MidiProbe::render(player, blocksFor(spb * 8, 512), 512, 0);

    auto minHold = [&](int note) -> int64_t {
        int64_t best = 0;
        for (const auto& on : MidiProbe::noteOns(events))
        {
            if (on.channel != 10 || on.note != note)
                continue;
            for (const auto& off : MidiProbe::noteOffs(events))
            {
                if (off.channel == 10 && off.note == note && off.sample > on.sample)
                {
                    best = std::max(best, off.sample - on.sample);
                    break;
                }
            }
        }
        return best;
    };

    REQUIRE(minHold(49) >= spb);       // crash ≥ 1 beat
    REQUIRE(minHold(46) >= spb * 9 / 10);  // open hat ≥ ~1 beat (jitter)
}

// ── T1.2 seek / silence note-off release ─────────────────────────────────────

TEST_CASE("T1.2 seek while crash and bass ring emits matching note-offs", "[midi][phase1][t1.2]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 512, 4);
    player.setBeatGridBassEnabled(true);

    const int64_t spb = samplesPerBeat();
    // One short block to start the crash (2.5 beats) and a bass note.
    juce::MidiBuffer first;
    player.process(first, 512, 0, true);

    bool crashOn = false, bassOn = false;
    for (const auto meta : first)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 49)
            crashOn = true;
        if (msg.isNoteOn() && msg.getChannel() == 2)
            bassOn = true;
    }
    REQUIRE(crashOn);
    REQUIRE(bassOn);

    // Jump far past the ringing notes (well outside ±2-block slack).
    juce::MidiBuffer seek;
    player.process(seek, 512, spb * 64, true);

    bool crashOff = false, bassOff = false;
    for (const auto meta : seek)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOff() && msg.getChannel() == 10 && msg.getNoteNumber() == 49)
            crashOff = true;
        if (msg.isNoteOff() && msg.getChannel() == 2)
            bassOff = true;
    }
    REQUIRE(crashOff);
    REQUIRE(bassOff);
}

TEST_CASE("T1.2 silence flushes pending offs and drops a stale armed crash", "[midi][phase1][t1.2][t1.6]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 512, 1);  // Verse Groove — no pattern crash
    player.armTransitionCrash();
    player.setStructureSilent(true);

    juce::MidiBuffer silent;
    player.process(silent, 512, 0, true);

    player.setStructureSilent(false);
    juce::MidiBuffer after;
    player.process(after, 512, 512, true);

    int crashOns = 0;
    for (const auto meta : after)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 49)
            ++crashOns;
    }
    REQUIRE(crashOns == 0);
}

// ── T1.3 split-block time base ───────────────────────────────────────────────

TEST_CASE("T1.3 mid-block pattern change does not emit the new groove early", "[midi][phase1][t1.3]")
{
    MidiPatternLibrary lib;
    const int64_t bar = samplesPerBeat() * 4;       // 96000
    const int64_t sixteenth = samplesPerBeat() / 4; // 6000
    const int blockSize = 18000;                    // bar sits at offset 6000
    REQUIRE(bar % blockSize == 6000);

    PatternPlayer player;
    preparePlayer(player, lib, blockSize, 1);

    int64_t pos = 0;
    bool queued = false;
    std::vector<int64_t> kicksAfterBar;
    const int64_t end = bar + sixteenth * 2;
    while (pos < end)
    {
        if (!queued && pos + blockSize > bar)
        {
            player.setPatternIndex(21);  // 16th kicks; pattern 1 has none at beat 4.25
            queued = true;
        }
        juce::MidiBuffer midi;
        player.process(midi, blockSize, pos, true);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10 || msg.getNoteNumber() != 36)
                continue;
            const int64_t abs = pos + meta.samplePosition;
            if (abs > bar + 64)
                kicksAfterBar.push_back(abs);
        }
        pos += blockSize;
    }

    REQUIRE_FALSE(kicksAfterBar.empty());
    const int64_t expected = bar + sixteenth;
    bool foundSixteenth = false;
    for (auto s : kicksAfterBar)
    {
        // Broken T1.3 places the 16th at the bar line (offset 0 of the post-change
        // half). The correct base puts it ~0.25 beats later.
        REQUIRE(s > bar + sixteenth / 2);
        if (std::llabs(s - expected) < 512)
            foundSixteenth = true;
    }
    REQUIRE(foundSixteenth);
}

// ── T1.4 learned-bass octave fold ────────────────────────────────────────────

TEST_CASE("T1.4 learned bass fold preserves pitch class in [28, 55]", "[midi][phase1][t1.4]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 512, 0);
    player.setBeatGridBassEnabled(false);
    player.setStructureSilent(false);

    for (int pc = 0; pc < 12; ++pc)
    {
        for (int transpose : { -12, 0, 12 })
        {
            PatternPlayer p;
            preparePlayer(p, lib, 512, 0);
            p.setBeatGridBassEnabled(false);
            const int raw = 36 + pc + transpose;  // C2 ± octave, as callers pass
            p.triggerLearnedBassNote(raw, 0.8f, 0, 20000);

            juce::MidiBuffer midi;
            p.process(midi, 512, 0, true);

            bool found = false;
            int outNote = -1;
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                {
                    found = true;
                    outNote = msg.getNoteNumber();
                    break;
                }
            }
            INFO("pc=" << pc << " transpose=" << transpose << " raw=" << raw << " out=" << outNote);
            REQUIRE(found);
            REQUIRE(outNote >= 28);
            REQUIRE(outNote <= 55);
            REQUIRE(((outNote % 12) + 12) % 12 == pc);
        }
    }
}

// ── T1.5 ghost threshold vs library ──────────────────────────────────────────

TEST_CASE("T1.5 authored ghosts sit under ghostThreshold", "[midi][phase1][t1.5]")
{
    MidiPatternLibrary lib;
    const uint8_t threshold = Groove::rock().ghostThreshold;
    REQUIRE(threshold == 62);
    REQUIRE(Groove::rock().ghostVelocityLo == 30.0f);
    REQUIRE(Groove::rock().ghostVelocityHi == 55.0f);

    int ghostCount = 0;
    for (int i = 0; i < lib.patternCount(); ++i)
    {
        for (const auto& ev : lib.getPattern(i).drumEvents)
        {
            if (!ev.isGhost)
                continue;
            ++ghostCount;
            INFO("pattern " << i << " beat " << ev.beatOffset << " vel " << (int) ev.velocity);
            REQUIRE(ev.velocity <= threshold);
            REQUIRE(ev.velocity <= 58);
        }
    }
    REQUIRE(ghostCount >= 6);
}

TEST_CASE("T1.5 pattern 20 off-16th snares render as ghosts", "[midi][phase1][t1.5]")
{
    MidiPatternLibrary lib;
    const auto& p20 = lib.getPattern(20);
    REQUIRE(p20.name == "Verse Ghost");
    for (const auto& ev : p20.drumEvents)
    {
        if (ev.note != 38)
            continue;
        const int cell = Groove::grid16Of(ev.beatOffset);
        if (cell == 2 || cell == 6 || cell == 10 || cell == 14)  // off-8ths used as ghosts
        {
            REQUIRE(ev.isGhost);
            REQUIRE(ev.velocity <= Groove::rock().ghostThreshold);
        }
    }

    PatternPlayer player;
    preparePlayer(player, lib, 128, 20);
    player.setSection(Groove::SongSectionId::Verse);
    player.setGenrePreset(0);  // Rock — ghost band 30–55
    player.setRandomSeed(1);

    const int64_t spb = samplesPerBeat();
    const auto events = MidiProbe::render(player, blocksFor(spb * 4, 128), 128, 0);
    const int ghostLo = static_cast<int>(Groove::rock().ghostVelocityLo);
    const int ghostHi = static_cast<int>(Groove::rock().ghostVelocityHi);

    int ghostHits = 0;
    for (const auto& on : MidiProbe::noteOns(events))
    {
        if (on.channel != 10 || on.note != 38)
            continue;
        if (on.velocity >= ghostLo && on.velocity <= ghostHi)
            ++ghostHits;
    }
    REQUIRE(ghostHits >= 2);
}

// ── T1.6 crash placement ─────────────────────────────────────────────────────

TEST_CASE("T1.6 armed crash into a crashing pattern emits exactly one note 49", "[midi][phase1][t1.6]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 512, 4);  // Chorus Mid crashes on beat 1
    player.armTransitionCrash();

    const int64_t spb = samplesPerBeat();
    const auto events = MidiProbe::render(player, blocksFor(spb, 512), 512, 0);
    REQUIRE(drumOnCount(events, 49) == 1);
}

TEST_CASE("T1.6 armed crash lands on a beat, not block start", "[midi][phase1][t1.6]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 512, 1);  // no pattern crash

    const int64_t spb = samplesPerBeat();
    // Start 200 samples before a beat so offset 0 of the armed block is not a beat.
    const int64_t start = spb - 200;
    int64_t pos = 0;
    while (pos + 512 <= start)
    {
        juce::MidiBuffer midi;
        player.process(midi, 512, pos, true);
        pos += 512;
    }

    player.armTransitionCrash();
    const auto events = MidiProbe::render(player, 16, 512, pos);
    int64_t crashAt = -1;
    for (const auto& e : events)
    {
        if (e.isNoteOn && e.channel == 10 && e.note == 49)
        {
            crashAt = e.sample;
            break;
        }
    }
    REQUIRE(crashAt >= 0);
    const int64_t beat = ((crashAt + spb / 2) / spb) * spb;
    REQUIRE(std::llabs(crashAt - beat) < 32);
}
