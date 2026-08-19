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

TEST_CASE("PitchEstimator tracks drop-C low string (65.4 Hz ≈ MIDI 36)", "[pitch][dropc]")
{
    const double sr = 48000.0;
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        // Low C2 fundamental + a bit of second harmonic, like a palm-muted chug.
        buf[static_cast<size_t>(i)] = static_cast<float>(
            0.7 * std::sin(2.0 * 3.14159265358979323846 * 65.406 * t)
            + 0.25 * std::sin(2.0 * 3.14159265358979323846 * 130.812 * t));
    }

    PitchEstimator est;
    est.prepare(sr, n);
    est.process(buf.data(), n);

    const float midi = est.getMidiNote();
    INFO("detected midi: " << midi << " conf: " << est.getConfidence());
    REQUIRE(midi > 35.0f);   // C2 = 36, allow a semitone of error
    REQUIRE(midi < 38.0f);
    REQUIRE(est.getConfidence() > 0.1f);
}

TEST_CASE("PitchEstimator tracks A1 (55 Hz ≈ MIDI 33) — extended low range", "[pitch][dropc]")
{
    const double sr = 48000.0;
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        buf[static_cast<size_t>(i)] = static_cast<float>(
            0.7 * std::sin(2.0 * 3.14159265358979323846 * 55.0 * t));
    }

    PitchEstimator est;
    est.prepare(sr, n);
    est.process(buf.data(), n);

    const float midi = est.getMidiNote();
    INFO("detected midi: " << midi << " conf: " << est.getConfidence());
    REQUIRE(midi > 32.0f);
    REQUIRE(midi < 35.0f);
    REQUIRE(est.getConfidence() > 0.1f);
}
