#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "analysis/OnsetDetector.h"

TEST_CASE("OnsetDetector converges near 160 BPM on synthetic impulse train", "[onset]")
{
    OnsetDetector detector;
    const double sr = 44100.0;
    detector.prepare(sr, 512);

    const float bpmTarget = 160.0f;
    const int periodSamples = static_cast<int>(std::round(static_cast<double>(sr) * 60.0 / static_cast<double>(bpmTarget)));

    const int chunk = 256;
    const int totalSamples = periodSamples * 24;

    for (int pos = 0; pos < totalSamples; pos += chunk)
    {
        std::vector<float> buf(static_cast<size_t>(chunk), 0.0f);
        for (int i = 0; i < chunk; ++i)
        {
            const int g = pos + i;
            if (g % periodSamples == 0)
                buf[static_cast<size_t>(i)] = 1.0f;
        }
        detector.process(buf.data(), chunk);
    }

    const float bpm = detector.getCurrentBpm();
    REQUIRE(bpm > 150.0f);
    REQUIRE(bpm < 170.0f);
}
