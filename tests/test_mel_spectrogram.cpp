/**
 * MelSpectrogramExtractor: FFT packing and spectrogram sanity.
 *
 * JUCE's real-only FFT takes the first N samples as time-domain input. A
 * packing bug (interleaved zeros) produces a garbage spectrogram, which made
 * the style CNN stick on Silence even though the UI was correctly wired.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "analysis/MelSpectrogramExtractor.h"

#if defined(MA_ENABLE_ONNX)
#include "inference/MetalGrooveInference.h"
#endif

namespace {

std::vector<float> makeSine(int n, double freq, double sr, float amp)
{
    std::vector<float> y(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        y[static_cast<size_t>(i)] = amp * static_cast<float>(
            std::sin(2.0 * M_PI * freq * static_cast<double>(i) / sr));
    return y;
}

int peakMelBand(const float* mel)
{
    int bestT = 0;
    float best = mel[0];
    // Energy per band = max over time (row-major [band][frame]).
    int bestBand = 0;
    float bestBandVal = -1.0e9f;
    for (int m = 0; m < MelSpectrogramExtractor::kMelBands; ++m)
    {
        float rowMax = -1.0e9f;
        for (int t = 0; t < MelSpectrogramExtractor::kTimeFrames; ++t)
        {
            const float v = mel[m * MelSpectrogramExtractor::kTimeFrames + t];
            if (v > rowMax)
                rowMax = v;
            if (v > best)
            {
                best = v;
                bestT = t;
            }
        }
        if (rowMax > bestBandVal)
        {
            bestBandVal = rowMax;
            bestBand = m;
        }
    }
    (void)bestT;
    return bestBand;
}

} // namespace

TEST_CASE("Mel extractor: silence sits on the dB floor", "[unit][mel]")
{
    MelSpectrogramExtractor ex;
    std::vector<float> zeros(22050, 0.0f);
    std::vector<float> mel(static_cast<size_t>(MelSpectrogramExtractor::kOutputSize), 0.0f);
    REQUIRE(ex.process(zeros.data(), mel.data()));
    for (float v : mel)
        REQUIRE(v <= -79.0f);
}

TEST_CASE("Mel extractor: 440 Hz sine peaks in a low-mid band, not the floor", "[unit][mel]")
{
    MelSpectrogramExtractor ex;
    auto y = makeSine(22050, 440.0, MelSpectrogramExtractor::kSampleRate, 0.5f);
    std::vector<float> mel(static_cast<size_t>(MelSpectrogramExtractor::kOutputSize), 0.0f);
    REQUIRE(ex.process(y.data(), mel.data()));

    float peak = -1.0e9f;
    for (float v : mel)
    {
        if (v > peak)
            peak = v;
    }
    // Peak is 0 dB after per-window max-normalisation.
    REQUIRE(peak > -5.0f);
    const int band = peakMelBand(mel.data());
    // 440 Hz sits around mel-band ~12 of 64 (0–8 kHz). Interleaved-FFT packing
    // shoved the peak into a nonsense band. Allow a wide window so filter-bank
    // details cannot flake the test.
    REQUIRE(band >= 4);
    REQUIRE(band <= 28);

    float lo = 1.0e9f;
    for (float v : mel)
        if (v < lo)
            lo = v;
    REQUIRE(peak - lo > 15.0f);
}

#if defined(MA_ENABLE_ONNX)
TEST_CASE("Style CNN accepts a silence-floor mel without crashing", "[unit][mel][style]")
{
    MetalGrooveInference inference;
    REQUIRE(inference.tryLoadModel());

    MelSpectrogramExtractor ex;
    std::vector<float> zeros(22050, 0.0f);
    std::vector<float> mel(static_cast<size_t>(MelSpectrogramExtractor::kOutputSize), 0.0f);
    REQUIRE(ex.process(zeros.data(), mel.data()));
    const int style = inference.classifyStyle(mel.data());
    REQUIRE(style >= 0);
    REQUIRE(style <= 4);
}
#endif
