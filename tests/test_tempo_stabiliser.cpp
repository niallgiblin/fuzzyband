#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "analysis/TempoStabiliser.h"

TEST_CASE("TempoStabiliser: initial stable BPM is 120", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    REQUIRE(ts.getStableBpm() == 120.0f);
}

TEST_CASE("TempoStabiliser: reset returns to 120 from any state", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    const double sr = 44100.0;

    // Drive beyond hold period to commit to 130
    const int holdSamples = static_cast<int>(2.0 * sr);
    int accumulated = 0;
    while (accumulated < holdSamples + 1)
    {
        ts.update(130.0f, true, 512, sr);
        accumulated += 512;
    }
    REQUIRE(ts.getStableBpm() == 130.0f);

    ts.reset();
    REQUIRE(ts.getStableBpm() == 120.0f);
}

TEST_CASE("TempoStabiliser: within deadband returns EMA not raw candidate", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    // Candidate 124 is 4.0 BPM from stable 120 — exactly at deadband boundary
    // Should return EMA: 120 + 0.05*(124-120) = 120.2
    const float result = ts.update(124.0f, true, 512, 44100.0);
    // Result should be between 120 and 124 (EMA step), not jump to 124
    REQUIRE(result > 120.0f);
    REQUIRE(result < 124.0f);
    // Stable BPM advances by EMA
    REQUIRE(ts.getStableBpm() > 120.0f);
    REQUIRE(ts.getStableBpm() < 124.0f);
}

TEST_CASE("TempoStabiliser: outside deadband holds until 2s then commits", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    const double sr = 44100.0;
    const int blockSize = 512;
    const int holdSamples = static_cast<int>(2.0 * sr); // 88200 samples

    // Candidate 130 is 10 BPM from stable 120 — outside deadband
    // Should hold at 120 until hold period accumulates
    float lastResult = 0.0f;
    int accumulated = 0;
    bool committed = false;

    // Feed blocks but stop just before hold period to verify hold
    const int preHold = holdSamples - blockSize;
    while (accumulated < preHold)
    {
        lastResult = ts.update(130.0f, true, blockSize, sr);
        accumulated += blockSize;
    }
    // Should still be holding at original stable BPM (120)
    REQUIRE(lastResult == 120.0f);

    // One more block pushes past hold threshold — should commit
    lastResult = ts.update(130.0f, true, blockSize, sr);
    accumulated += blockSize;
    REQUIRE(accumulated > holdSamples);
    committed = (lastResult == 130.0f);
    REQUIRE(committed);
    REQUIRE(ts.getStableBpm() == 130.0f);
}

TEST_CASE("TempoStabiliser: pre-gate snap (playbackOpen=false) returns candidate directly", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    // When playback gate is closed, candidate returned without hold
    const float result = ts.update(150.0f, false, 512, 44100.0);
    REQUIRE(result == 150.0f);
}

TEST_CASE("TempoStabiliser: large candidate change resets pending counter", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    const double sr = 44100.0;
    const int blockSize = 1000;

    // Accumulate some samples toward 130 commit (outside deadband from 120)
    // But don't complete the full hold
    ts.update(130.0f, true, blockSize, sr); // accumulated = 1000

    // Now jump to 200 — |200 - 130| = 70 > 4 deadband → counter resets
    // This means hold period starts over for 200
    const float result = ts.update(200.0f, true, blockSize, sr);
    // After counter reset, we're at the start of hold period for 200
    // So result should still be the old stable (120)
    REQUIRE(result == 120.0f);
}

TEST_CASE("TempoStabiliser: clamped candidate stays within 80-220 range", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    // Below-range candidate clamped to 80 — within deadband of 120? No, |80-120|=40 > 4
    // So goes into pending for 80
    const float result1 = ts.update(50.0f, false, 512, 44100.0);
    REQUIRE(result1 == 80.0f); // clamped

    // Above-range candidate clamped to 220
    const float result2 = ts.update(300.0f, false, 512, 44100.0);
    REQUIRE(result2 == 220.0f); // clamped
}

TEST_CASE("TempoStabiliser: warmStart sets stable BPM immediately", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    ts.warmStart(140.0f);
    REQUIRE(ts.getStableBpm() == 140.0f);
}

TEST_CASE("TempoStabiliser: warmStart primes EMA anchor for deadband candidates", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    ts.warmStart(140.0f);
    REQUIRE(ts.getStableBpm() == 140.0f);

    // Candidate 142 is within deadband of 140: |142-140| = 2 ≤ 4
    // EMA from warm-started 140: 140 + 0.05*(142-140) = 140.1
    const float result = ts.update(142.0f, true, 512, 44100.0);
    REQUIRE(result > 140.0f);
    REQUIRE(result < 142.0f);
    REQUIRE(ts.getStableBpm() > 140.0f);
    REQUIRE(ts.getStableBpm() < 142.0f);
}

TEST_CASE("TempoStabiliser: warmStart with outside-deadband candidate holds then commits", "[tempo_stabiliser]")
{
    TempoStabiliser ts;
    ts.warmStart(100.0f);
    REQUIRE(ts.getStableBpm() == 100.0f);

    const double sr = 44100.0;
    const int blockSize = 512;
    const int holdSamples = static_cast<int>(2.0 * sr);

    // Candidate 160 is 60 BPM from warm-started 100 — outside deadband
    // Should hold at 100 until hold period accumulates
    float lastResult = 0.0f;
    int accumulated = 0;

    // Feed blocks but stop just before hold period
    const int preHold = holdSamples - blockSize;
    while (accumulated < preHold)
    {
        lastResult = ts.update(160.0f, true, blockSize, sr);
        accumulated += blockSize;
    }
    REQUIRE(lastResult == 100.0f); // still holding

    // One more block pushes past hold threshold — should commit to 160
    lastResult = ts.update(160.0f, true, blockSize, sr);
    accumulated += blockSize;
    REQUIRE(accumulated > holdSamples);
    REQUIRE(lastResult == 160.0f);
    REQUIRE(ts.getStableBpm() == 160.0f);
}
