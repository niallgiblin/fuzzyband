#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <cmath>

#include "analysis/StablePitchTracker.h"

// Helper: accumulate enough blocks to reach the 1/8-beat stability window at
// 120 BPM, sr=44100. One 1/8 beat = 60/120/8 * 44100 = 2756.25 samples; with
// 512-sample blocks the counter crosses it on the 6th block.
static constexpr double kTestSr       = 44100.0;
static constexpr float  kTestBpm      = 120.0f;
static constexpr int    kBlockSize    = 512;
static constexpr int    kBlocksPerWindow = 6;

// Feed the tracker a constant pitch until it returns a stable offset.
static int runToStable(StablePitchTracker& tracker, float midi, float conf = 0.8f)
{
    int result = INT_MIN;
    for (int i = 0; i < kBlocksPerWindow + 10; ++i)
    {
        result = tracker.update(midi, conf, kTestBpm, kBlockSize, kTestSr, false);
        if (result != INT_MIN)
            break;
    }
    return result;
}

TEST_CASE("StablePitchTracker: silence resets state and returns INT_MIN", "[stable_pitch]")
{
    StablePitchTracker tracker;
    // Feed valid C2 (MIDI 36) for many blocks to establish stability, then silence
    (void)runToStable(tracker, 36.0f);

    // Now silence — should reset and return INT_MIN
    const int result = tracker.update(36.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, true);
    REQUIRE(result == INT_MIN);

    // After silence, starting fresh with valid C: counter starts from zero
    // One block should not be enough to trigger update
    const int afterSilence = tracker.update(36.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
    REQUIRE(afterSilence == INT_MIN);
}

TEST_CASE("StablePitchTracker: confidence below threshold returns INT_MIN", "[stable_pitch]")
{
    StablePitchTracker tracker;
    // Low confidence — rawConf=0.1 < 0.20 fast-response threshold
    const int result = tracker.update(45.0f, 0.1f, kTestBpm, kBlockSize, kTestSr, false);
    REQUIRE(result == INT_MIN);
}

TEST_CASE("StablePitchTracker: stability window required before update", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // A = MIDI 45 (A2), pc=9; C-anchored delta = 9
    const float aMidi = 45.0f;

    int lastResult = INT_MIN;
    int blocksUntilUpdate = 0;
    for (int i = 0; i < kBlocksPerWindow + 10; ++i)
    {
        lastResult = tracker.update(aMidi, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        if (lastResult != INT_MIN)
        {
            blocksUntilUpdate = i + 1;
            break;
        }
    }

    // Should have gotten an update (not still INT_MIN after many blocks)
    REQUIRE(lastResult != INT_MIN);
    // A2 (pc 9) → offset 9 (folded onto C2 by the processor → bass A2 = 45)
    REQUIRE(lastResult == 9);
    // Must have taken at least some blocks (not immediate)
    REQUIRE(blocksUntilUpdate > 1);
}

TEST_CASE("StablePitchTracker: octave-flip tolerance — E2 then E3 keeps counter", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // Feed E2 (MIDI 40, pc=4) for half the required samples
    const int halfBlocks = kBlocksPerWindow / 2;
    for (int i = 0; i < halfBlocks; ++i)
    {
        const int r = tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        // Should not have fired yet
        REQUIRE(r == INT_MIN);
    }

    // Now switch to E3 (MIDI 52, pc=4) — same pitch class, counter should NOT reset
    const int result = runToStable(tracker, 52.0f);
    // E pc=4 → offset 4 (bass root E2 = 40)
    REQUIRE(result == 4);
}

TEST_CASE("StablePitchTracker: C (MIDI 36, pc=0) returns offset 0 (drop-C root)", "[stable_pitch]")
{
    StablePitchTracker tracker;
    const int result = runToStable(tracker, 36.0f);
    REQUIRE(result == 0);
}

TEST_CASE("StablePitchTracker: G (MIDI 43, pc=7) returns 7 — no octave-down wrap", "[stable_pitch]")
{
    // Regression: the old ±6 wrap folded pc≥7 an octave down (G→bass B1 = 35,
    // below the audible range of many bass VSTs). With the [0,11] contract the
    // processor folds G onto G2 = 43 instead.
    StablePitchTracker tracker;
    const int result = runToStable(tracker, 43.0f);
    REQUIRE(result == 7);
}

TEST_CASE("StablePitchTracker: Bb (pc=10) returns 10", "[stable_pitch]")
{
    StablePitchTracker tracker;
    const int result = runToStable(tracker, 46.0f); // Bb2
    REQUIRE(result == 10);
}

TEST_CASE("StablePitchTracker: B (pc=11) returns 11", "[stable_pitch]")
{
    StablePitchTracker tracker;
    const int result = runToStable(tracker, 47.0f); // B2
    REQUIRE(result == 11);
}

TEST_CASE("StablePitchTracker: reset() clears all state", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // Build up stable state (C2 for > window)
    (void)runToStable(tracker, 36.0f);

    // Reset
    tracker.reset();

    // Now a single block should return INT_MIN (counter cleared)
    const int result = tracker.update(36.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
    REQUIRE(result == INT_MIN);
}

TEST_CASE("StablePitchTracker: pitch class change resets counter", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // Accumulate half a window of E (MIDI 40)
    const int halfBlocks = kBlocksPerWindow / 2;
    for (int i = 0; i < halfBlocks; ++i)
        tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);

    // Switch to A (different pitch class, MIDI 45, pc=9) — counter resets
    int result = INT_MIN;
    int count = 0;
    for (int i = 0; i < kBlocksPerWindow * 2 + 10; ++i)
    {
        result = tracker.update(45.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        ++count;
        if (result != INT_MIN)
            break;
    }

    // Should fire at offset 9 for A
    REQUIRE(result == 9);
    // Should have taken a full stability window from when we switched to A
    REQUIRE(count >= kBlocksPerWindow);
}

TEST_CASE("StablePitchTracker: E (MIDI 40, pc=4) returns offset 4", "[stable_pitch]")
{
    StablePitchTracker tracker;
    const int result = runToStable(tracker, 40.0f);
    REQUIRE(result == 4);
}
