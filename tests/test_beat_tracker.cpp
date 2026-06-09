#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

#include "analysis/BeatTracker.h"

TEST_CASE("BeatTracker: impulse train settles near 120 BPM with usable confidence", "[bpm]")
{
    BeatTracker tracker;
    const double sr = 48000.0;
    tracker.prepare(sr, 512);

    constexpr int fftSize = 2048;
    const int hop = std::max(256, fftSize / 4); // Matches OnsetDetector defaults
    const double targetBpm = 120.0;
    double hopAccum = 0.0;
    const double hopsPerPulse =
        ((sr * 60.0) / targetBpm) / static_cast<double>(hop);

    int64_t t = 0;
    const int hops = 4000;
    for (int i = 0; i < hops; ++i)
    {
        hopAccum += 1.0;
        float flux = 0.0f;
        if (hopAccum >= hopsPerPulse)
        {
            hopAccum -= hopsPerPulse;
            flux = 2.0f;
        }
        tracker.pushFluxSample(flux, t);
        t += hop;
    }

    const float bpm = tracker.getBpm();
    REQUIRE(bpm >= 115.0f);
    REQUIRE(bpm <= 125.0f);
}

TEST_CASE("BeatTracker: silent OSF yields low confidence", "[bpm]")
{
    BeatTracker tracker;
    tracker.prepare(44100.0, 512);
    int64_t t = 0;
    for (int i = 0; i < 2500; ++i)
    {
        tracker.pushFluxSample(0.0f, t);
        t += 512;
    }
    REQUIRE(tracker.getConfidence() < 0.4f);
}

TEST_CASE("BeatTracker: folds near-double-time DI transients toward slower pulse", "[bpm]")
{
    BeatTracker tracker;
    const double sr = 48000.0;
    tracker.prepare(sr, 512);

    constexpr int fftSize = 2048;
    const int hop = std::max(256, fftSize / 4);
    const double doubleTimeBpm = 200.0;
    double hopAccum = 0.0;
    const double hopsPerPulse =
        ((sr * 60.0) / doubleTimeBpm) / static_cast<double>(hop);

    int64_t t = 0;
    for (int i = 0; i < 4000; ++i)
    {
        hopAccum += 1.0;
        float flux = 0.0f;
        if (hopAccum >= hopsPerPulse)
        {
            hopAccum -= hopsPerPulse;
            flux = 2.0f;
        }
        tracker.pushFluxSample(flux, t);
        t += hop;
    }

    REQUIRE(tracker.getBpm() >= 95.0f);
    REQUIRE(tracker.getBpm() <= 125.0f);
}
