/**
 * Integration tests for AccompanimentProcessor — full plugin pipeline.
 *
 * Uses the test-only control hooks (pause / flush / resume background inference)
 * to exercise the processor deterministically without real-time threading.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

// Build a 2-channel (stereo) buffer filled with silence.
juce::AudioBuffer<float> makeSilenceBuffer(int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    buf.clear();
    return buf;
}

// Build a 2-channel buffer containing a sine wave at the given frequency and amplitude.
juce::AudioBuffer<float> makeSineBuffer(int numSamples, double freq, double sr, float amplitude)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* ptr = buf.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            ptr[i] = amplitude * static_cast<float>(
                         std::sin(2.0 * M_PI * freq * i / sr));
    }
    return buf;
}

// Build a click-train buffer: unit impulse every `period` samples on both channels.
juce::AudioBuffer<float> makeClickBuffer(int numSamples, int period)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    buf.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        float* ptr = buf.getWritePointer(ch);
        for (int i = 0; i < numSamples; i += period)
            ptr[i] = 1.0f;
    }
    return buf;
}

// Feed numBlocks × blockSize samples of `audio` (wrapped circularly) through the processor.
void feedBlocks(AccompanimentProcessor& proc,
                const juce::AudioBuffer<float>& source,
                int blockSize,
                int numBlocks)
{
    juce::MidiBuffer midi;
    const int totalSrc = source.getNumSamples();
    int64_t pos = 0;
    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> block(2, blockSize);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const int srcIdx = static_cast<int>((pos + i) % totalSrc);
                block.setSample(ch, i, source.getSample(ch, srcIdx));
            }
        }
        proc.processBlock(block, midi);
        pos += blockSize;
    }
}

} // namespace

// ─── Silent pipeline ─────────────────────────────────────────────────────────

TEST_CASE("Processor pipeline: silence produces pattern 0 with no note-on MIDI", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const int blockSize = 512;
    const int numBlocks = 20;

    juce::MidiBuffer accMidi;
    auto silence = makeSilenceBuffer(blockSize);

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> block = makeSilenceBuffer(blockSize);
        juce::MidiBuffer midi;
        proc.processBlock(block, midi);
        accMidi.addEvents(midi, 0, -1, 0);
    }

    proc.flushBackgroundInferenceForTests();

    // No note-on events should have been generated for pure silence
    bool anyNoteOn = false;
    for (const auto meta : accMidi)
        if (meta.getMessage().isNoteOn())
            anyNoteOn = true;
    REQUIRE_FALSE(anyNoteOn);

    // Pattern index should remain at 0 (SILENT state → base 0 → policy with genre 0 → 0)
    REQUIRE(proc.getDisplayPatternIndex() == 0);

    proc.releaseResources();
}

// ─── Active signal pipeline ───────────────────────────────────────────────────

TEST_CASE("Processor pipeline: sustained signal raises RMS and changes display state", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const double sr       = 48000.0;
    const int    block    = 512;
    const int    numBlocks = 300; // ~3.2 s of audio — enough for StructureTagger to leave SILENT

    // 1500 Hz sine at amplitude 0.5: rmsEnergy ≈ 0.283 (> kSilentRms 0.05, < kLoudRms 0.35) → SOFT
    auto sigSrc = makeSineBuffer(block, 1500.0, sr, 0.5f);
    feedBlocks(proc, sigSrc, block, numBlocks);

    proc.flushBackgroundInferenceForTests();

    // After enough signal, the displayed RMS must exceed the silent threshold
    REQUIRE(proc.getDisplayRms() > 0.05f);

    // State should have advanced beyond SILENT (index 0 = SILENT)
    REQUIRE(proc.getDisplayStateIndex() != 0);

    proc.releaseResources();
}

// ─── State serialization round-trip ─────────────────────────────────────────

TEST_CASE("Processor pipeline: getStateInformation / setStateInformation round-trip", "[integration][pipeline]")
{
    AccompanimentProcessor procA;
    procA.prepareToPlay(48000.0, 512);

    // Set non-default parameter values via AudioProcessorParameter::setValue.
    // For AudioParameterChoice (5 options): normalized = index / (count-1).
    //   genre index 3 "Progressive" → 3/4 = 0.75
    // For AudioParameterFloat [0,1]: normalized value == actual value.
    if (auto* p = procA.getApvts().getParameter("genre"))
        p->setValue(0.75f); // index 3 of 5 choices

    if (auto* p = procA.getApvts().getParameter("intensity"))
        p->setValue(0.8f);

    if (auto* p = procA.getApvts().getParameter("variation"))
        p->setValue(0.2f);

    juce::MemoryBlock data;
    procA.getStateInformation(data);
    REQUIRE(data.getSize() > 0);

    AccompanimentProcessor procB;
    procB.prepareToPlay(48000.0, 512);
    procB.setStateInformation(data.getData(), static_cast<int>(data.getSize()));

    // Genre should survive the round-trip
    if (auto* g = dynamic_cast<juce::AudioParameterChoice*>(
            procB.getApvts().getParameter("genre")))
        REQUIRE(g->getIndex() == 3);

    // Continuous parameters should be within floating-point tolerance
    if (auto* inten = dynamic_cast<juce::AudioParameterFloat*>(
            procB.getApvts().getParameter("intensity")))
        REQUIRE(std::abs(inten->get() - 0.8f) < 0.01f);

    if (auto* var = dynamic_cast<juce::AudioParameterFloat*>(
            procB.getApvts().getParameter("variation")))
        REQUIRE(std::abs(var->get() - 0.2f) < 0.01f);

    procA.releaseResources();
    procB.releaseResources();
}

// ─── Parameter effect on pattern selection ────────────────────────────────────

TEST_CASE("Processor pipeline: changing genre shifts displayPatternIndex via PolicyPatternMapper", "[integration][pipeline]")
{
    // With SILENT input, RuleBasedInference returns base=0.
    // PolicyPatternMapper: (0 + genreIndex) % 7 (variation=0.5 → varShift=0).
    // genre 0 → 0,  genre 1 → 1.
    // So changing genre from 0 to 1 with silence input changes patternIndex from 0 to 1.

    AccompanimentProcessor procG0;
    procG0.prepareToPlay(48000.0, 512);
    procG0.pauseBackgroundInferenceForTests();
    feedBlocks(procG0, makeSilenceBuffer(512), 512, 5);
    procG0.flushBackgroundInferenceForTests();
    const int idxG0 = procG0.getDisplayPatternIndex();

    AccompanimentProcessor procG1;
    procG1.prepareToPlay(48000.0, 512);
    // genre index 1 "Metalcore" → normalized 1/4 = 0.25
    if (auto* p = procG1.getApvts().getParameter("genre"))
        p->setValue(0.25f);
    procG1.pauseBackgroundInferenceForTests();
    feedBlocks(procG1, makeSilenceBuffer(512), 512, 5);
    procG1.flushBackgroundInferenceForTests();
    const int idxG1 = procG1.getDisplayPatternIndex();

    // Genre 1 should produce a different (higher) index than genre 0 for silent input
    REQUIRE(idxG0 == 0);
    REQUIRE(idxG1 == 1);

    procG0.releaseResources();
    procG1.releaseResources();
}

// ─── Thread pause / resume safety ────────────────────────────────────────────

TEST_CASE("Processor pipeline: pause/flush/resume cycle does not corrupt state", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    // Pause background thread
    proc.pauseBackgroundInferenceForTests();

    // Feed 50 blocks of silence with background paused
    auto silence = makeSilenceBuffer(512);
    for (int b = 0; b < 50; ++b)
    {
        juce::AudioBuffer<float> block = makeSilenceBuffer(512);
        juce::MidiBuffer midi;
        proc.processBlock(block, midi);
    }

    // Synchronously drain the feature queue
    proc.flushBackgroundInferenceForTests();

    // Resume thread
    proc.resumeBackgroundInferenceForTests();

    // Processor must still be in a sane state — pattern index in [0, 6]
    const int patIdx = proc.getDisplayPatternIndex();
    REQUIRE(patIdx >= 0);
    REQUIRE(patIdx <= 6);

    // BPM must be in valid range
    const float bpm = proc.getDisplayBpm();
    REQUIRE(bpm >= 80.0f);
    REQUIRE(bpm <= 220.0f);

    proc.releaseResources();
}

// ─── BPM display ─────────────────────────────────────────────────────────────

TEST_CASE("Processor pipeline: BPM display stays in [80, 220] at all times", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    // Feed a click-train at 120 BPM (period = 24000 samples at 48 kHz)
    const int period = 24000;
    auto clicks = makeClickBuffer(period * 12, period); // 12 clicks
    feedBlocks(proc, clicks, 512, (period * 12) / 512);

    proc.flushBackgroundInferenceForTests();

    const float bpm = proc.getDisplayBpm();
    REQUIRE(bpm >= 80.0f);
    REQUIRE(bpm <= 220.0f);

    proc.releaseResources();
}
