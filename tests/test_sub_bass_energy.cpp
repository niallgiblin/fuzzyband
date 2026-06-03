#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "analysis/EnergyAnalyser.h"
#include "analysis/StructureTagger.h"

namespace {

std::vector<float> makeSine(int numSamples, double freq, double sampleRate, double amplitude = 1.0)
{
    std::vector<float> buf(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        buf[static_cast<size_t>(i)] = static_cast<float>(
            amplitude * std::sin(2.0 * M_PI * freq * i / sampleRate));
    return buf;
}

std::vector<float> makeSilence(int numSamples)
{
    return std::vector<float>(static_cast<size_t>(numSamples), 0.0f);
}

} // namespace

// ─── Sub-Bass Energy (EnergyAnalyser) ────────────────────────────────────────

TEST_CASE("Sub-bass: ratio > 0.5 for 60 Hz tone", "[sub_bass]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // 60 Hz sine — all energy in the 30-120 Hz sub-bass band
    // Feed enough hops to fill the FFT fifo (4 hops = 1024 samples minimum)
    const int n = 4096;
    auto sig = makeSine(n, 60.0, 44100.0, 0.5);
    analyser.process(sig.data(), n);

    const float ratio = analyser.getSubBassRatio();
    REQUIRE(ratio > 0.5f);
}

TEST_CASE("Sub-bass: ratio < 0.2 for 2 kHz tone", "[sub_bass]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // 2 kHz sine — all energy well above the 120 Hz sub-bass ceiling
    const int n = 4096;
    auto sig = makeSine(n, 2000.0, 44100.0, 0.5);
    analyser.process(sig.data(), n);

    const float ratio = analyser.getSubBassRatio();
    REQUIRE(ratio < 0.2f);
}

TEST_CASE("Sub-bass: energy is non-negative for silence", "[sub_bass]")
{
    EnergyAnalyser analyser;
    analyser.prepare(44100.0, 512);

    // Silence should produce subBassEnergy ≥ 0 and ratio = 0 (via floor guard)
    const int n = 4096;
    auto silent = makeSilence(n);
    analyser.process(silent.data(), n);

    REQUIRE(analyser.getSubBassEnergy() >= 0.0f);
    REQUIRE(analyser.getSubBassRatio() == 0.0f);
}

TEST_CASE("Sub-bass: ratio increases with lower-frequency content", "[sub_bass]")
{
    EnergyAnalyser low, high;
    low.prepare(44100.0, 512);
    high.prepare(44100.0, 512);

    // 60 Hz = mostly sub-bass; 500 Hz = mostly above sub-bass
    const int n = 4096;
    auto sigLow  = makeSine(n, 60.0,  44100.0, 0.5);
    auto sigHigh = makeSine(n, 500.0, 44100.0, 0.5);
    low.process(sigLow.data(), n);
    high.process(sigHigh.data(), n);

    REQUIRE(low.getSubBassRatio() > high.getSubBassRatio());
}

// ─── Sub-Bass Discrimination in StructureTagger ──────────────────────────────

TEST_CASE("StructureTagger: SOFT with low sub-bass ratio stays SOFT", "[sub_bass][structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // RMS=0.040 (typical SOFT zone), subBassRatio=0.1 (clean arpeggio)
    // Should stay SOFT despite being above kAmbientCeil
    tagger.setSubBassRatio(0.1f);
    const StructureState s = tagger.update(0.040f, 800.0f, 0.0f, 512);

    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: LOUD with high sub-bass ratio detects chug", "[sub_bass][structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // RMS=0.040 (ambiguous SOFT zone) but subBassRatio=0.6 (palm-mute chug)
    // Should return LOUD immediately because sub-bass overrides RMS ambiguity
    tagger.setSubBassRatio(0.6f);
    const StructureState s = tagger.update(0.040f, 500.0f, 0.0f, 512);

    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureTagger: RMS above loudFloor still returns LOUD regardless of sub-bass", "[sub_bass][structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // RMS=0.090 (≥ kLoudRms=0.075) — RMS alone triggers LOUD
    // Sub-bass ratio is low but RMS dominates
    tagger.setSubBassRatio(0.0f);
    const StructureState s = tagger.update(0.090f, 500.0f, 0.0f, 512);

    REQUIRE(s == StructureState::LOUD);
}

TEST_CASE("StructureTagger: sub-bass discrimination respects hold time", "[sub_bass][structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // Enter SOFT with clean signal
    tagger.setSubBassRatio(0.1f);
    tagger.update(0.040f, 800.0f, 0.0f, 512);  // now SOFT

    // Switch to chug-like sub-bass but stay in SOFT hold window
    tagger.setSubBassRatio(0.6f);
    StructureState s = tagger.update(0.040f, 500.0f, 0.0f, 512);
    REQUIRE(s == StructureState::SOFT);  // still held

    // Pump blocks past SOFT→LOUD hold time (1.6s = ~139 blocks at 512/44100)
    for (int i = 0; i < 140; ++i)
        s = tagger.update(0.040f, 500.0f, 0.0f, 512);

    REQUIRE(s == StructureState::LOUD);  // hold expired, chug detected
}

TEST_CASE("StructureTagger: sub-bass ratio in middle range falls through to RMS logic", "[sub_bass][structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // subBassRatio=0.30: between kSubBassSoftCeil (0.20) and kSubBassLoudFloor (0.35)
    // RMS=0.040 < loudFloor (0.075) → should fall through to SOFT (RMS catch-all)
    tagger.setSubBassRatio(0.30f);
    const StructureState s = tagger.update(0.040f, 600.0f, 0.0f, 512);

    REQUIRE(s == StructureState::SOFT);
}

TEST_CASE("StructureTagger: BREAKDOWN still works with sub-bass", "[sub_bass][structure]")
{
    StructureTagger tagger;
    tagger.prepare(44100.0);

    // RMS=0.15 ≥ kBreakdownRms=0.12 → BREAKDOWN regardless of sub-bass
    tagger.setSubBassRatio(0.1f);  // low sub-bass, but RMS alone triggers breakdown
    const StructureState s = tagger.update(0.150f, 300.0f, 0.0f, 512);

    REQUIRE(s == StructureState::BREAKDOWN);
}
