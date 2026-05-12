#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <cmath>

#include "analysis/StablePitchTracker.h"

// Helper: accumulate enough blocks to reach the one-beat window at 120 BPM, sr=44100
// One beat = 60/120 * 44100 = 22050 samples
// Using 512-sample blocks: ceil(22050/512) = 44 blocks needed

static constexpr double kTestSr    = 44100.0;
static constexpr float  kTestBpm   = 120.0f;
static constexpr int    kBlockSize = 512;
// One beat = (60/120)*44100 = 22050 samples => 44 blocks of 512 = 22528 samples >= 22050
static constexpr int    kBlocksPerBeat = 44;

TEST_CASE("StablePitchTracker: silence resets state and returns INT_MIN", "[stable_pitch]")
{
    StablePitchTracker tracker;
    // Feed valid E2 (MIDI 40) for many blocks to establish stability, then silence
    for (int i = 0; i < kBlocksPerBeat + 5; ++i)
    {
        int result = tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        (void)result;
    }

    // Now silence — should reset and return INT_MIN
    const int result = tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, true);
    REQUIRE(result == INT_MIN);

    // After silence, starting fresh with valid E: counter starts from zero
    // One block should not be enough to trigger update
    const int afterSilence = tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
    REQUIRE(afterSilence == INT_MIN);
}

TEST_CASE("StablePitchTracker: confidence below threshold returns INT_MIN", "[stable_pitch]")
{
    StablePitchTracker tracker;
    // Low confidence — rawConf=0.2 < 0.35 threshold
    const int result = tracker.update(45.0f, 0.2f, kTestBpm, kBlockSize, kTestSr, false);
    REQUIRE(result == INT_MIN);
}

TEST_CASE("StablePitchTracker: one-beat stability window required before update", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // A = MIDI 69 (or 45 for A2), pc=9; bassRootPc=4; delta=5
    // Use A2 = MIDI 45 for clarity (pc = 45%12 = 9)
    const float aMidi = 45.0f;

    // Feed blocks one at a time; only the block that crosses oneBeatSamples threshold should return 5
    int lastResult = INT_MIN;
    int blocksUntilUpdate = 0;
    for (int i = 0; i < kBlocksPerBeat + 10; ++i)
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
    // Should be delta = 9 - 4 = 5
    REQUIRE(lastResult == 5);
    // Must have taken at least some blocks (not immediate)
    REQUIRE(blocksUntilUpdate > 1);
}

TEST_CASE("StablePitchTracker: octave-flip tolerance — E2 then E3 keeps counter", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // Feed E2 (MIDI 40, pc=4) for half the required samples
    const int halfBlocks = kBlocksPerBeat / 2;
    for (int i = 0; i < halfBlocks; ++i)
    {
        const int r = tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        // Should not have fired yet
        REQUIRE(r == INT_MIN);
    }

    // Now switch to E3 (MIDI 52, pc=4) — same pitch class, counter should NOT reset
    // We need remaining blocks to cross the window
    int result = INT_MIN;
    for (int i = 0; i < kBlocksPerBeat + 5; ++i)
    {
        result = tracker.update(52.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        if (result != INT_MIN)
            break;
    }

    // Should fire (E is root, offset = 0)
    REQUIRE(result == 0);
}

TEST_CASE("StablePitchTracker: Bb (pc=10) returns delta=6 (at boundary, not clamped)", "[stable_pitch]")
{
    StablePitchTracker tracker;
    // Bb = MIDI 46 (pc = 46%12 = 10), or 58 (same pc)
    // delta = 10 - 4 = 6 (at boundary, stays 6)
    const float bbMidi = 46.0f; // Bb2

    int result = INT_MIN;
    for (int i = 0; i < kBlocksPerBeat + 10; ++i)
    {
        result = tracker.update(bbMidi, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        if (result != INT_MIN)
            break;
    }
    REQUIRE(result == 6);
}

TEST_CASE("StablePitchTracker: B (pc=11) wraps via delta-12 to return -5", "[stable_pitch]")
{
    StablePitchTracker tracker;
    // B = MIDI 47 (pc = 47%12 = 11)
    // delta = 11 - 4 = 7, > 6, so delta -= 12 => -5
    const float bMidi = 47.0f; // B2

    int result = INT_MIN;
    for (int i = 0; i < kBlocksPerBeat + 10; ++i)
    {
        result = tracker.update(bMidi, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        if (result != INT_MIN)
            break;
    }
    REQUIRE(result == -5);
}

TEST_CASE("StablePitchTracker: reset() clears all state", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // Build up stable state (E2 for >1 beat)
    for (int i = 0; i < kBlocksPerBeat + 5; ++i)
        tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);

    // Reset
    tracker.reset();

    // Now a single block should return INT_MIN (counter cleared)
    const int result = tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
    REQUIRE(result == INT_MIN);
}

TEST_CASE("StablePitchTracker: pitch class change resets counter", "[stable_pitch]")
{
    StablePitchTracker tracker;

    // Accumulate half beat of E (MIDI 40)
    const int halfBlocks = kBlocksPerBeat / 2;
    for (int i = 0; i < halfBlocks; ++i)
        tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);

    // Switch to A (different pitch class, MIDI 45, pc=9) — counter resets
    // We now need a full beat to fire for A
    int result = INT_MIN;
    int count = 0;
    for (int i = 0; i < kBlocksPerBeat * 2 + 10; ++i)
    {
        result = tracker.update(45.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        ++count;
        if (result != INT_MIN)
            break;
    }

    // Should fire at delta=5 for A
    REQUIRE(result == 5);
    // Should have taken a full beat worth of blocks from when we switched to A
    REQUIRE(count >= kBlocksPerBeat);
}

TEST_CASE("StablePitchTracker: E (MIDI 40, pc=4) returns semitone offset 0 (root)", "[stable_pitch]")
{
    StablePitchTracker tracker;

    int result = INT_MIN;
    for (int i = 0; i < kBlocksPerBeat + 10; ++i)
    {
        result = tracker.update(40.0f, 0.8f, kTestBpm, kBlockSize, kTestSr, false);
        if (result != INT_MIN)
            break;
    }
    REQUIRE(result == 0);
}
