#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class OnsetDetector
{
public:
    OnsetDetector();
    ~OnsetDetector();

    void prepare(double sampleRate, int maxBlockSize);
    void process(const float* audioData, int numSamples);

    float getCurrentBpm() const { return currentBpm.load(std::memory_order_relaxed); }
    bool onsetDetectedThisBlock() const { return onsetThisBlock; }

private:
    void runFftFrame();
    float medianIoiBpm() const;
    void pushIoi(float ioiSamples);

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftOrder = 11;
    int fftSize = 2048;
    int hopSize = 512;

    std::vector<float> fifo;
    int fifoWrite = 0;
    int samplesSinceLastFft = 0;

    std::vector<float> window;
    std::vector<float> fftScratch;
    std::vector<float> prevMagnitudes;

    double sampleRate = 44100.0;
    int64_t totalSamples = 0;

    std::array<float, 8> ioiHistory{};
    int ioiRingWrite = 0;
    int ioiRingCount = 0;

    int64_t lastOnsetSample = -1;
    int minOnsetIntervalSamples = 2205;

    bool onsetThisBlock = false;

    float fluxThreshold = 0.35f;
    static constexpr float kMinBpm = 80.0f;
    static constexpr float kMaxBpm = 220.0f;

    std::atomic<float> currentBpm{ 120.0f };
};
