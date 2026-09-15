#pragma once

/**
 * @file
 * @brief Monophonic pitch estimate (YIN) for guitar analysis (PITCH-01).
 */

#include <cstddef>
#include <vector>

/**
 * @brief Real-time YIN pitch estimator; buffers audio for stable lag search (no heap in steady-state process).
 */
class PitchEstimator
{
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    /** @brief Analyse @p mono; real-time safe — uses only preallocated storage after @ref prepare. */
    void process(const float* mono, int numSamples);
    /** @brief Last estimated pitch as continuous MIDI note number (after @ref process). */
    float getMidiNote() const noexcept { return lastMidiNote_; }
    /** @brief [0,1] confidence from YIN CMNDF minima (PITCH-02). */
    float getConfidence() const noexcept { return lastConfidence_; }

    /**
     * @brief Onset-aligned pitch: YIN over the window that STARTS at a pick.
     *
     * The block-level @ref process estimate uses a window that ends at the
     * current sample, so at the instant of a pick it is still dominated by the
     * PREVIOUS note — the mirror then plays one note behind (measured on real
     * DIs: pitch-class match ~50% vs 97% for this window). This estimates the
     * pitch from `[onsetAbs, onsetAbs+length)` once those samples are in the
     * ring, i.e. `length` samples after the pick.
     *
     * @param blockEndAbs absolute sample one past the newest ring sample
     *                    (hostSampleTime + numSamples after @ref process).
     * @param onsetAbs    absolute sample of the pick.
     * @param length      window length (>0, <= kRingSize).
     * @return true when the whole window is present and a pitch was estimated.
     */
    bool estimateOnset(std::int64_t blockEndAbs, std::int64_t onsetAbs, int length,
                       float& midiOut, float& confOut) noexcept;

    /** @brief Ring capacity in samples (the valid range of @ref estimateOnset windows). */
    static constexpr int getRingSize() noexcept { return kRingSize; }

private:
    void runYin(const float* x, int n);
    void runYinRange(const float* x, int n, int tauMin, int tauMax,
                     float& midiOut, float& confOut);

    double sampleRate_ = 44100.0;

    float lastMidiNote_ = 40.0f;
    float lastConfidence_ = 0.0f;

    static constexpr int kRingSize = 4096;
    std::vector<float> ring_;
    int ringWrite_ = 0;
    int ringFilled_ = 0;

    std::vector<float> yinWindow_;
    std::vector<float> onsetWindow_;
    std::vector<float> d_;
    std::vector<float> cmndf_;

    // Block-level estimate window. Kept at ~2048 (46 ms at 44.1 kHz): long
    // enough for drop-C, short enough that the block estimate is not dominated
    // by the previous note. The mirror uses @ref estimateOnset instead.
    static constexpr int kBlockWindow = 2048;

    int minLag_ = 1;
    int maxLag_ = 1;
};
