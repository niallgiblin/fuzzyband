#include "OnsetDetector.h"
#include <algorithm>
#include <cmath>

OnsetDetector::OnsetDetector() = default;

OnsetDetector::~OnsetDetector() = default;

void OnsetDetector::prepare(double newSampleRate, int maxBlockSize)
{
    (void)maxBlockSize;
    sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSize = fft->getSize();
    hopSize = juce::jmax(256, fftSize / 4);

    fifo.assign(static_cast<size_t>(fftSize), 0.0f);
    fifoWrite = 0;
    samplesSinceLastFft = 0;

    window.resize(static_cast<size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
    {
        const float phase = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(phase));
    }

    fftScratch.assign(static_cast<size_t>(fftSize * 2), 0.0f);
    prevMagnitudes.assign(static_cast<size_t>(fftSize / 2 + 1), 0.0f);

    ioiRingWrite = 0;
    ioiRingCount = 0;
    for (auto& v : ioiHistory)
        v = 0.0f;

    lastOnsetSample = -1;
    totalSamples = 0;
    onsetThisBlock = false;

    minOnsetIntervalSamples = juce::jmax(1, static_cast<int>(0.05 * sampleRate));

    // Precompute band-limited flux bin range: 100 Hz – 4000 Hz (skip DC)
    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    const int nyquist = fftSize / 2;
    fluxBinLo = std::max(1, static_cast<int>(std::ceil(100.0f / binHz)));
    fluxBinHi = std::min(nyquist, static_cast<int>(std::floor(4000.0f / binHz)));

    // Reset adaptive threshold rolling state
    fluxRollingBuf.fill(0.0f);
    fluxRollingSum = 0.0f;
    fluxRollingN   = 0;
    fluxRollingIdx = 0;
}

void OnsetDetector::pushIoi(float ioiSamples)
{
    ioiHistory[static_cast<size_t>(ioiRingWrite % 16)] = ioiSamples;
    ++ioiRingWrite;
    ioiRingCount = juce::jmin(16, ioiRingCount + 1);
}

float OnsetDetector::medianIoiBpm() const
{
    if (ioiRingCount < 8)
        return currentBpm.load(std::memory_order_relaxed);

    std::array<float, 16> sorted{};
    for (int i = 0; i < ioiRingCount; ++i)
    {
        int idx = ioiRingWrite - ioiRingCount + i;
        idx = (idx % 16 + 16) % 16;
        sorted[static_cast<size_t>(i)] = ioiHistory[static_cast<size_t>(idx)];
    }

    std::sort(sorted.begin(), sorted.begin() + ioiRingCount);

    const int n = ioiRingCount;
    float medianIoi = 0.0f;
    if ((n % 2) == 1)
        medianIoi = sorted[static_cast<size_t>(n / 2)];
    else
        medianIoi = 0.5f * (sorted[static_cast<size_t>(n / 2 - 1)] + sorted[static_cast<size_t>(n / 2)]);

    const float ioiSec = medianIoi / static_cast<float>(sampleRate);
    if (ioiSec <= 1.0e-6f)
        return currentBpm.load(std::memory_order_relaxed);

    float bpm = 60.0f / ioiSec;
    bpm = juce::jlimit(kMinBpm, kMaxBpm, bpm);
    return bpm;
}

void OnsetDetector::runFftFrame()
{
    const int numBins = fftSize / 2 + 1;

    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (fifoWrite - fftSize + i + fftSize) % fftSize;
        fftScratch[static_cast<size_t>(i)] = fifo[static_cast<size_t>(idx)] * window[static_cast<size_t>(i)];
    }

    for (int i = fftSize; i < fftSize * 2; ++i)
        fftScratch[static_cast<size_t>(i)] = 0.0f;

    fft->performFrequencyOnlyForwardTransform(fftScratch.data(), true);

    // Update prevMagnitudes for all bins; accumulate flux only in band-limited range
    float flux = 0.0f;
    for (int i = 0; i < numBins; ++i)
    {
        const float m = fftScratch[static_cast<size_t>(i)];
        const float prev = prevMagnitudes[static_cast<size_t>(i)];
        if (i >= fluxBinLo && i <= fluxBinHi)
        {
            const float d = m - prev;
            if (d > 0.0f)
                flux += d;
        }
        prevMagnitudes[static_cast<size_t>(i)] = m;
    }

    // Update rolling mean and compute adaptive threshold (evict-first canonical pattern)
    if (fluxRollingN == kRollingWindow)
        fluxRollingSum -= fluxRollingBuf[static_cast<size_t>(fluxRollingIdx)];
    else
        ++fluxRollingN;
    fluxRollingBuf[static_cast<size_t>(fluxRollingIdx)] = flux;
    fluxRollingSum += flux;
    fluxRollingIdx = (fluxRollingIdx + 1) % kRollingWindow;

    const float mean = fluxRollingSum / static_cast<float>(fluxRollingN);
    const float threshold = std::max(kFluxFloor, kAdaptiveK * mean);

    if (flux <= threshold)
        return;

    if (lastOnsetSample >= 0
        && (totalSamples - lastOnsetSample) < static_cast<int64_t>(minOnsetIntervalSamples))
        return;

    if (lastOnsetSample >= 0)
    {
        const float ioi = static_cast<float>(totalSamples - lastOnsetSample);
        const float minIoi = static_cast<float>(sampleRate) * 60.0f / kMaxBpm;
        const float maxIoi = static_cast<float>(sampleRate) * 60.0f / kMinBpm;
        if (ioi >= minIoi && ioi <= maxIoi)
        {
            pushIoi(ioi);
            const float bpm = medianIoiBpm();
            currentBpm.store(bpm, std::memory_order_relaxed);
        }
    }

    lastOnsetSample = totalSamples;
    onsetThisBlock = true;
}

void OnsetDetector::process(const float* audioData, int numSamples)
{
    onsetThisBlock = false;

    for (int n = 0; n < numSamples; ++n)
    {
        fifo[static_cast<size_t>(fifoWrite)] = audioData[n];
        fifoWrite = (fifoWrite + 1) % fftSize;
        ++totalSamples;
        ++samplesSinceLastFft;

        if (samplesSinceLastFft >= hopSize)
        {
            samplesSinceLastFft = 0;
            runFftFrame();
        }
    }
}
