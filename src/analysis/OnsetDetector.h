#pragma once

/**
 * @file
 * @brief Spectral-flux onset detector and IOI-based tempo estimate.
 */

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

/**
 * @brief Detects note onsets and tracks BPM (40–300) from inter-onset intervals.
 *
 * Locks BPM once 8+ consecutive IOIs agree within ±8%; lock clears on SILENT state.
 * Onset refractory period is 50 ms to reject fuzz-guitar harmonic false-triggers.
 * BPM is computed via median IOI with no post-rounding.
 *
 * Intended to run from the audio processing path; methods called from @ref AccompanimentProcessor::processBlock.
 */
class OnsetDetector
{
public:
    OnsetDetector();
    ~OnsetDetector();

    void prepare(double sampleRate, int maxBlockSize);
    void process(const float* audioData, int numSamples);

    /** @brief Median-filtered BPM estimate from recent IOIs. */
    float getCurrentBpm() const { return currentBpm.load(std::memory_order_relaxed); }

    /** @brief True if a new onset was detected in the current process() call. */
    bool onsetDetectedThisBlock() const { return onsetThisBlock; }

    /** @brief Clear lock and recent IOI tempo memory after sustained silence. Audio-thread safe. */
    void resetTempoLock() noexcept;

    /** @brief True once 8+ consecutive IOIs are within ±8% of their mean. */
    bool isTempoLocked() const noexcept { return tempoLocked; }

    /** @brief Seed current BPM from an external source (e.g. TempoStabiliser) so reset
     *         does not snap to default. Audio-thread safe. */
    void setSeedBpm(float bpm) noexcept;

    /** @brief Optional per-hop OSF callback (real-time safe — single indirect call).
     *         Invoked with latest flux **before** adaptive onset gating. Default: no-op. */
    using FluxSinkFn = void (*)(void* userData, float flux, int64_t audioSampleCounter);
    void setFluxSink(void* userData, FluxSinkFn fn) noexcept;

private:
    void runFftFrame();
    float medianIoiBpm() const;
    void pushIoi(float ioiSamples);
    /** @brief Chronological IOI index @a chronologicalIndex (oldest = 0) in the 16-slot ring. */
    int ioiChronologicalSlot(int chronologicalIndex) const noexcept;

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

    std::array<float, 16> ioiHistory{};
    int ioiRingWrite = 0;
    int ioiRingCount = 0;

    int64_t lastOnsetSample = -1;
    int minOnsetIntervalSamples = 2205;

    bool onsetThisBlock = false;
    bool tempoLocked = false;

    // Band-limited flux: precomputed bin range for 100 Hz – 4000 Hz
    int fluxBinLo = 0;
    int fluxBinHi = 0;

    // Adaptive threshold: rolling mean over ~0.5 s at 512-sample hop / 44100 Hz
    static constexpr float kAdaptiveK     = 1.5f;  // multiplier above mean
    static constexpr float kFluxFloor     = 0.05f; // absolute minimum threshold
    static constexpr int   kRollingWindow = 43;    // ~0.5 s at hop/sr
    float fluxRollingSum = 0.0f;
    int   fluxRollingN   = 0;
    int   fluxRollingIdx = 0;
    std::array<float, 43> fluxRollingBuf{};

    static constexpr float kMinBpm = 40.0f;
    static constexpr float kMaxBpm = 300.0f;
    static constexpr float kSoftLockEmaAlpha = 0.03f;

    std::atomic<float> currentBpm{ 120.0f };

    void* fluxSinkUser = nullptr;
    FluxSinkFn fluxSinkFn = nullptr;
};
