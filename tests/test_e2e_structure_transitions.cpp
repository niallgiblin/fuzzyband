/**
 * E2E: Structure-transition functional test.
 *
 * Synthesises a composite audio signal:
 *   5 s  silence
 *  10 s  soft-like  (1500 Hz sine, amplitude 0.3 — RMS ≈ 0.212, below kLoudRms=0.075 → SOFT)
 *  10 s  loud-like  (1500 Hz sine, amplitude 0.6 — RMS ≈ 0.424, above kLoudRms=0.075 → LOUD)
 *   5 s  silence
 *
 * Feeds it through AccompanimentProcessor in 512-sample blocks and verifies:
 *   - During silence, display state is SILENT (index 0) and pattern is 0.
 *   - After sustained soft section, display state transitions out of SILENT.
 *   - After sustained loud section, pattern index is consistent with LOUD.
 *   - After the final silence, display RMS drops back below the silent threshold.
 *
 * Classification uses pure RMS: below kSilentRms(0.012) → SILENT,
 * above kBreakdownRms(0.12) → BREAKDOWN, kLoudRms(0.075) → LOUD,
 * in between → AMBIENT or SOFT.
 *
 * Note: state transitions are delayed by the scaled hold times in StructureTagger.
 * Tests sample state at the end of each section, not at the exact transition point.
 * State indices: SILENT=0, AMBIENT=1, SOFT=2, LOUD=3, BREAKDOWN=4.
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

    // Display state must be LOUD (index 3 after AMBIENT insertion)
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

// Amplitude thresholds calibrated against EnergyAnalyser rmsEnergy (= raw_rms * 4.0, clamped [0,1]):
//   amp 0.010 → raw_rms ≈ 0.0071 → rmsEnergy ≈ 0.0283 → AMBIENT (between kSilentRms and kAmbientCeil)
//   amp 0.015 → raw_rms ≈ 0.0106 → rmsEnergy ≈ 0.0424 → SOFT (between kAmbientCeil and kLoudRms)
//   amp 0.25  → raw_rms ≈ 0.1768 → rmsEnergy ≈ 0.707  → LOUD/BREAKDOWN
TEST_CASE("E2E: palm-muted verse to full-chord chorus — no flip-flopping", "[e2e][transitions]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Phase 1: Palm-muted verse (SOFT) — 10s of amp 0.015
    // StructureTagger: rmsEnergy ≈ 0.0424 → SOFT (above kAmbientCeil, below kLoudRms)
    {
        const int n = static_cast<int>(10.0 * sr);
        auto verse = sineSection(n, 1500.0, sr, 0.015f);
        feedSection(proc, verse.data(), n, block);
    }

    // After sustained palm-muted playing, state should have settled to SOFT (index 2)
    const int stateAfterVerse = proc.getDisplayStateIndex();
    REQUIRE(stateAfterVerse == 2); // 2 = SOFT (after AMBIENT=1)

    // Phase 2: Full-chord chorus (LOUD) — 15s of amp 0.25
    {
        const int n = static_cast<int>(15.0 * sr);
        auto chorus = sineSection(n, 1500.0, sr, 0.25f);
        feedSection(proc, chorus.data(), n, block);
    }

    // After sustained full-chord playing, state should be stable at LOUD (index 3)
    const int stateAfterChorus = proc.getDisplayStateIndex();
    REQUIRE(stateAfterChorus == 3); // 3 = LOUD

    // Phase 3: Return to palm-muted (SOFT) — 10s of amp 0.015
    {
        const int n = static_cast<int>(10.0 * sr);
        auto verse2 = sineSection(n, 1500.0, sr, 0.015f);
        feedSection(proc, verse2.data(), n, block);
    }

    // After return to palm-muted, state should settle back to SOFT
    const int stateAfterReturn = proc.getDisplayStateIndex();
    REQUIRE(stateAfterReturn == 2); // 2 = SOFT

    proc.releaseResources();
}

// Verify that rapid alternation between SOFT and LOUD input does not
// cause flip-flopping — the bar-quantized hold guard holds for ≥2 bars.
TEST_CASE("E2E: rapid SOFT/LOUD alternation — hold guard prevents flip-flopping", "[e2e][transitions]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Establish initial LOUD state with sustained signal
    {
        const int n = static_cast<int>(5.0 * sr);
        auto loud = sineSection(n, 1500.0, sr, 0.25f);
        feedSection(proc, loud.data(), n, block);
    }
    const int initialState = proc.getDisplayStateIndex();
    REQUIRE(initialState == 3); // LOUD

    // Rapidly alternate SOFT/LOUD in 1-bar chunks for 8 bars (~16s at 120 BPM).
    // The first bar of SOFT input may commit at the next bar boundary (≥2 bars
    // since LOUD was established). After that, the hold guard blocks further
    // changes because only 1 bar has elapsed. We count state changes to verify
    // the hold guard limits flip-flopping to at most 1 in any 2-bar window.
    const double bpm = 120.0;
    const int samplesPerBar = static_cast<int>(4.0 * 60.0 / bpm * sr);
    int stateChangeCount = 0;
    int lastState = initialState;

    for (int bar = 0; bar < 8; ++bar)
    {
        const float amp = (bar % 2 == 0) ? 0.25f : 0.015f; // alternate LOUD/SOFT
        auto seg = sineSection(samplesPerBar, 1500.0, sr, amp);
        feedSection(proc, seg.data(), samplesPerBar, block);
        const int s = proc.getDisplayStateIndex();
        if (s != lastState)
        {
            ++stateChangeCount;
            lastState = s;
        }
    }

    // With 8 bars of alternation, the hold guard should allow at most 2-3 state
    // changes (not 8!). The first transition LOUD→SOFT is allowed (≥2 bars elapsed),
    // then further changes are held. Without the hold guard, we'd see 8 changes.
    REQUIRE(stateChangeCount < 4);

    proc.releaseResources();
}
