/**
 * Long-duration stability test: 300+ seconds of continuous audio processing
 * with alternating SOFT/LOUD/SILENT sections simulating a human jam.
 *
 * Verifies the ONNX pipeline survives extended use without crashes, invalid
 * state transitions, or index-out-of-bounds pattern selections.
 *
 * Section layout (~310 seconds total):
 *   3 s    silence (warmup)
 *  15 × (
 *     2 s  silence gap
 *     8 s  SOFT  (amp 0.013, 1500 Hz sine)
 *     2 s  silence gap
 *     8 s  LOUD  (amp 0.25,  1500 Hz sine)
 *  )
 *   3 s    silence (cooldown)
 *
 * Invariants checked after every section:
 *   - patternIndex ∈ [0, 10]
 *   - BPM ∈ [kMinBpm, kMaxBpm] for non-silent sections
 *   - structure state is a valid enum (0–3)
 *   - no exceptions/crashes (implicit — test survives)
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

namespace {

static constexpr float kMinBpm = 40.0f;
static constexpr float kMaxBpm = 320.0f;

static std::vector<float> sineSection(int numSamples, double freq, double sr, float amp)
{
    std::vector<float> v(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        v[static_cast<size_t>(i)] = amp * static_cast<float>(
            std::sin(2.0 * M_PI * freq * i / sr));
    return v;
}

struct SectionResult
{
    int patternIndex;
    float bpm;
    int stateIndex;
    float rms;
};

static SectionResult feedSection(AccompanimentProcessor& proc,
                                 const float* audio,
                                 int numSamples,
                                 int blockSize)
{
    SectionResult res{};
    for (int start = 0; start + blockSize <= numSamples; start += blockSize)
    {
        juce::AudioBuffer<float> buf(2, blockSize);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(buf.getWritePointer(ch),
                                              audio + start, blockSize);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
    }
    res.patternIndex = proc.getDisplayPatternIndex();
    res.bpm          = proc.getDisplayBpm();
    res.stateIndex   = proc.getDisplayStateIndex();
    res.rms          = proc.getDisplayRms();
    return res;
}

static void checkInvariants(const SectionResult& r,
                            int section,
                            bool expectNonSilent,
                            const std::string& label)
{
    INFO("Section " << section << " [" << label << "]"
         << " pat=" << r.patternIndex
         << " bpm=" << r.bpm
         << " state=" << r.stateIndex
         << " rms=" << r.rms);

    // Pattern index must be in [0, 10] for the 11-pattern library
    CHECK((r.patternIndex >= 0 && r.patternIndex <= 10));

    // BPM must be in valid range for non-silent sections
    if (expectNonSilent)
    {
        CHECK((r.bpm >= kMinBpm && r.bpm <= kMaxBpm));
    }

    // Structure state must be a valid enum (0=SILENT, 1=VERSE, 2=CHORUS, 3=BREAKDOWN)
    CHECK((r.stateIndex >= 0 && r.stateIndex <= 3));
}

} // namespace

TEST_CASE("Long-duration stability: 300+ seconds continuous processing", "[stability][long]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Track some aggregate stats
    int    totalSections  = 0;
    int    nonSilentCount = 0;
    float  minBpmSeen     = 999.0f;
    float  maxBpmSeen     = 0.0f;
    int    minPatSeen     = 999;
    int    maxPatSeen     = -1;
    int    statesSeen[4]  = {0, 0, 0, 0};   // SILENT, VERSE, CHORUS, BREAKDOWN

    auto runSilence = [&](double dur)
    {
        const int n = static_cast<int>(dur * sr);
        std::vector<float> sil(static_cast<size_t>(n), 0.0f);
        auto res = feedSection(proc, sil.data(), n, block);
        ++totalSections;
        checkInvariants(res, totalSections, false, "SILENCE");

        if (res.stateIndex >= 0 && res.stateIndex <= 3)
            ++statesSeen[res.stateIndex];
    };

    auto runSection = [&](float amp, double durSec, const std::string& label)
    {
        const int n = static_cast<int>(durSec * sr);
        auto sig = sineSection(n, 1500.0, sr, amp);
        auto res = feedSection(proc, sig.data(), n, block);

        ++totalSections;
        ++nonSilentCount;

        checkInvariants(res, totalSections, true, label);

        // Track ranges
        if (res.bpm > 0.0f)
        {
            if (res.bpm < minBpmSeen) minBpmSeen = res.bpm;
            if (res.bpm > maxBpmSeen) maxBpmSeen = res.bpm;
        }
        if (res.patternIndex < minPatSeen) minPatSeen = res.patternIndex;
        if (res.patternIndex > maxPatSeen) maxPatSeen = res.patternIndex;
        if (res.stateIndex >= 0 && res.stateIndex <= 3)
            ++statesSeen[res.stateIndex];
    };

    // ── Warmup ──────────────────────────────────────────────────────────────
    std::cerr << "[STABILITY] Starting 300+ second stability test..." << std::endl;
    runSilence(3.0);

    // ── 15 rounds of SOFT + LOUD (each round: 2s silence + 8s SOFT + 2s silence + 8s LOUD) ──
    const int rounds = 15;
    for (int r = 0; r < rounds; ++r)
    {
        runSilence(2.0);
        runSection(0.013f, 8.0, "SOFT");
        runSilence(2.0);
        runSection(0.25f,  8.0, "LOUD");

        if ((r + 1) % 5 == 0)
            std::cerr << "[STABILITY]  round " << (r + 1) << "/" << rounds
                      << " (" << ((r + 1) * 20.0) << "s of music)" << std::endl;
    }

    // ── Cooldown ────────────────────────────────────────────────────────────
    runSilence(3.0);

    // ── Summary ─────────────────────────────────────────────────────────────
    const double musicSeconds = rounds * 16.0;  // 8s SOFT + 8s LOUD per round
    const double totalSeconds = musicSeconds + (rounds * 2 + 3) * 2.0 + 6.0; // ~310s

    std::cerr << "[STABILITY] Complete — "
              << totalSections << " sections, "
              << nonSilentCount << " non-silent, "
              << musicSeconds << "s music, ~"
              << totalSeconds << "s total"
              << std::endl;
    std::cerr << "[STABILITY] BPM range: " << minBpmSeen << " – " << maxBpmSeen << std::endl;
    std::cerr << "[STABILITY] Pattern range: " << minPatSeen << " – " << maxPatSeen << std::endl;
    std::cerr << "[STABILITY] States seen: SILENT=" << statesSeen[0]
              << " VERSE=" << statesSeen[1]
              << " CHORUS=" << statesSeen[2]
              << " BREAKDOWN=" << statesSeen[3]
              << std::endl;

    // ── Assertions ──────────────────────────────────────────────────────────
    REQUIRE(totalSections > 30);
    REQUIRE(nonSilentCount >= 30);

    // BPM must have been in valid range throughout (already checked per-section)
    // Pattern index must have been in valid range throughout (already checked per-section)
    // State must have been valid throughout (already checked per-section)

    // At least VERSE (state 1) must have been seen during SOFT sections
    CHECK(statesSeen[1] > 0);   // VERSE from SOFT sections

    proc.releaseResources();
}
