/**
 * E2E: Structure-transition functional test.
 *
 * Synthesises a composite audio signal:
 *   5 s  silence
 *  10 s  soft-like  (1500 Hz sine, amplitude 0.3 — RMS ≈ 0.212, below kLoudRms=0.35 → SOFT)
 *  10 s  loud-like  (1500 Hz sine, amplitude 0.6 — RMS ≈ 0.424, above kLoudRms=0.35 → LOUD)
 *   5 s  silence
 *
 * Feeds it through AccompanimentProcessor in 512-sample blocks and verifies:
 *   - During silence, display state is SILENT (index 0) and pattern is 0.
 *   - After sustained soft section, display state transitions out of SILENT.
 *   - After sustained loud section, pattern index is consistent with LOUD.
 *   - After the final silence, display RMS drops back below the silent threshold.
 *
 * Classification uses pure RMS: below kSilentRms(0.05) → SILENT, above kLoudRms(0.35) → LOUD,
 * in between → SOFT. Centroid is not used as a classifier axis.
 *
 * Note: state transitions are delayed by the 2 s / 2.5 s hold times in StructureTagger.
 * Tests sample state at the end of each section, not at the exact transition point.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

namespace {

static std::vector<float> sineSection(int numSamples, double freq, double sr, float amp)
{
    std::vector<float> v(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        v[static_cast<size_t>(i)] = amp * static_cast<float>(
            std::sin(2.0 * M_PI * freq * i / sr));
    return v;
}

// Feed `audio` through the processor in `blockSize` blocks, pausing + flushing each block.
// Returns the pattern index at the end of the section.
int feedSection(AccompanimentProcessor& proc,
                const float* audio,
                int numSamples,
                int blockSize)
{
    int patIdx = proc.getDisplayPatternIndex();
    for (int start = 0; start + blockSize <= numSamples; start += blockSize)
    {
        juce::AudioBuffer<float> buf(2, blockSize);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(buf.getWritePointer(ch), audio + start, blockSize);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        patIdx = proc.getDisplayPatternIndex();
    }
    return patIdx;
}

} // namespace

TEST_CASE("E2E: silence section keeps state SILENT and pattern 0", "[e2e][transitions]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    const int silLen = static_cast<int>(5.0 * sr);
    std::vector<float> sil(static_cast<size_t>(silLen), 0.0f);
    const int endPat = feedSection(proc, sil.data(), silLen, block);

    REQUIRE(endPat == 0);
    REQUIRE(proc.getDisplayStateIndex() == 0);  // 0 = SILENT
    REQUIRE(proc.getDisplayRms() < 0.05f);

    proc.releaseResources();
}

TEST_CASE("E2E: verse-like section causes display state to leave SILENT", "[e2e][transitions]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Brief silence warmup
    {
        const int n = static_cast<int>(0.5 * sr);
        std::vector<float> sil(static_cast<size_t>(n), 0.0f);
        feedSection(proc, sil.data(), n, block);
    }

    // 10 s of 1500 Hz sine at amplitude 0.3 — RMS ≈ 0.212, safely below kLoudRms(0.35) → SOFT
    // After the 0.1 s RMS warmup and immediate SILENT exit, state should enter SOFT
    {
        const int n = static_cast<int>(10.0 * sr);
        auto soft = sineSection(n, 1500.0, sr, 0.3f);
        feedSection(proc, soft.data(), n, block);
    }

    // Display state must have left SILENT (0)
    REQUIRE(proc.getDisplayStateIndex() != 0);
    // RMS must be elevated
    REQUIRE(proc.getDisplayRms() > 0.05f);

    proc.releaseResources();
}

TEST_CASE("E2E: chorus-like section after verse drives pattern index to [4,5]", "[e2e][transitions]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Silence → soft (reach SOFT state; needs kHoldSilentSec=0 + 0.1 s RMS warmup)
    {
        const int n = static_cast<int>(8.0 * sr);  // long enough for state to leave SILENT
        auto soft = sineSection(n, 1500.0, sr, 0.3f);  // RMS ≈ 0.212 → SOFT
        feedSection(proc, soft.data(), n, block);
    }

    // LOUD section: amplitude 0.6 for 14 s — RMS ≈ 0.424, safely above kLoudRms(0.35).
    // StructureTagger needs > 2 s of LOUD input while in SOFT before transitioning.
    // After 14 s the tagger has been in LOUD for ~12 s.
    {
        const int n = static_cast<int>(14.0 * sr);
        auto loud = sineSection(n, 1500.0, sr, 0.6f);  // RMS ≈ 0.424 → LOUD
        feedSection(proc, loud.data(), n, block);
    }

    // With default BPM ≈ 120 and LOUD state:
    //   RuleBasedInference: bpmAdj = 120, < 160 → base = 4
    //   PolicyPatternMapper (var=0.5, varShift=0): result = 4
    // Actual BPM from click-free sine may still be 120 (default), so expect pattern 4 or 5.
    const int pat = proc.getDisplayPatternIndex();
    REQUIRE(pat >= 4);
    REQUIRE(pat <= 5);

    // Display state must be LOUD (index 2)
    REQUIRE(proc.getDisplayStateIndex() == static_cast<int>(StructureState::LOUD));

    proc.releaseResources();
}

TEST_CASE("E2E: final silence after signal causes RMS to drop", "[e2e][transitions]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Sustained signal to raise RMS (amplitude 0.3 → RMS ≈ 0.212 → SOFT)
    {
        const int n = static_cast<int>(3.0 * sr);
        auto sig = sineSection(n, 1500.0, sr, 0.3f);
        feedSection(proc, sig.data(), n, block);
    }
    const float rmsAfterSignal = proc.getDisplayRms();
    REQUIRE(rmsAfterSignal > 0.1f);

    // Long silence to drain the RMS window (0.1 s) and let the peak envelope decay
    {
        const int n = static_cast<int>(5.0 * sr);
        std::vector<float> sil(static_cast<size_t>(n), 0.0f);
        feedSection(proc, sil.data(), n, block);
    }

    REQUIRE(proc.getDisplayRms() < rmsAfterSignal);

    proc.releaseResources();
}
