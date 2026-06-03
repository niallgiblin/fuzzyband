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

namespace {

/** @brief Process @a numSamples of audio through @a proc in 512-sample blocks.
 *  @param buf  Mono buffer (length ≥ @a numSamples); copied to both channels per block.
 */
void feedAudio(AccompanimentProcessor& proc,
               const std::vector<float>& buf, int numSamples,
               double sr, int block)
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

    // Partial tail block
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

/** @brief Generate a mono click train at @a bpm for @a durationSeconds.
 *  Returns (signal, period in samples).
 */
std::pair<std::vector<float>, int>
synthClickTrain(float bpm, double durationSeconds, double sr)
{
    const int period = static_cast<int>(std::round(sr * 60.0 / static_cast<double>(bpm)));
    const int total  = static_cast<int>(sr * durationSeconds);
    std::vector<float> click(static_cast<size_t>(total), 0.0f);
    for (int i = 0; i < total; i += period)
        click[static_cast<size_t>(i)] = 1.0f;
    return {click, period};
}

}  // namespace

TEST_CASE("E2E: 120 BPM click train converges BPM estimate to within ±5 BPM", "[e2e][bpm]")
{
    const double sr    = 48000.0;
    const int    block = 512;
    const float  target = 120.0f;

    const auto [click, period] = synthClickTrain(target, 12.0, sr);
    (void)period;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    feedAudio(proc, click, static_cast<int>(click.size()), sr, block);

    const float bpm = proc.getDisplayBpm();

    // PROJECT.md accuracy requirement: tempo within ±5 BPM
    REQUIRE(bpm >= target - 5.0f);
    REQUIRE(bpm <= target + 5.0f);

    proc.releaseResources();
}

TEST_CASE("E2E: BPM survives silence-induced tracker reset (save/restore)", "[e2e][bpm][reset]")
{
    const double sr    = 48000.0;
    const int    block = 512;
    const float  target = 120.0f;

    // Establish tempo: 4 s of 120 BPM click → lock + stable BPM
    const auto [click, period] = synthClickTrain(target, 4.0, sr);
    (void)period;

    // Silence: 2.5 s (exceeds PlaybackGate phrase-breath hold of 2.0 s)
    const int silenceSamples = static_cast<int>(sr * 2.5);
    std::vector<float> silence(static_cast<size_t>(silenceSamples), 0.0f);

    // Resume click: 4 s
    const auto [click2, period2] = synthClickTrain(target, 4.0, sr);
    (void)period2;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Phase 1 — warmup
    feedAudio(proc, click, static_cast<int>(click.size()), sr, block);
    const float bpmAfterWarmup = proc.getDisplayBpm();
    CHECK(bpmAfterWarmup >= target - 5.0f);
    CHECK(bpmAfterWarmup <= target + 5.0f);

    // Phase 2 — silence (triggers PlaybackGate.resetTrackers)
    feedAudio(proc, silence, silenceSamples, sr, block);
    const float bpmAfterSilence = proc.getDisplayBpm();
    CHECK(bpmAfterSilence >= target - 5.0f);
    CHECK(bpmAfterSilence <= target + 5.0f);
    INFO("bpmAfterSilence = " << bpmAfterSilence);

    // Phase 3 — resume click
    feedAudio(proc, click2, static_cast<int>(click2.size()), sr, block);
    const float bpmAfterResume = proc.getDisplayBpm();
    CHECK(bpmAfterResume >= target - 5.0f);
    CHECK(bpmAfterResume <= target + 5.0f);

    proc.releaseResources();
}
