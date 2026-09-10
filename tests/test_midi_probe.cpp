/**
 * T0.4 / T1.1 — MIDI test harness self-test.
 *
 * Renders a fixed pattern in 128- and 2048-sample blocks and compares absolute
 * event sample positions. Deferred drum note-offs (T1.1) make this buffer-invariant.
 */

#include <catch2/catch_test_macros.hpp>

#include "fixtures/MidiProbe.h"
#include "midi/MidiPatternLibrary.h"

#include <cmath>
#include <vector>

namespace
{

void prepareFixedPlayer(PatternPlayer& player, MidiPatternLibrary& lib, int blockSize)
{
    player.setPatternLibrary(&lib);
    player.prepare(48000.0, blockSize);
    player.setRandomSeed(0xC0FFEE);
    player.snapBpm(120.0f);
    player.setPatternIndex(4);  // Chorus Mid — crash durationBeats = 2.5
    player.setSection(Groove::SongSectionId::Chorus);
    player.setGenrePreset(3);   // Metal
    player.setStructureSilent(false);
    player.setBeatGridBassEnabled(true);
    player.setBassParams(40, 2);
}

int blocksForSpan(int64_t spanSamples, int blockSize)
{
    return static_cast<int>(spanSamples / blockSize);
}

} // namespace

TEST_CASE("MidiProbe renders note-ons for a fixed pattern", "[midi][probe]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    prepareFixedPlayer(player, lib, 512);

    const int64_t span = 2048 * 94;  // ~2 bars at 48 kHz / 120 BPM, divisible by 128 and 2048
    const int blockSize = 512;
    REQUIRE(span % blockSize == 0);

    const auto events = MidiProbe::render(player, blocksForSpan(span, blockSize), blockSize, 0);
    const auto ons = MidiProbe::noteOns(events);
    REQUIRE_FALSE(ons.empty());

    bool sawKick = false;
    bool sawBass = false;
    for (const auto& n : ons)
    {
        REQUIRE(n.sample >= 0);
        REQUIRE(n.sample < span);
        if (n.channel == 10 && n.note == 36)
            sawKick = true;
        if (n.channel == 2)
            sawBass = true;
    }
    REQUIRE(sawKick);
    REQUIRE(sawBass);
}

TEST_CASE("MidiProbe dual-block-size golden is buffer-invariant", "[midi][probe][golden]")
{
    // T1.1 / review §5.3: crash note-off *duration* is independent of block size.
    // Absolute note-on samples still move with microtiming clamp (T3.4); this
    // test asserts the deferred-off contract, not the full fingerprint.
    MidiPatternLibrary lib;
    PatternPlayer a, b;
    prepareFixedPlayer(a, lib, 128);
    prepareFixedPlayer(b, lib, 2048);

    const int64_t span = 2048 * 188;  // ~4 bars, divisible by 128 and 2048
    const auto events128  = MidiProbe::render(a, blocksForSpan(span, 128),  128,  0);
    const auto events2048 = MidiProbe::render(b, blocksForSpan(span, 2048), 2048, 0);

    REQUIRE_FALSE(events128.empty());
    REQUIRE_FALSE(events2048.empty());

    auto crashHolds = [](const std::vector<MidiProbe::Event>& ev) {
        std::vector<int64_t> holds, ons;
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
    const auto h128 = crashHolds(events128);
    const auto h2048 = crashHolds(events2048);
    REQUIRE_FALSE(h128.empty());
    REQUIRE(h128 == h2048);
    const int64_t spb = static_cast<int64_t>(std::llround(48000.0 * 60.0 / 120.0));
    for (auto h : h128)
        REQUIRE(h >= spb);
}
