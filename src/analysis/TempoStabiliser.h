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

    /** @brief Prime the stabiliser with a saved BPM before the next update cycle.
        Sets stable and pending BPM to the given value and resets the hold counter.
        Call from processBlock after a silence reset to carry forward pre-silence BPM. */
    void warmStart(float bpm) noexcept;

    /** @brief Update with latest BPM candidate. Returns the new stable BPM.
        @param candidateBpm  Raw BPM estimate (clamped to [40, 300] internally).
        @param playbackOpen  True when the playback gate is open.
        @param numSamples    Block size in samples (for accumulating hold time).
        @param sampleRate    Current sample rate. */
    float update(float candidateBpm, bool playbackOpen,
                 int numSamples, double sampleRate) noexcept;

    /** @brief Current stable BPM (updated by update()). */
    float getStableBpm() const noexcept { return stablePlaybackBpm; }

private:
    static constexpr float  kMinStableBpm            = 40.0f;
    static constexpr float  kMaxStableBpm            = 300.0f;
    static constexpr float  kTempoChangeDeadbandBpm = 8.0f;
    static constexpr double kTempoChangeHoldSec      = 6.0;

    float stablePlaybackBpm        = 120.0f;
    float pendingCandidateBpm      = 120.0f;
    int   pendingCandidateSamples  = 0;
};
