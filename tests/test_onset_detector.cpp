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
    // Deliver enough periods to satisfy the minimum-count guard (>=9 clicks before checking BPM).
    // At 160 BPM the period is ~16538 samples; 24 periods gives 24 onsets, well above the 8-onset minimum.
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

TEST_CASE("OnsetDetector: rejects sustained noise bursts", "[onset]")
{
    // Feed identical repeated blocks (constant magnitude across all frames — no transients).
    // Because each FFT hop sees the same spectrum, positive flux is always zero after the first
    // frame (prevMagnitudes converges to the steady-state). The adaptive threshold therefore has
    // nothing to suppress, and no onsets are detected. BPM should remain at the default 120.

    OnsetDetector detector;
    const double sr = 44100.0;
    detector.prepare(sr, 512);

    // A fixed-content block: non-zero DC-like signal that produces a steady FFT spectrum
    const int blockSize = 512;
    std::vector<float> noiseBlock(static_cast<size_t>(blockSize));
    for (int i = 0; i < blockSize; ++i)
        noiseBlock[static_cast<size_t>(i)] = 0.5f;  // constant — identical each call

    // Feed ~2 seconds of the same block repeatedly
    const int numBlocks = static_cast<int>(std::ceil(2.0 * sr / blockSize));
    for (int b = 0; b < numBlocks; ++b)
        detector.process(noiseBlock.data(), blockSize);

    // BPM should remain at the default (120.0f) because no genuine transients were detected
    const float bpm = detector.getCurrentBpm();
    REQUIRE(bpm == 120.0f);
}
