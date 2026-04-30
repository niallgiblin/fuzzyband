#pragma once

/**
 * @file
 * @brief Comb-style tempo + phase estimation from onset-strength function (OSF) samples.
 */

#include <cstdint>
#include <vector>

/**
 * @brief Real-time-safe beat tracker fed one spectral-flux hop at a time from @ref OnsetDetector.
 *
 * Buffers are allocated only in @ref prepare(). @ref pushFluxSample must not allocate.
 * BPM is clamped to [80, 220]. Confidence drives merge with IOI BPM in @ref AccompanimentProcessor.
 */
class BeatTracker
{
public:
    BeatTracker();
    ~BeatTracker() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** @brief One OSF value per FFT hop (same cadence as @ref OnsetDetector). */
    void pushFluxSample(float flux, int64_t totalSamples) noexcept;

    /** @brief Smoothed BPM in [80, 220]. */
    float getBpm() const noexcept { return smoothedBpm; }

    /** @brief Combined confidence in [0, 1]. */
    float getConfidence() const noexcept { return confidence; }

    /** @brief Hysteresis: true after high confidence sustained ~one bar. */
    bool isLocked() const noexcept { return locked; }

    /** @brief Beat phase in [0, 1) for the current beat (visualisation / tests). */
    double getBeatPhase01() const noexcept { return beatPhase01; }

private:
    void recompute() noexcept;

    double sampleRate = 44100.0;
    int hopSize = 512;
    double osfRate = 86.0;

    std::vector<float> ring;
    std::vector<float> lagScratch; // fixed cap — filled in @ref recompute (no realloc on audio thread)
    int ringWrite = 0;
    int ringCount = 0;

    int64_t hopCounter = 0;

    float smoothedBpm = 120.0f;
    float confidence = 0.0f;
    bool locked = false;
    double beatPhase01 = 0.0;

    int highConfHops = 0;
    int lowConfHops = 0;

    static constexpr float kMinBpm = 80.0f;
    static constexpr float kMaxBpm = 220.0f;
    static constexpr float kConfHigh = 0.55f;
    static constexpr float kConfLow = 0.35f;
};
