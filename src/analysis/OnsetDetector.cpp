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

    minOnsetIntervalSamples = juce::jmax(1, static_cast<int>(0.08 * sampleRate));
    tempoLocked = false;

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
    const int slot = ioiRingWrite;                   // already in [0,15]
    ioiHistory[static_cast<size_t>(slot)] = ioiSamples;
    ioiRingWrite = (ioiRingWrite + 1) & 15;          // keep in [0,15], no signed overflow
    ioiRingCount = juce::jmin(16, ioiRingCount + 1);
}

int OnsetDetector::ioiChronologicalSlot(int chronologicalIndex) const noexcept
{
    return (ioiRingWrite - ioiRingCount + chronologicalIndex + 256) % 16;
}

float OnsetDetector::medianIoiBpm() const
{
    if (ioiRingCount < 8)
        return currentBpm.load(std::memory_order_relaxed);

    std::array<float, 16> sorted{};
    for (int i = 0; i < ioiRingCount; ++i)
        sorted[static_cast<size_t>(i)] = ioiHistory[static_cast<size_t>(ioiChronologicalSlot(i))];

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

    // Octave disambiguation: fold 2x/0.5x aliases back toward current estimate.
    const float current = currentBpm.load(std::memory_order_relaxed);
    if (current > 0.0f)
    {
        if (bpm / current > 1.7f)
            bpm *= 0.5f;
        else if (bpm / current < 0.6f)
            bpm *= 2.0f;
    }
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

    if (fluxSinkFn != nullptr)
        fluxSinkFn(fluxSinkUser, flux, totalSamples);

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

            if (!tempoLocked)
            {
                const float bpm = medianIoiBpm();

                if (ioiRingCount >= 8)
                {
                    float sum = 0.0f;
                    for (int j = 0; j < ioiRingCount; ++j)
                        sum += ioiHistory[static_cast<size_t>(ioiChronologicalSlot(j))];
                    const float ioiMean = sum / static_cast<float>(ioiRingCount);
                    const float band = ioiMean * 0.08f;

                    bool allConsistent = true;
                    for (int j = 0; j < ioiRingCount; ++j)
                    {
                        const float ioij = ioiHistory[static_cast<size_t>(ioiChronologicalSlot(j))];
                        if (std::abs(ioij - ioiMean) > band)
                        {
                            allConsistent = false;
                            break;
                        }
                    }
                    if (allConsistent)
                        tempoLocked = true;
                }

                currentBpm.store(bpm, std::memory_order_relaxed);
            }
            else
            {
                // Soft-lock: slowly drift toward genuine tempo changes (EMA α=0.03).
                const float newBpm   = medianIoiBpm();
                const float current  = currentBpm.load(std::memory_order_relaxed);
                const float drifted  = current + kSoftLockEmaAlpha * (newBpm - current);
                currentBpm.store(drifted, std::memory_order_relaxed);
            }
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

void OnsetDetector::resetTempoLock() noexcept
{
    tempoLocked = false;
    // BPM is preserved through reset — caller seeds it via setSeedBpm() after
    // reading the saved value from TempoStabiliser.
    ioiRingWrite = 0;
    ioiRingCount = 0;
    for (auto& v : ioiHistory)
        v = 0.0f;
    lastOnsetSample = -1;
}

void OnsetDetector::setSeedBpm(float bpm) noexcept
{
    currentBpm.store(bpm, std::memory_order_relaxed);
}

void OnsetDetector::setFluxSink(void* userData, FluxSinkFn fn) noexcept
{
    fluxSinkUser = userData;
    fluxSinkFn = fn;
}
