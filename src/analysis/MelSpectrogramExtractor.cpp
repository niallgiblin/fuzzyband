#include "MelSpectrogramExtractor.h"
#include <algorithm>
#include <cmath>

// ══════════════════════════════════════════════════════════════════════════════
// Helpers
// ══════════════════════════════════════════════════════════════════════════════

static float hzToMel(float hz) noexcept
{
    return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

static float melToHz(float mel) noexcept
{
    return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

// ══════════════════════════════════════════════════════════════════════════════
// Constructor
// ══════════════════════════════════════════════════════════════════════════════

MelSpectrogramExtractor::MelSpectrogramExtractor()
{
    fft = std::make_unique<juce::dsp::FFT>(11); // log2(2048) = 11

    // Hann window
    window.resize(kFftSize);
    for (int i = 0; i < kFftSize; ++i)
        window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (kFftSize - 1)));

    // Spectra rolling buffer: kTimeFrames × (kFftSize/2 + 1) = 32 × 1025
    const int numBins = kFftSize / 2 + 1;
    spectraBuffer.resize(static_cast<size_t>(kTimeFrames * numBins), 0.0f);

    fftScratch.resize(kFftSize * 2); // interleaved real/imag for JUCE FFT

    buildMelFilterbank();
}

// ══════════════════════════════════════════════════════════════════════════════
// Mel filterbank
// ══════════════════════════════════════════════════════════════════════════════

void MelSpectrogramExtractor::buildMelFilterbank() noexcept
{
    const int numBins = kFftSize / 2 + 1;  // 1025
    const float nyquist = static_cast<float>(kSampleRate) / 2.0f;  // 22050
    const float melLow = hzToMel(0.0f);
    const float melHigh = hzToMel(8000.0f);  // fmax=8000 as per Python

    melWeights.resize(static_cast<size_t>(kMelBands * numBins), 0.0f);

    // Compute mel center frequencies
    std::vector<float> melCenters(static_cast<size_t>(kMelBands + 2));
    for (int i = 0; i < kMelBands + 2; ++i)
    {
        const float mel = melLow + (melHigh - melLow) * static_cast<float>(i) / static_cast<float>(kMelBands + 1);
        melCenters[static_cast<size_t>(i)] = melToHz(mel);
    }

    // Map mel centers to FFT bin indices
    std::vector<int> binCenters(static_cast<size_t>(kMelBands + 2));
    for (int i = 0; i < kMelBands + 2; ++i)
        binCenters[static_cast<size_t>(i)] = static_cast<int>(std::floor(melCenters[static_cast<size_t>(i)] / nyquist * (numBins - 1)));

    // Build triangular filters
    for (int m = 0; m < kMelBands; ++m)
    {
        const int leftBin  = binCenters[static_cast<size_t>(m)];
        const int midBin   = binCenters[static_cast<size_t>(m + 1)];
        const int rightBin = binCenters[static_cast<size_t>(m + 2)];

        auto* out = melWeights.data() + m * numBins;

        // Rising edge: leftBin → midBin
        for (int k = leftBin; k <= midBin; ++k)
        {
            const float weight = (midBin == leftBin) ? 1.0f
                : static_cast<float>(k - leftBin) / static_cast<float>(midBin - leftBin);
            out[k] = weight;
        }

        // Falling edge: midBin → rightBin
        for (int k = midBin + 1; k <= rightBin; ++k)
        {
            const float weight = (rightBin == midBin) ? 1.0f
                : static_cast<float>(rightBin - k) / static_cast<float>(rightBin - midBin);
            out[k] = weight;
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Process one STFT frame into the rolling buffer
// ══════════════════════════════════════════════════════════════════════════════

void MelSpectrogramExtractor::processFrame(const float* fftMagnitudes) noexcept
{
    const int numBins = kFftSize / 2 + 1;

    // Write into rolling buffer at current write position
    const int offset = spectraWriteIdx * numBins;
    std::copy(fftMagnitudes, fftMagnitudes + numBins, spectraBuffer.begin() + offset);

    spectraWriteIdx = (spectraWriteIdx + 1) % kTimeFrames;
}

// ══════════════════════════════════════════════════════════════════════════════
// Process audio window → mel spectrogram
// ══════════════════════════════════════════════════════════════════════════════

bool MelSpectrogramExtractor::process(const float* audio, float* melOut) noexcept
{
    if (audio == nullptr || melOut == nullptr) return false;

    const int numBins = kFftSize / 2 + 1;

    // ── STFT: slide window with kHopSize hop ──────────────────────────────
    // Slide FFT window across the full input (assumes 22050 samples = 512ms @ 44.1kHz)
    // (22050 - 2048) / 512 + 1 ≈ 40 frames
    const int audioLen = 22050;

    for (int start = 0; start + kFftSize <= audioLen; start += kHopSize)
    {
        // Hann-window the input
        for (int i = 0; i < kFftSize; ++i)
        {
            fftScratch[static_cast<size_t>(i * 2)]     = audio[start + i] * window[static_cast<size_t>(i)];
            fftScratch[static_cast<size_t>(i * 2 + 1)] = 0.0f;
        }

        fft->performRealOnlyForwardTransform(fftScratch.data(), true);

        // Compute magnitude spectrum (only positive frequencies)
        std::vector<float> mags(static_cast<size_t>(numBins));
        for (int b = 0; b < numBins; ++b)
        {
            const float re = fftScratch[static_cast<size_t>(b * 2)];
            const float im = fftScratch[static_cast<size_t>(b * 2 + 1)];
            mags[static_cast<size_t>(b)] = std::sqrt(re * re + im * im);
        }

        processFrame(mags.data());
    }

    // ── Apply mel filterbank to the full rolling buffer ───────────────────
    // The rolling buffer is ordered [oldest...newest] from spectraWriteIdx.
    // Output: [melBand][timeFrame] row-major.

    // Find max magnitude for dB normalization
    float maxMag = 1e-8f;
    for (int t = 0; t < kTimeFrames; ++t)
    {
        const int frameIdx = (spectraWriteIdx + t) % kTimeFrames;
        const auto* spectrum = spectraBuffer.data() + frameIdx * numBins;
        for (int m = 0; m < kMelBands; ++m)
        {
            const auto* weights = melWeights.data() + m * numBins;
            float sum = 0.0f;
            for (int b = 0; b < numBins; ++b)
                sum += spectrum[b] * weights[b];
            const int outIdx = m * kTimeFrames + t;
            melOut[outIdx] = sum;
            if (sum > maxMag) maxMag = sum;
        }
    }

    // Power to dB (with floor)
    const float dbFloor = -80.0f;
    for (int i = 0; i < kOutputSize; ++i)
    {
        float val = melOut[i] / maxMag;
        if (val < 1e-8f) val = 1e-8f;
        melOut[i] = std::max(dbFloor, 10.0f * std::log10(val));
    }

    return true;
}
