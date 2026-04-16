#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

class EnergyAnalyser
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void process(const float* audioData, int numSamples);

    float getRmsEnergy() const { return rmsEnergy; }
    float getSpectralCentroid() const { return spectralCentroid; }
    float getHighFreqFlux() const { return highFreqFlux; }

private:
    void runSpectrum();

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 1024;

    std::vector<float> window;
    std::vector<float> fftScratch;
    std::vector<float> prevHighMagnitudes;

    std::vector<float> rmsWindow;
    int rmsWrite = 0;
    int rmsFill = 0;

    double sampleRate = 44100.0;

    float rmsEnergy = 0.0f;
    float spectralCentroid = 0.0f;
    float highFreqFlux = 0.0f;

    std::vector<float> fifo;
    int fifoWrite = 0;
    int hopCounter = 0;
    int hopSize = 256;
};
