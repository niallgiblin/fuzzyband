#pragma once

/**
 * @file
 * @brief RMS, spectral centroid, and high-frequency flux for structure classification.
 */

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <vector>

#include "RmsWindow.h"

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

    // ── Fixed-hop onset envelope ─────────────────────────────────────────────
    // The onset RMS sampled at a fixed time hop, independent of the host block
    // size. The attack detector is driven per hop (see the processor), so a
    // 65536-sample host block gives the same detection as a 64-sample one. The
    // hop matches the ~10.7 ms update interval the detector was tuned at.

    /** @brief Number of fixed-hop onset samples captured in the last process(). */
    int getOnsetHopCount() const noexcept { return onsetHopCount; }
    /** @brief Fixed-hop onset RMS (analyser-scaled) at hop @p i. */
    float getOnsetHopRms(int i) const noexcept
    {
        return onsetHopRms[static_cast<size_t>(i)];
    }
    /** @brief Sample offset of hop @p i within the last process() block. */
    int getOnsetHopOffset(int i) const noexcept
    {
        return onsetHopOffset[static_cast<size_t>(i)];
    }
    float getSpectralCentroid() const { return spectralCentroid; }
    float getHighFreqFlux() const { return highFreqFlux; }
    float getPeakRms() const noexcept { return peakRmsEnvelope; }

    /** Sub-bass energy (30–120 Hz) for distinguishing palm-mute chugs from clean arpeggios. */
    float getSubBassEnergy() const { return subBassEnergy; }

    /** Sub-bass energy ratio: subBassEnergy / totalSpectralEnergy. High for chugs, low for clean. */
    float getSubBassRatio() const { return subBassRatio; }

    // Window lengths in seconds. The tests build their RMS window from this
    // constant, so the window definition has one home (see RmsWindow).
    static constexpr double kStructureWindowSeconds = 0.1;
    static constexpr double kOnsetWindowSeconds = 0.02;

private:
    void runSpectrum();

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 1024;

    std::vector<float> window;
    std::vector<float> fftScratch;
    std::vector<float> prevHighMagnitudes;

    // Fixed-in-time RMS windows: the 0.1 s structure window and the ~20 ms onset
    // window. Shared with the tests via RmsWindow so the definition cannot drift.
    RmsWindow rmsWindow;
    RmsWindow onsetWindow;

    // Fixed-hop capture of the onset envelope (see getOnsetHopCount).
    static constexpr int kMaxOnsetHops = 4096;
    std::array<float, kMaxOnsetHops> onsetHopRms{};
    std::array<int, kMaxOnsetHops> onsetHopOffset{};
    int onsetHopCount = 0;
    int onsetHopSamples = 512;
    int onsetHopCountdown = 512;

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
