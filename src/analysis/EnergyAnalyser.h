#pragma once

/**
 * @file
 * @brief RMS, spectral centroid, and high-frequency flux for structure classification.
 */

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

/**
 * @brief Per-block spectral features used by @ref StructureTagger.
 *
 * Call `prepare` once, then `process` for each audio block on the audio thread.
 */
class EnergyAnalyser
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void process(const float* audioData, int numSamples);

    float getRmsEnergy() const { return rmsEnergy; }

    /**
     * @brief Fast (~20 ms) RMS for note-onset detection.
     *
     * `getRmsEnergy()` uses a 0.1 s window: stable enough to be the loudness
     * signal for the structure tagger, but far too long to resolve note attacks
     * — at 120 BPM a 16th note lasts 125 ms, so a 100 ms window never sees a
     * trough between notes and the attack detector had to lean on stale-state
     * artefacts to fire at all (and then fired several times per note). This
     * window is the onset signal; it keeps the same ×4 scale and clamp.
     */
    float getOnsetRmsEnergy() const { return onsetRmsEnergy; }
    float getSpectralCentroid() const { return spectralCentroid; }
    float getHighFreqFlux() const { return highFreqFlux; }
    float getPeakRms() const noexcept { return peakRmsEnvelope; }

    /** Sub-bass energy (30–120 Hz) for distinguishing palm-mute chugs from clean arpeggios. */
    float getSubBassEnergy() const { return subBassEnergy; }

    /** Sub-bass energy ratio: subBassEnergy / totalSpectralEnergy. High for chugs, low for clean. */
    float getSubBassRatio() const { return subBassRatio; }

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

    // Fast onset-detection window (see getOnsetRmsEnergy). Fixed in time, so it
    // does not change with the host buffer size.
    std::vector<float> onsetWindow;
    int onsetWrite = 0;
    int onsetFill = 0;

    double sampleRate = 44100.0;

    float rmsEnergy = 0.0f;
    float onsetRmsEnergy = 0.0f;
    float spectralCentroid = 0.0f;
    float highFreqFlux = 0.0f;
    float peakRmsEnvelope = 0.0f;     // slow-decay peak of rmsEnergy

    float subBassEnergy = 0.0f;
    float subBassRatio = 0.0f;

    static constexpr float kSilentRmsFloor = 0.01f;  // prevents divide-by-zero in genuinely silent input
    static constexpr float kSubBassEnergyFloor = 1.0e-9f;  // prevents NaN in ratio when silent

    std::vector<float> fifo;
    int fifoWrite = 0;
    int hopCounter = 0;
    int hopSize = 256;
};
