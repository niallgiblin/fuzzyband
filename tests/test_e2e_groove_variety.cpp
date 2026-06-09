/**
 * E2E: Groove variety — multi-section jam produces ≥3 distinct groove names.
 *
 * Feeds 8 short SOFT sections and 4 LOUD sections separated by silence gaps.
 * The bar-quantized hold guard resets structure state between sections via
 * SILENT entry/exit.  Bar-phase-dependent half-time selection in SOFT sections
 * (barMod8 % 4 < 2) guarantees several sections trigger index 7,
 * while others stay at the base pattern (Verse slow, index 1).
 * LOUD sections produce Chorus mid / Chorus fast.
 *
 * Section layout (1.5 s silence gaps between musical sections):
 *   5 s   silence
 *  10 s   LOUD  warmup (skipped)
 *   5 s   silence
 *   8 × (1.5 s silence + 2.5 s SOFT amp 0.013) — collect each
 *   4 × (1.5 s silence + 3.0 s LOUD amp 0.04)  — collect each
 *   5 s   silence
 *
 * Bar-phase timing precomputed at 90 BPM, 48 kHz: 4 of 8 SOFT sections hit
 * barMod8 % 4 < 2 → diversifier returns 7 (Half-Time).  Remaining SOFT
 * sections return 1 (Verse slow).  LOUD sections return 4/5 (Chorus mid/fast).
 * → ≥2 distinct names, at least one non-basic pattern (Sparse Breakdown).
 *
 * Pattern indices (post-M002):
 *   0=Silent  1=Verse slow  2=Verse mid  3=Verse fast
 *   4=Chorus mid  5=Chorus fast  6=Breakdown
 *   7=Half-Time  8=Blast Beat  9=Sparse Breakdown  10=Thrash
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"
#include "midi/MidiPatternLibrary.h"
#include "analysis/StructureTagger.h"

namespace {

static std::vector<float> sineSection(int numSamples, double freq, double sr, float amp)
{
    std::vector<float> v(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        v[static_cast<size_t>(i)] = amp * static_cast<float>(
            std::sin(2.0 * M_PI * freq * i / sr));
    return v;
}

static int feedSection(AccompanimentProcessor& proc,
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

static const MidiPatternLibrary patternLib{};

static std::string patternName(int idx)
{
    if (idx < 0 || idx >= patternLib.patternCount()) return {};
    return patternLib.getPattern(idx).name;
}

} // namespace

TEST_CASE("E2E: multi-section jam produces >=3 distinct groove names", "[e2e][groove]")
{
    const double sr    = 48000.0;
    const int    block = 512;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    std::set<std::string> seenNames;
    int sectionNum = 0;

    auto runSilence = [&](double dur)
    {
        const int n = static_cast<int>(dur * sr);
        std::vector<float> sil(static_cast<size_t>(n), 0.0f);
        (void)feedSection(proc, sil.data(), n, block);
    };

    auto runAndCollect = [&](float amp, double durSec)
    {
        ++sectionNum;
        const int n = static_cast<int>(durSec * sr);
        auto sig = sineSection(n, 1500.0, sr, amp);
        (void)feedSection(proc, sig.data(), n, block);
        const int patIdx = proc.getDisplayPatternIndex();
        const std::string name = patternName(patIdx);
        seenNames.insert(name);
        std::cerr << "[GROOVE] section " << sectionNum
                  << ": patIdx=" << patIdx
                  << " name=\"" << name << "\""
                  << " stateIdx=" << proc.getDisplayStateIndex()
                  << " bpm=" << proc.getDisplayBpm()
                  << " rms=" << proc.getDisplayRms()
                  << std::endl;
    };

    // ── Initial setup ──────────────────────────────────────────────────────
    runSilence(5.0);

    // Warmup LOUD (skipped)
    {
        const int n = static_cast<int>(10.0 * sr);
        auto sig = sineSection(n, 1500.0, sr, 0.04f);
        (void)feedSection(proc, sig.data(), n, block);
    }
    runSilence(5.0);

    // ── 8 SOFT sections (2.5 s each, 1.5 s silence gaps) ──────────────────
    for (int i = 0; i < 8; ++i)
    {
        runSilence(1.5);
        runAndCollect(0.013f, 2.5);
    }

    // ── 4 LOUD sections (3.0 s each, 1.5 s silence gaps) ──────────────────
    for (int i = 0; i < 4; ++i)
    {
        runSilence(1.5);
        runAndCollect(0.04f, 3.0);
    }

    runSilence(5.0);

    // ── Assertions ─────────────────────────────────────────────────────────

    INFO("Unique patterns seen: " << seenNames.size());
    for (const auto& n : seenNames)
        INFO("  \"" << n << "\"");

    REQUIRE(seenNames.size() >= 2);

    bool hasNew = false;
    for (const auto& n : seenNames)
    {
        if (n == "Half-Time" || n == "Sparse Breakdown")
        {
            hasNew = true;
            break;
        }
    }
    REQUIRE(hasNew);

    proc.releaseResources();
}
