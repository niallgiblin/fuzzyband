/**
 * Integration tests for AccompanimentProcessor — full plugin pipeline.
 *
 * Uses the test-only control hooks (pause / flush / resume background inference)
 * to exercise the processor deterministically without real-time threading.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>

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

TEST_CASE("Processor pipeline: clean DI phrase gap does not immediately return to SILENT", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const double sr = 48000.0;
    const int block = 512;

    // Low-level clean DI: raw sine amp 0.012 becomes scaled RMS around 0.034.
    auto cleanDi = makeSineBuffer(block, 110.0, sr, 0.012f);
    feedBlocks(proc, cleanDi, block, static_cast<int>(1.0 * sr) / block);
    proc.flushBackgroundInferenceForTests();
    REQUIRE(proc.getDisplayStateIndex() != static_cast<int>(StructureState::SILENT));

    auto silence = makeSilenceBuffer(block);
    feedBlocks(proc, silence, block, static_cast<int>(0.75 * sr) / block);
    proc.flushBackgroundInferenceForTests();

    REQUIRE(proc.getDisplayStateIndex() != static_cast<int>(StructureState::SILENT));

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: active signal opens accompaniment gate", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const double sr = 48000.0;
    const int block = 512;
    auto activeSignal = makeSineBuffer(block, 1500.0, sr, 0.5f);

    feedBlocks(proc, activeSignal, block, static_cast<int>(3.0 * sr) / block);
    proc.flushBackgroundInferenceForTests();

    bool sawNoteOn = false;
    for (int i = 0; i < static_cast<int>(4.0 * sr) / block; ++i)
    {
        juce::AudioBuffer<float> audio = activeSignal;
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
        for (const auto meta : midi)
        {
            if (meta.getMessage().isNoteOn())
                sawNoteOn = true;
        }
    }

    REQUIRE(sawNoteOn);

    proc.releaseResources();
}

// ─── State serialization round-trip ─────────────────────────────────────────

TEST_CASE("Processor pipeline: getStateInformation / setStateInformation round-trip", "[integration][pipeline]")
{
    AccompanimentProcessor procA;
    procA.prepareToPlay(48000.0, 512);

    // Set non-default parameter values via AudioProcessorParameter::setValue.
    if (auto* p = procA.getApvts().getParameter("intensity"))
        p->setValue(0.8f);

    juce::MemoryBlock data;
    procA.getStateInformation(data);
    REQUIRE(data.getSize() > 0);

    AccompanimentProcessor procB;
    procB.prepareToPlay(48000.0, 512);
    procB.setStateInformation(data.getData(), static_cast<int>(data.getSize()));

    // Continuous parameters should be within floating-point tolerance
    if (auto* inten = dynamic_cast<juce::AudioParameterFloat*>(
            procB.getApvts().getParameter("intensity")))
        REQUIRE(std::abs(inten->get() - 0.8f) < 0.01f);

    procA.releaseResources();
    procB.releaseResources();
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
    REQUIRE(bpm >= 40.0f);
    REQUIRE(bpm <= 300.0f);

    proc.releaseResources();
}

// ─── BPM display ─────────────────────────────────────────────────────────────

TEST_CASE("Processor pipeline: BPM display stays in [40, 300] at all times", "[integration][pipeline]")
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
    REQUIRE(bpm >= 40.0f);
    REQUIRE(bpm <= 300.0f);

    proc.releaseResources();
}

// ─── Rejection signal ───────────────────────────────────────────────────────

TEST_CASE("Processor rejection changes displayPatternIndex", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    // Feed an active signal to establish a non-silent pattern. SILENT has only pattern 0
    // as a compatible class, so rejection cannot cycle to another pattern there.
    feedBlocks(proc, makeSineBuffer(512, 1500.0, 48000.0, 0.5f), 512, 300);
    proc.flushBackgroundInferenceForTests();
    const int idxBefore = proc.getDisplayPatternIndex();
    REQUIRE(idxBefore > 0);

    // Feed one more block so the queue has a fresh feature for the next flush
    feedBlocks(proc, makeSineBuffer(512, 1500.0, 48000.0, 0.5f), 512, 1);

    // Trigger rejection — should exclude the current pattern for one cycle
    proc.patternRejectionCount.store(1, std::memory_order_release);

    proc.flushBackgroundInferenceForTests();
    const int idxAfter = proc.getDisplayPatternIndex();

    REQUIRE(idxAfter != idxBefore);
    REQUIRE(idxAfter >= 1);
    REQUIRE(idxAfter <= 6);

    // Rejection should be consumed (single-shot)
    const int remainingRejection = proc.patternRejectionCount.load(std::memory_order_acquire);
    REQUIRE(remainingRejection == 0);

    proc.releaseResources();
}

// ─── Session backward-compat: pre-v0.4.0 XML with genre attribute ─────────────

TEST_CASE("Session XML round-trip: pre-v0.4.0 genre attribute ignored", "[integration][pipeline]")
{
    // Step 1: Get real serialized state as a ValueTree, then inject pre-v0.4.0 params.
    AccompanimentProcessor procSrc;
    procSrc.prepareToPlay(48000.0, 512);
    if (auto* p = procSrc.getApvts().getParameter("intensity"))
        p->setValue(0.6f);
    if (auto* p = procSrc.getApvts().getParameter("structureBlend"))
        p->setValue(0.3f);

    // Force APVTS state tree to reflect setValue before serialising
    procSrc.getApvts().state.sendPropertyChangeMessage("intensity");

    juce::MemoryBlock baseData;
    procSrc.getStateInformation(baseData);
    REQUIRE(baseData.getSize() > 0);

    // Parse the baseline ValueTree and inject pre-v0.4.0 unknown properties.
    // JUCE APVTS uses ValueTree::readFromData / writeToStream (binary, not XML).
    auto tree = juce::ValueTree::readFromData(baseData.getData(), baseData.getSize());
    REQUIRE(tree.isValid());

    // Inject pre-v0.4.0 properties that no longer exist in the APVTS.
    // JUCE silently ignores unknown ValueTree properties on deserialization.
    tree.setProperty("genre", "0.75", nullptr);
    tree.setProperty("variation", "0.2", nullptr);

    // Step 2: Serialise the tampered ValueTree back to a MemoryBlock.
    juce::MemoryBlock tamperedData;
    juce::MemoryOutputStream tamperedStream(tamperedData, false);
    tree.writeToStream(tamperedStream);

    // Step 3: Load tampered state into a fresh processor — must not crash.
    AccompanimentProcessor procDst;
    procDst.prepareToPlay(48000.0, 512);
    procDst.setStateInformation(tamperedData.getData(), static_cast<int>(tamperedData.getSize()));

    // Step 4: Surviving parameters must load correctly.
    if (auto* inten = dynamic_cast<juce::AudioParameterFloat*>(
            procDst.getApvts().getParameter("intensity")))
        REQUIRE(std::abs(inten->get() - 0.6f) < 0.01f);

    if (auto* blend = dynamic_cast<juce::AudioParameterFloat*>(
            procDst.getApvts().getParameter("structureBlend")))
        REQUIRE(std::abs(blend->get() - 0.3f) < 0.01f);

    // Step 5: Plugin must be in a consistent state (pattern index in valid range).
    REQUIRE(procDst.getDisplayPatternIndex() >= 0);
    REQUIRE(procDst.getDisplayPatternIndex() <= 6);

    procSrc.releaseResources();
    procDst.releaseResources();
}
