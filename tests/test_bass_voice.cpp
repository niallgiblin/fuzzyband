/**
 * @file
 * @brief BassVoice tests — the one monophonic bass voice: producer arbitration
 *        (grid is a gap-filler while the mirror rings) and per-note provenance.
 *
 * The extraction is behaviour-frozen to the 1.0.3 ownership contract
 * (`docs/BASS_MIRRORING.md` §7); the existing `test_pattern_player.cpp` suite is
 * the rendering-regression guard. These tests cover the *new* seam directly:
 * which producer owns the voice, and which producer emitted each note-on.
 */

#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "midi/BassVoice.h"

namespace
{
using Producer = BassVoice::Producer;

int countNoteOns(const juce::MidiBuffer& midi)
{
    int n = 0;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
            ++n;
    return n;
}

int countNoteOffs(const juce::MidiBuffer& midi)
{
    int n = 0;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOff())
            ++n;
    return n;
}
} // namespace

TEST_CASE("BassVoice: a learned mirror request emits a Mirror-tagged note",
          "[bass][voice][provenance]")
{
    BassVoice voice;
    voice.reset();
    voice.requestLearned(40, 0.5f, 10, 100, /*hold=*/false, Producer::Mirror);

    juce::MidiBuffer midi;
    voice.flushLearned(midi, 512, 0);

    REQUIRE(countNoteOns(midi) == 1);
    REQUIRE(voice.getLearnedCount() == 1);
    REQUIRE(voice.getProducerCount(Producer::Mirror) == 1);
    REQUIRE(voice.getLastProducer() == Producer::Mirror);

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            REQUIRE(msg.getChannel() == BassVoice::kBassChannel);
    }
}

TEST_CASE("BassVoice: a frozen snapshot note is tagged Frozen, not Mirror",
          "[bass][voice][provenance]")
{
    BassVoice voice;
    voice.reset();
    voice.requestLearned(43, 0.58f, 0, 100, /*hold=*/false, Producer::Frozen);

    juce::MidiBuffer midi;
    voice.flushLearned(midi, 512, 0);

    REQUIRE(voice.getProducerCount(Producer::Frozen) == 1);
    REQUIRE(voice.getProducerCount(Producer::Mirror) == 0);
    REQUIRE(voice.getLastProducer() == Producer::Frozen);
}

TEST_CASE("BassVoice: the held mirror gates the grid out, and releasing it lets the grid back in",
          "[bass][voice][arbitration]")
{
    BassVoice voice;
    voice.reset();
    voice.setGuitarAudible(true);
    voice.requestLearned(40, 0.5f, 0, 1000, /*hold=*/true, Producer::Mirror);

    juce::MidiBuffer held;
    voice.flushLearned(held, 512, 0);
    REQUIRE(voice.getGridGateSample() == std::numeric_limits<std::int64_t>::max());

    // While held, a grid note must be refused and NOT counted.
    juce::MidiBuffer gridWhileHeld;
    REQUIRE_FALSE(voice.emitGrid(gridWhileHeld, 512, 1024, 1024, 43, 100, 0, 1000, 0,
                                 /*forceRetrigger=*/true, Producer::GridAuthored));
    REQUIRE(countNoteOns(gridWhileHeld) == 0);
    REQUIRE(voice.getGridCount() == 0);
    REQUIRE(voice.getLastProducer() == Producer::Mirror);   // unchanged by the refusal

    // Guitar stops → the held note is released and the grid gate opens.
    voice.setGuitarAudible(false);
    juce::MidiBuffer release;
    voice.releaseHeldIfStopped(release, 2048);
    REQUIRE(countNoteOffs(release) == 1);
    REQUIRE(voice.getGridGateSample() == 2048);

    juce::MidiBuffer gridAfterStop;
    REQUIRE(voice.emitGrid(gridAfterStop, 512, 2048, 2048, 43, 100, 0, 1000, 0,
                           /*forceRetrigger=*/true, Producer::GridAuthored));
    REQUIRE(countNoteOns(gridAfterStop) == 1);
    REQUIRE(voice.getGridCount() == 1);
    REQUIRE(voice.getProducerCount(Producer::GridAuthored) == 1);
    REQUIRE(voice.getLastProducer() == Producer::GridAuthored);
}

TEST_CASE("BassVoice: the grid gap-fills between mirror notes but not inside them",
          "[bass][voice][arbitration]")
{
    BassVoice voice;
    voice.reset();
    voice.setGuitarAudible(true);

    // A gated (non-held) mirror note owns [0, 1000).
    voice.requestLearned(40, 0.5f, 0, 1000, /*hold=*/false, Producer::Mirror);
    juce::MidiBuffer midi;
    voice.flushLearned(midi, 512, 0);
    const std::int64_t gate = voice.getGridGateSample();
    REQUIRE(gate == 1000);

    juce::MidiBuffer inside;
    REQUIRE_FALSE(voice.emitGrid(inside, 512, 0, 500, 43, 100, 500, 100, 0, true,
                                 Producer::GridAuthored));
    juce::MidiBuffer after;
    REQUIRE(voice.emitGrid(after, 512, 1000, 1000, 43, 100, 0, 100, 0, true,
                           Producer::GridAuthored));
}

