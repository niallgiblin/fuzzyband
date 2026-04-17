#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "analysis/PitchEstimator.h"

TEST_CASE("PitchEstimator tracks 440 Hz sine near MIDI 69 (±0.25 semitone)", "[pitch]")
{
    const double sr = 48000.0;
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        buf[static_cast<size_t>(i)] = static_cast<float>(0.8 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
    }

    PitchEstimator est;
    est.prepare(sr, n);
    est.process(buf.data(), n);

    const float midi = est.getMidiNote();
    REQUIRE(midi > 69.0f - 0.25f);
    REQUIRE(midi < 69.0f + 0.25f);
    REQUIRE(est.getConfidence() > 0.1f);
}

TEST_CASE("PitchEstimator silence yields low confidence", "[pitch]")
{
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);

    PitchEstimator est;
    est.prepare(48000.0, n);
    est.process(buf.data(), n);

    REQUIRE(est.getConfidence() < 0.2f);
}
