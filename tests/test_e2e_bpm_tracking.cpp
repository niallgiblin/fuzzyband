/**
 * E2E: Verify that BPM stays stable (from knob/DAW transport) through audio processing.
 *
 * BPM now comes from manual BPM knob or DAW transport, not auto-detection.
 * Test that displayBpm reflects the APVTS parameter and stays stable.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

namespace {

void feedAudio(AccompanimentProcessor& proc,
               const std::vector<float>& buf, int numSamples,
               double /*sr*/, int block)
{
    for (int start = 0; start + block <= numSamples; start += block)
    {
        juce::AudioBuffer<float> b(2, block);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(
                b.getWritePointer(ch), buf.data() + start, block);

        juce::MidiBuffer midi;
        proc.processBlock(b, midi);
        proc.flushBackgroundInferenceForTests();
    }

    const int rem = numSamples % block;
    if (rem > 0)
    {
        const int start = numSamples - rem;
        juce::AudioBuffer<float> b(2, rem);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(
                b.getWritePointer(ch), buf.data() + start, rem);

        juce::MidiBuffer midi;
        proc.processBlock(b, midi);
        proc.flushBackgroundInferenceForTests();
    }
}

std::vector<float> makeSine(int total, double freq, double sr, float amp)
{
    std::vector<float> sig(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i)
        sig[static_cast<size_t>(i)] = amp * std::sin(2.0 * M_PI * freq * static_cast<double>(i) / sr);
    return sig;
}

}  // namespace

TEST_CASE("E2E: BPM stays stable at default 120 through processing", "[e2e][bpm]")
{
    const double sr    = 48000.0;
    const int    block = 512;
    const float  target = 120.0f;

    auto sig = makeSine(static_cast<int>(sr * 8.0), 440.0, sr, 0.02f);

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    feedAudio(proc, sig, static_cast<int>(sig.size()), sr, block);

    const float bpm = proc.getDisplayBpm();
    REQUIRE(bpm == target);

    proc.releaseResources();
}

TEST_CASE("E2E: BPM survives silence — uses knob value", "[e2e][bpm][reset]")
{
    const double sr    = 48000.0;
    const int    block = 512;
    const float  target = 120.0f;

    auto sig = makeSine(static_cast<int>(sr * 4.0), 440.0, sr, 0.02f);

    const int silenceSamples = static_cast<int>(sr * 2.5);
    std::vector<float> silence(static_cast<size_t>(silenceSamples), 0.0f);

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Phase 1 — playing
    feedAudio(proc, sig, static_cast<int>(sig.size()), sr, block);
    const float bpmAfterWarmup = proc.getDisplayBpm();
    CHECK(bpmAfterWarmup == target);

    // Phase 2 — silence
    feedAudio(proc, silence, silenceSamples, sr, block);
    const float bpmAfterSilence = proc.getDisplayBpm();
    CHECK(bpmAfterSilence == target);

    // Phase 3 — resume
    feedAudio(proc, sig, static_cast<int>(sig.size()), sr, block);
    const float bpmAfterResume = proc.getDisplayBpm();
    CHECK(bpmAfterResume == target);

    proc.releaseResources();
}
