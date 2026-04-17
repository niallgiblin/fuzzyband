/**
 * STRUC-03: Replay test/fixtures/structure_shadow.wav and compare pattern-index transitions
 * to tests/expected_structure_shadow_pattern_transitions.txt (MA_ENABLE_ONNX=OFF path).
 *
 * Regenerate golden (from repo root):
 *   MA_WRITE_STRUCTURE_GOLDEN=1 ./build/MetalAccompanimentIntegrationTests "Structure shadow fixture"
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

#ifndef MA_REPO_ROOT
#error "MA_REPO_ROOT required"
#endif

static juce::File goldenFile()
{
    return juce::File(MA_REPO_ROOT).getChildFile("tests/expected_structure_shadow_pattern_transitions.txt");
}

TEST_CASE("Structure shadow fixture replay matches golden pattern transitions", "[structure][shadow]")
{
    const juce::File wavFile = juce::File(MA_REPO_ROOT).getChildFile("test/fixtures/structure_shadow.wav");
    REQUIRE(wavFile.existsAsFile());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(new juce::FileInputStream(wavFile), true));
    REQUIRE(reader != nullptr);

    const int numSamples = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> monoBuf(1, numSamples);
    REQUIRE(reader->read(&monoBuf, 0, numSamples, 0, true, false));

    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const int block = 512;
    int lastPat = -999999;
    std::ostringstream oss;
    int64_t acc = 0;

    juce::MidiBuffer midi;

    for (int start = 0; start + block <= numSamples; start += block)
    {
        juce::AudioBuffer<float> buf(2, block);
        buf.copyFrom(0, 0, monoBuf, 0, start, block);
        buf.copyFrom(1, 0, monoBuf, 0, start, block);
        proc.processBlock(buf, midi);
        acc += static_cast<int64_t>(block);
        const int p = proc.getDisplayPatternIndex();
        if (p != lastPat)
        {
            oss << acc << " " << p << "\n";
            lastPat = p;
        }
        proc.flushBackgroundInferenceForTests();
    }

    proc.releaseResources();

    const std::string got = oss.str();

    if (std::getenv("MA_WRITE_STRUCTURE_GOLDEN") != nullptr)
    {
        goldenFile().replaceWithText(got);
        FAIL("Wrote golden via MA_WRITE_STRUCTURE_GOLDEN=1 — disable env and re-run.");
    }

    REQUIRE(goldenFile().existsAsFile());
    std::ifstream f(goldenFile().getFullPathName().toStdString());
    REQUIRE(f.good());
    std::ostringstream exp;
    for (std::string line; std::getline(f, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        exp << line << '\n';
    }
    REQUIRE(exp.str() == got);
}
