/**
 * E2E: Feed a synthesised 120 BPM click track through AccompanimentProcessor and
 * assert that getDisplayBpm() converges to 120 ± 5 BPM after a warmup period.
 *
 * Audio source: synthesised in memory (no external WAV file required).
 *
 * Convergence criteria from PROJECT.md: "Tempo within ±5 BPM".
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

TEST_CASE("E2E: 120 BPM click train converges BPM estimate to within ±5 BPM", "[e2e][bpm]")
{
    const double sr      = 48000.0;
    const int    block   = 512;
    const float  target  = 120.0f;

    // Period in samples for 120 BPM at 48 kHz
    const int period = static_cast<int>(std::round(sr * 60.0 / static_cast<double>(target)));

    // Synthesise ~12 s of click train (24 periods × period samples)
    const int numPeriods = 24;
    const int totalSamples = period * numPeriods;

    std::vector<float> monoClick(static_cast<size_t>(totalSamples), 0.0f);
    for (int i = 0; i < totalSamples; i += period)
        monoClick[static_cast<size_t>(i)] = 1.0f;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    int64_t pos = 0;
    for (int start = 0; start + block <= totalSamples; start += block)
    {
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(
                buf.getWritePointer(ch),
                monoClick.data() + start,
                block);

        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        pos += block;
    }

    const float bpm = proc.getDisplayBpm();

    // PROJECT.md accuracy requirement: tempo within ±5 BPM
    REQUIRE(bpm >= target - 5.0f);
    REQUIRE(bpm <= target + 5.0f);

    proc.releaseResources();
}
