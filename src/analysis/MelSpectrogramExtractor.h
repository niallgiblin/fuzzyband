#pragma once

/**
 * @file
 * @brief Real-time mel spectrogram extraction for ONNX playing-style classifier.
 *
 * Accumulates audio via AudioRingBuffer, extracts STFT frames on hop intervals,
 * maintains a rolling 32-frame buffer, applies 64-band mel filterbank,
 * and outputs (64, 32) float tensors for the classifier.
 */

#include <vector>
#include <memory>
#include <juce_dsp/juce_dsp.h>

/**
 * @brief Extracts 64×32 mel spectrograms from real-time audio.
 *
 * Parameters match the Python librosa pipeline:
 *   - FFT size: 2048 (40ms @ 44.1kHz)
 *   - Hop: 512 samples (11.6ms)
 *   - 32-frame rolling buffer (~370ms context)
 *   - 64-band mel filterbank (0–8000 Hz)
 *
 * Thread-safe for use on inference background thread.
 */
class MelSpectrogramExtractor
{
public:
    MelSpectrogramExtractor();

    /** @brief Process a new audio window and produce a mel spectrogram.
     *  @param audio 22050-sample mono window (512ms @ 44.1kHz)
     *  @param melOut output buffer of size 64*32 = 2048 floats, row-major [melBand][timeFrame]
     *  @return true if mel spectrogram was produced */
    bool process(const float* audio, float* melOut) noexcept;

    /** @brief Number of mel bands (height of output). */
    static constexpr int kMelBands = 64;

    /** @brief Number of time frames (width of output). */
    static constexpr int kTimeFrames = 32;

    /** @brief FFT size. */
    static constexpr int kFftSize = 2048;

    /** @brief Hop size between STFT frames, in samples. */
    static constexpr int kHopSize = 512;

    /** @brief Output tensor size: 64 * 32 = 2048 floats. */
    static constexpr int kOutputSize = kMelBands * kTimeFrames;

    /** @brief Sample rate the extractor was designed for. */
    static constexpr double kSampleRate = 44100.0;

private:
    void buildMelFilterbank() noexcept;
    void processFrame(const float* fftMagnitudes) noexcept;

    std::unique_ptr<juce::dsp::FFT> fft;

    // Hann window
    std::vector<float> window;

    // Mel filterbank: [kMelBands][kFftSize/2+1]
    std::vector<float> melWeights;

    // Rolling buffer of STFT spectra: [kTimeFrames][kFftSize/2+1]
    std::vector<float> spectraBuffer;
    int spectraWriteIdx = 0;

    // Scratch for FFT
    std::vector<float> fftScratch;
};
