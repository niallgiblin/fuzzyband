#include "TempoStabiliser.h"
#include <cmath>
#include <juce_core/juce_core.h>

void TempoStabiliser::reset() noexcept
{
    stablePlaybackBpm       = 120.0f;
    pendingCandidateBpm     = 120.0f;
    pendingCandidateSamples = 0;
}

float TempoStabiliser::update(float candidateBpm, bool playbackOpen,
                              int numSamples, double sampleRate) noexcept
{
    const float candidate = juce::jlimit(80.0f, 220.0f, candidateBpm);

    if (!playbackOpen)
    {
        pendingCandidateBpm     = candidate;
        pendingCandidateSamples = 0;
        return candidate;
    }

    if (std::abs(candidate - stablePlaybackBpm) <= kTempoChangeDeadbandBpm)
    {
        pendingCandidateBpm     = candidate;
        pendingCandidateSamples = 0;
        stablePlaybackBpm = stablePlaybackBpm + 0.05f * (candidate - stablePlaybackBpm);
        return stablePlaybackBpm;
    }

    if (std::abs(candidate - pendingCandidateBpm) > kTempoChangeDeadbandBpm)
    {
        pendingCandidateBpm     = candidate;
        pendingCandidateSamples = 0;
    }

    pendingCandidateSamples += numSamples;
    const int holdSamples = static_cast<int>(kTempoChangeHoldSec * sampleRate);
    if (pendingCandidateSamples >= holdSamples)
    {
        pendingCandidateSamples = 0;
        stablePlaybackBpm = candidate;
        return candidate;
    }

    return stablePlaybackBpm;
}
