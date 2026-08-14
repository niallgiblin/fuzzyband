#include <catch2/catch_test_macros.hpp>

#include "analysis/PlaybackGate.h"

namespace
{
// Feed `totalSamples` worth of a single structure state in `blockSize` chunks.
GateDecision feed(PlaybackGate& gate, StructureState st, double sr,
                  int totalSamples, int blockSize = 512)
{
    GateDecision last{};
    int remaining = totalSamples;
    while (remaining > 0)
    {
        const int n = std::min(remaining, blockSize);
        last = gate.update(st, n, sr);
        remaining -= n;
    }
    return last;
}
} // namespace

TEST_CASE("PlaybackGate: short silence is a phrase breath, not a reset", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;

    // Establish a non-silent previous state.
    feed(gate, StructureState::LOUD, sr, static_cast<int>(1.0 * sr));

    // 4s of silence is below the 8s hold — a phrase breath.
    const GateDecision silentGd = feed(gate, StructureState::SILENT, sr, static_cast<int>(4.0 * sr));
    REQUIRE_FALSE(silentGd.resetTrackers);
    REQUIRE_FALSE(silentGd.armCrash);
}

TEST_CASE("PlaybackGate: phrase-breath re-entry arms a crash cymbal", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;

    feed(gate, StructureState::LOUD, sr, static_cast<int>(1.0 * sr));
    feed(gate, StructureState::SILENT, sr, static_cast<int>(4.0 * sr));

    // First loud block after a phrase breath → arm crash.
    const GateDecision gd = gate.update(StructureState::LOUD, 512, sr);
    REQUIRE(gd.armCrash);
    REQUIRE_FALSE(gd.resetTrackers);
}

TEST_CASE("PlaybackGate: full reset fires after long silence (> 8s)", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;

    feed(gate, StructureState::LOUD, sr, static_cast<int>(1.0 * sr));

    const GateDecision lastGd = feed(gate, StructureState::SILENT, sr, static_cast<int>(9.0 * sr));
    REQUIRE(lastGd.resetTrackers);
    REQUIRE_FALSE(lastGd.armCrash);
}

TEST_CASE("PlaybackGate: 7s silence does NOT trigger reset (within 8s hold)", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;

    feed(gate, StructureState::LOUD, sr, static_cast<int>(1.0 * sr));

    const GateDecision gd = feed(gate, StructureState::SILENT, sr, static_cast<int>(7.0 * sr));
    REQUIRE_FALSE(gd.resetTrackers);
}

TEST_CASE("PlaybackGate: reset() clears state", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;

    feed(gate, StructureState::LOUD, sr, static_cast<int>(1.0 * sr));
    feed(gate, StructureState::SILENT, sr, static_cast<int>(4.0 * sr));
    gate.reset();

    // After reset, a single SILENT block must not request a reset.
    const GateDecision gd = gate.update(StructureState::SILENT, 512, sr);
    REQUIRE_FALSE(gd.resetTrackers);
    REQUIRE_FALSE(gd.armCrash);
}

TEST_CASE("PlaybackGate: GateDecision defaults", "[playback_gate]")
{
    GateDecision gd;
    REQUIRE_FALSE(gd.armCrash);
    REQUIRE_FALSE(gd.resetTrackers);
}