TEST_CASE("BassVoice: the voice is monophonic — a new note closes the ringing one",
          "[bass][voice][monophonic]")
{
    BassVoice voice;
    voice.reset();

    juce::MidiBuffer midi;
    voice.emitGrid(midi, 512, 0, 0, 40, 100, 0, 1000, 0, true, Producer::GridAuthored);
    voice.emitGrid(midi, 512, 0, 100, 43, 100, 100, 1000, 0, true, Producer::GridHarmonic);

    // noteOn 40, then noteOff 40 + noteOn 43 at the retrigger.
    REQUIRE(countNoteOns(midi) == 2);
    REQUIRE(countNoteOffs(midi) == 1);

    int offNote = -1, offSample = -1;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOff())
        {
            offNote = msg.getNoteNumber();
            offSample = meta.samplePosition;
        }
    }
    REQUIRE(offNote == 40);
    REQUIRE(offSample == 100);
    REQUIRE(voice.getLastProducer() == Producer::GridHarmonic);
    REQUIRE(voice.getProducerCount(Producer::GridAuthored) == 1);
    REQUIRE(voice.getProducerCount(Producer::GridHarmonic) == 1);
}

TEST_CASE("BassVoice: provenance ring is most-recent-first and reports the producer",
          "[bass][voice][provenance]")
{
    BassVoice voice;
    voice.reset();

    juce::MidiBuffer midi;
    voice.emitPickup(midi, 512, 0, 45, 96, 0, 100);
    voice.emitGrid(midi, 512, 0, 200, 40, 100, 200, 100, 0, true, Producer::GridAuthored);
    voice.requestLearned(43, 0.5f, 300, 100, false, Producer::Frozen);
    voice.flushLearned(midi, 512, 0);

    BassVoice::NoteOn recent[8];
    const int n = voice.getRecentNoteOns(recent, 8);
    REQUIRE(n == 3);
    REQUIRE(recent[0].producer == Producer::Frozen);
    REQUIRE(recent[0].midi == 43);
    REQUIRE(recent[1].producer == Producer::GridAuthored);
    REQUIRE(recent[1].midi == 40);
    REQUIRE(recent[2].producer == Producer::Pickup);
    REQUIRE(recent[2].midi == 45);
    REQUIRE(voice.getLastProducer() == Producer::Frozen);
}

TEST_CASE("BassVoice: seek/silence flush drops the grid claim and the pending queue",
          "[bass][voice][flush]")
{
    BassVoice voice;
    voice.reset();
    voice.setGuitarAudible(true);
    voice.requestLearned(40, 0.5f, 0, 1000, /*hold=*/true, Producer::Mirror);

    juce::MidiBuffer midi;
    voice.flushLearned(midi, 512, 0);
    REQUIRE(voice.getGridGateSample() == std::numeric_limits<std::int64_t>::max());

    voice.requestLearned(43, 0.5f, 0, 100, false, Producer::Frozen);  // queued, not yet flushed
    voice.clearPending();
    voice.flushAll(midi, 0);

    REQUIRE(voice.getGridGateSample() == -1);

    juce::MidiBuffer after;
    voice.flushLearned(after, 512, 0);   // nothing queued
    REQUIRE(countNoteOns(after) == 0);

    // The grid is free again.
    REQUIRE(voice.emitGrid(after, 512, 0, 0, 43, 100, 0, 100, 0, true, Producer::GridAuthored));
}

TEST_CASE("BassVoice: a held mirror note survives the guitar's natural decay",
          "[bass][voice][sustain]")
{
    // Regression: releasing at 25% of the attack level cut the bass off ~150 ms
    // into an ordinary decaying guitar note, then left it silent for the rest of
    // the phrase (measured on the user's real takes, a 2 s hole in the bass).
    // A decay to 20% must HOLD; only a real fall-off past the release floor ends it.
    BassVoice voice;
    voice.reset();
    voice.setGuitarAudible(true);
    voice.setInputLevel(0.40f);   // attack
    voice.requestLearned(40, 0.5f, 0, 1000, /*hold=*/true, Producer::Mirror);
    juce::MidiBuffer on;
    voice.flushLearned(on, 512, 0);
    REQUIRE(countNoteOns(on) == 1);

    // The note decays to 20% of its attack level — still audible, still held.
    voice.setInputLevel(0.08f);
    juce::MidiBuffer mid;
    voice.releaseHeldIfStopped(mid, 1024);
    REQUIRE(countNoteOffs(mid) == 0);

    // …and further to 12.5%, still above the 10% release fraction: still held.
    voice.setInputLevel(0.05f);
    voice.releaseHeldIfStopped(mid, 1536);
    REQUIRE(countNoteOffs(mid) == 0);

    // The guitar has genuinely gone quiet: release.
    voice.setInputLevel(0.001f);
    voice.releaseHeldIfStopped(mid, 2048);
    REQUIRE(countNoteOffs(mid) == 1);
}
