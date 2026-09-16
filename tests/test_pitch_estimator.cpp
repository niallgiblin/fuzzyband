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

TEST_CASE("PitchEstimator: fixed-hop estimates track a low E2 sine at every hop",
          "[pitch][hop]")
{
    const double sr = 48000.0;
    const int block = 512;
    PitchEstimator est;
    est.prepare(sr, block);
    std::vector<float> buf(static_cast<size_t>(block), 0.0f);
    int64_t abs = 0;
    int good = 0, bad = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const double t = static_cast<double>(abs + i) / sr;
            buf[static_cast<size_t>(i)] =
                static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 82.407 * t));
        }
        est.process(buf.data(), block);
        abs += block;
        if (b < 40)
            continue;
        for (int h = 0; h < est.getHopCount(); ++h)
        {
            if (est.getHopConf(h) <= 0.3f)
                continue;
            const int pc = ((static_cast<int>(std::lround(est.getHopMidi(h))) % 12) + 12) % 12;
            if (pc == 4) ++good; else ++bad;
        }
    }
    INFO("E2 hops: good=" << good << " bad=" << bad);
    REQUIRE(good > 0);
    REQUIRE(bad == 0);
}
