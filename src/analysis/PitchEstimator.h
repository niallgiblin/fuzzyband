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

private:
    void runYin(const float* x, int n);

    double sampleRate_ = 44100.0;

    float lastMidiNote_ = 40.0f;
    float lastConfidence_ = 0.0f;

    static constexpr int kRingSize = 4096;
    std::vector<float> ring_;
    int ringWrite_ = 0;
    int ringFilled_ = 0;

    std::vector<float> yinWindow_;
    std::vector<float> d_;
    std::vector<float> cmndf_;

    int minLag_ = 1;
    int maxLag_ = 1;
};
