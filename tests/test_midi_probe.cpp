/**
 * T0.4 — MIDI test harness self-test.
 *
 * Renders a fixed pattern in 128- and 2048-sample blocks and compares absolute
 * event sample positions. Equality is the documented contract; it currently
 * fails because drum note-offs are clamped into the triggering block (T1.1).
 * Tagged [!mayfail] until that fix lands.
 */

#include <catch2/catch_test_macros.hpp>

#include "fixtures/MidiProbe.h"
#include "midi/MidiPatternLibrary.h"

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

TEST_CASE("MidiProbe dual-block-size golden is buffer-invariant", "[midi][probe][golden][!mayfail]")
{
    // T1.1 / review §5.2: the same musical span at 128 and 2048 must produce
    // identical absolute event samples. Today they differ because every drum
    // note-off is jmin'd into the triggering block. Keep the assertion honest;
    // remove [!mayfail] in T1.1 when the deferred note-off table lands.
    MidiPatternLibrary lib;
    PatternPlayer a, b;
    prepareFixedPlayer(a, lib, 128);
    prepareFixedPlayer(b, lib, 2048);

    const int64_t span = 2048 * 188;  // ~4 bars, divisible by 128 and 2048
    const auto events128  = MidiProbe::render(a, blocksForSpan(span, 128),  128,  0);
    const auto events2048 = MidiProbe::render(b, blocksForSpan(span, 2048), 2048, 0);

    REQUIRE_FALSE(events128.empty());
    REQUIRE_FALSE(events2048.empty());
    REQUIRE(MidiProbe::fingerprint(events128) == MidiProbe::fingerprint(events2048));
}
