#include <catch2/catch_test_macros.hpp>

#include "analysis/PlaybackGate.h"

// Helper: call update() repeatedly with SILENT state for totalSamples worth of blocks.
static void feedSilence(PlaybackGate& gate, double sr, int totalSamples, int blockSize = 512)
{
    int remaining = totalSamples;
    while (remaining > 0)
    {
        const int n = std::min(remaining, blockSize);
        gate.update(StructureState::SILENT, 0.0, false, false, 0.0f, n, sr);
        remaining -= n;
    }
}

// Helper: call update() repeatedly with LOUD state for totalSamples worth of blocks.
static GateDecision feedLoud(PlaybackGate& gate, double sr, int totalSamples,
                              double beatPhase, bool isLocked, bool isOnsetLocked,
                              float conf, int blockSize = 512)
{
    GateDecision last{};
    int remaining = totalSamples;
    while (remaining > 0)
    {
        const int n = std::min(remaining, blockSize);
        last = gate.update(StructureState::LOUD, beatPhase, isLocked, isOnsetLocked, conf, n, sr);
        remaining -= n;
    }
    return last;
}

TEST_CASE("PlaybackGate: phrase-breath hold keeps gate closed and does not reset trackers", "[playback_gate]")
{
    // Feed SILENT for < 2 seconds of samples — should not trigger resetTrackers.
    PlaybackGate gate;
    const double sr = 44100.0;
    // 1.5 seconds — below kPhraseBreathHoldSec (2.0s)
    const int silentSamples = static_cast<int>(1.5 * sr);
    GateDecision gd{};
    int remaining = silentSamples;
    while (remaining > 0)
    {
        const int n = std::min(remaining, 512);
        gd = gate.update(StructureState::SILENT, 0.0, false, false, 0.0f, n, sr);
        remaining -= n;
    }
    REQUIRE_FALSE(gd.resetTrackers);
    REQUIRE_FALSE(gd.gateOpen);
    REQUIRE_FALSE(gd.armCrash);
    REQUIRE_FALSE(gd.snapBeatNow);
    REQUIRE_FALSE(gate.isGateOpen());
}

TEST_CASE("PlaybackGate: full reset fires after long silence (> 8s)", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;
    // Feed > 8s of silence (9.0s = 396900 samples)
    const int silentSamples = static_cast<int>(9.0 * sr);
    GateDecision lastGd{};
    int remaining = silentSamples;
    while (remaining > 0)
    {
        const int n = std::min(remaining, 512);
        lastGd = gate.update(StructureState::SILENT, 0.0, false, false, 0.0f, n, sr);
        remaining -= n;
    }
    REQUIRE(lastGd.resetTrackers);
    REQUIRE_FALSE(lastGd.gateOpen);
    REQUIRE_FALSE(gate.isGateOpen());
}

TEST_CASE("PlaybackGate: 7s silence does NOT trigger reset (within 8s hold)", "[playback_gate]")
{
    // 7.0s is below the 8.0s hold — should NOT trigger resetTrackers.
    PlaybackGate gate;
    const double sr = 44100.0;
    const int silentSamples = static_cast<int>(7.0 * sr);
    GateDecision gd{};
    int remaining = silentSamples;
    while (remaining > 0)
    {
        const int n = std::min(remaining, 512);
        gd = gate.update(StructureState::SILENT, 0.0, false, false, 0.0f, n, sr);
        remaining -= n;
    }
    REQUIRE_FALSE(gd.resetTrackers);
    REQUIRE_FALSE(gd.gateOpen);
    REQUIRE_FALSE(gate.isGateOpen());
}

TEST_CASE("PlaybackGate: active fallback opens gate after kActiveFallbackStartSec when not locked", "[playback_gate]")
{
    // Gate was closed, feed LOUD with no tempo lock and low confidence for > 0.35s
    PlaybackGate gate;
    const double sr = 44100.0;
    // Feed slightly more than kActiveFallbackStartSec (0.35s) = ~15435 samples
    // Use enough to cross beat phase wrap for snap. Since fallbackSnap fires, snapBeatNow=true.
    // Feed a total of 1.0s with monotonically increasing beatPhase to ensure wrap fires.
    const int totalSamples = static_cast<int>(1.0 * sr);
    GateDecision gd{};
    int remaining = totalSamples;
    double phase = 0.0;
    const double phaseStep = (512.0 / sr) * (120.0 / 60.0); // 120 BPM advance per block
    while (remaining > 0)
    {
        const int n = std::min(remaining, 512);
        phase = std::fmod(phase + phaseStep, 1.0);
        gd = gate.update(StructureState::LOUD, phase, false, false, 0.2f, n, sr);
        remaining -= n;
    }
    // After > kActiveFallbackStartSec with no lock, gate should open
    REQUIRE(gate.isGateOpen());
    REQUIRE_FALSE(gd.armCrash);
}

TEST_CASE("PlaybackGate: armCrash fires on phrase-breath re-entry (SILENT then LOUD)", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;

    // Phase 1: Feed some LOUD to open gate
    feedLoud(gate, sr, static_cast<int>(1.0 * sr), 0.0, true, true, 0.8f);

    // Phase 2: Feed SILENT < 8s (phrase breath, inPhraseBreath=true)
    feedSilence(gate, sr, static_cast<int>(4.0 * sr));

    // Phase 3: One LOUD block — should fire armCrash
    GateDecision gd = gate.update(StructureState::LOUD, 0.5, true, true, 0.8f, 512, sr);
    REQUIRE(gd.armCrash);
}

TEST_CASE("PlaybackGate: beat-snap fires at bar boundary when pendingBeatSnap set", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;

    // Feed LOUD with isTempoLocked=true, beatConfidence=0.6 to trigger pendingBeatSnap.
    // Use beat phase that wraps from ~0.99 to ~0.01 on next call.
    // First call sets pendingBeatSnap, second wraps phase → snapBeatNow=true.

    // First: one block with phase 0.5 (trigger allowPlayback→pendingBeatSnap)
    gate.update(StructureState::LOUD, 0.5, true, true, 0.8f, 512, sr);

    // Now advance phase past 1.0 (wrap: phase 0.01 < prev 0.99) → snapBeatNow
    GateDecision gd = gate.update(StructureState::LOUD, 0.01, true, true, 0.8f, 512, sr);
    REQUIRE(gd.snapBeatNow);
    REQUIRE(gd.gateOpen);
    REQUIRE(gate.isGateOpen());
}

TEST_CASE("PlaybackGate: reset() clears all state", "[playback_gate]")
{
    PlaybackGate gate;
    const double sr = 44100.0;
    // Open the gate first
    feedLoud(gate, sr, static_cast<int>(2.0 * sr), 0.0, true, true, 0.9f);
    // Gate may be open now; reset should clear everything
    gate.reset();
    REQUIRE_FALSE(gate.isGateOpen());
    // After reset, a single SILENT block should not produce resetTrackers
    GateDecision gd = gate.update(StructureState::SILENT, 0.0, false, false, 0.0f, 512, sr);
    REQUIRE_FALSE(gd.resetTrackers);
}

TEST_CASE("PlaybackGate: GateDecision has all four boolean fields", "[playback_gate]")
{
    GateDecision gd;
    // Verify default values
    REQUIRE_FALSE(gd.gateOpen);
    REQUIRE_FALSE(gd.snapBeatNow);
    REQUIRE_FALSE(gd.armCrash);
    REQUIRE_FALSE(gd.resetTrackers);
}
