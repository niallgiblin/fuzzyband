#pragma once

/**
 * @file
 * @brief BPM hysteresis: holds stable playback tempo against short-term fluctuation.
 */

/**
 * @brief Smooths BPM candidate stream with deadband + hold-period hysteresis.
 *
 * Value member of AccompanimentProcessor. All methods are noexcept and allocate nothing.
 */
class TempoStabiliser
{
public:
    /** @brief Reset to 120 BPM. Call from prepareToPlay() and processBlockBypassed(). */
    void reset() noexcept;

    /** @brief Update with latest BPM candidate. Returns the new stable BPM.
        @param candidateBpm  Raw BPM estimate (clamped to [80, 220] internally).
        @param playbackOpen  True when the playback gate is open.
        @param numSamples    Block size in samples (for accumulating hold time).
        @param sampleRate    Current sample rate. */
    float update(float candidateBpm, bool playbackOpen,
                 int numSamples, double sampleRate) noexcept;

    /** @brief Current stable BPM (updated by update()). */
    float getStableBpm() const noexcept { return stablePlaybackBpm; }

private:
    static constexpr float  kTempoChangeDeadbandBpm = 4.0f;
    static constexpr double kTempoChangeHoldSec      = 2.0;

    float stablePlaybackBpm        = 120.0f;
    float pendingCandidateBpm      = 120.0f;
    int   pendingCandidateSamples  = 0;
};
