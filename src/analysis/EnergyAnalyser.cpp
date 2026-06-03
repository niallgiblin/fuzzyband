#include "EnergyAnalyser.h"
#include <cmath>

void EnergyAnalyser::prepare(double newSampleRate, int maxBlockSize)
{
    (void)maxBlockSize;
    sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;

    fft = std::make_unique<juce::dsp::FFT>(10);
    fftSize = fft->getSize();
    hopSize = juce::jmax(128, fftSize / 4);

    window.resize(static_cast<size_t>(fftSize));
    for (int i = 0; i < fftSize; ++i)
    {
        const float phase = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[static_cast<size_t>(i)] = 0.5f * (1.0f - std::cos(phase));
    }

    fftScratch.assign(static_cast<size_t>(fftSize * 2), 0.0f);
    prevHighMagnitudes.assign(static_cast<size_t>(fftSize / 2 + 1), 0.0f);

    const int rmsLen = juce::jmax(1, static_cast<int>(0.1 * sampleRate));
    rmsWindow.assign(static_cast<size_t>(rmsLen), 0.0f);
    rmsWrite = 0;
    rmsFill = 0;

    fifo.assign(static_cast<size_t>(fftSize), 0.0f);
    fifoWrite = 0;
    hopCounter = 0;

    rmsEnergy = 0.0f;
    spectralCentroid = 0.0f;
    highFreqFlux = 0.0f;
    peakRmsEnvelope = 0.0f;
    subBassEnergy = 0.0f;
    subBassRatio = 0.0f;
}

void EnergyAnalyser::runSpectrum()
{
    const int numBins = fftSize / 2 + 1;
    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);

    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (fifoWrite - fftSize + i + fftSize) % fftSize;
        fftScratch[static_cast<size_t>(i)] = fifo[static_cast<size_t>(idx)] * window[static_cast<size_t>(i)];
    }

    for (int i = fftSize; i < fftSize * 2; ++i)
        fftScratch[static_cast<size_t>(i)] = 0.0f;

    fft->performFrequencyOnlyForwardTransform(fftScratch.data(), true);

    float sumMag = 0.0f;
    float weighted = 0.0f;
    float highFlux = 0.0f;
    float subBassSum = 0.0f;

    const int minHighBin = juce::jlimit(1, numBins - 1, static_cast<int>(std::ceil(2000.0f / binHz)));

    // Sub-bass range: 30–120 Hz — palm-mute chugs concentrate energy here
    const int subBassLowBin = juce::jlimit(0, numBins - 1, static_cast<int>(std::ceil(30.0f / binHz)));
    const int subBassHighBin = juce::jlimit(0, numBins - 1, static_cast<int>(std::floor(120.0f / binHz)));

    for (int i = 0; i < numBins; ++i)
    {
        const float m = fftScratch[static_cast<size_t>(i)];
        const float f = static_cast<float>(i) * binHz;
        sumMag += m;
        weighted += f * m;

        if (i >= subBassLowBin && i <= subBassHighBin)
            subBassSum += m;

        if (i >= minHighBin)
        {
            const float prev = prevHighMagnitudes[static_cast<size_t>(i)];
            const float d = m - prev;
            if (d > 0.0f)
                highFlux += d;
            prevHighMagnitudes[static_cast<size_t>(i)] = m;
        }
    }

    subBassEnergy = subBassSum;
    subBassRatio = (sumMag > kSubBassEnergyFloor) ? (subBassSum / sumMag) : 0.0f;

    if (sumMag > 1.0e-9f)
        spectralCentroid = weighted / sumMag;
    else
        spectralCentroid = 0.0f;

    highFreqFlux = highFlux;
}

void EnergyAnalyser::process(const float* audioData, int numSamples)
{
    for (int n = 0; n < numSamples; ++n)
    {
        const float s = audioData[n];

        rmsWindow[static_cast<size_t>(rmsWrite)] = s * s;
        rmsWrite = (rmsWrite + 1) % static_cast<int>(rmsWindow.size());
        if (rmsFill < static_cast<int>(rmsWindow.size()))
            ++rmsFill;

        fifo[static_cast<size_t>(fifoWrite)] = s;
        fifoWrite = (fifoWrite + 1) % fftSize;
        ++hopCounter;

        if (hopCounter >= hopSize)
        {
            hopCounter = 0;
            runSpectrum();
        }
    }

    float acc = 0.0f;
    const int len = rmsFill;
    for (int i = 0; i < len; ++i)
        acc += rmsWindow[static_cast<size_t>(i)];

    if (len > 0)
    {
        const float rms = std::sqrt(acc / static_cast<float>(len));
        rmsEnergy = juce::jlimit(0.0f, 1.0f, rms * 4.0f);
    }
    else
    {
        rmsEnergy = 0.0f;
    }

    // Slow-decay peak envelope tracking for relative SILENT detection
    static constexpr float kRelease = 0.9995f;  // slow fall (~10 s to -20 dB at 50 Hz block rate)
    if (rmsEnergy > peakRmsEnvelope)
        peakRmsEnvelope = rmsEnergy;          // instantaneous attack: snap to true peak
    else
        peakRmsEnvelope = kRelease * peakRmsEnvelope;
    peakRmsEnvelope = std::max(peakRmsEnvelope, kSilentRmsFloor);
}
