#include "BeatTracker.h"

#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

namespace
{
constexpr int kFftOrder = 11;
constexpr int kFftSize = 1 << kFftOrder;

int hopForOnsetDefaults()
{
    const int fftSize = kFftSize;
    const int hop = juce::jmax(256, fftSize / 4);
    return hop;
}
} // namespace

BeatTracker::BeatTracker() = default;

void BeatTracker::prepare(double newSampleRate, int maxBlockSize)
{
    (void)maxBlockSize;
    sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
    hopSize = hopForOnsetDefaults();
    osfRate = sampleRate / static_cast<double>(hopSize);

    // ≥4 s of OSF storage (ring buffer capacity)
    const int cap = juce::jmax(256, static_cast<int>(std::ceil(osfRate * 4.5)));
    ring.assign(static_cast<size_t>(cap), 0.0f);
    lagScratch.assign(static_cast<size_t>(std::ceil(osfRate * 3.0)) + 8u, 0.0f);

    ringWrite = 0;
    ringCount = 0;

    reset();
}

void BeatTracker::reset() noexcept
{
    ringWrite = 0;
    ringCount = 0;
    if (!ring.empty())
        std::fill(ring.begin(), ring.end(), 0.0f);

    hopCounter = 0;
    smoothedBpm = 120.0f;
    confidence = 0.0f;
    locked = false;
    beatPhase01 = 0.0;
    highConfHops = 0;
    lowConfHops = 0;
}

void BeatTracker::pushFluxSample(float flux, int64_t totalSamples) noexcept
{
    (void)totalSamples;
    if (ring.empty())
        return;

    ring[static_cast<size_t>(ringWrite)] = flux;
    ringWrite = (ringWrite + 1) % static_cast<int>(ring.size());
    ringCount = juce::jmin(static_cast<int>(ring.size()), ringCount + 1);
    ++hopCounter;

    recompute();
}

void BeatTracker::recompute() noexcept
{
    if (ringCount < 128)
    {
        confidence = 0.0f;
        locked = false;
        beatPhase01 = 0.0;
        return;
    }

    const int cap = static_cast<int>(ring.size());
    const int W = juce::jmin(ringCount, static_cast<int>(std::floor(osfRate * 3.0))); // ~3 s window

    const int oldestIdx = (ringWrite - ringCount + cap * 2) % cap;

    if (static_cast<size_t>(W) > lagScratch.size())
    {
        confidence = 0.0f;
        return;
    }

    for (int i = 0; i < W; ++i)
    {
        const int idx = (oldestIdx + (ringCount - W) + i) % cap;
        lagScratch[static_cast<size_t>(i)] = ring[static_cast<size_t>(idx)];
    }

    double mean = 0.0;
    for (int i = 0; i < W; ++i)
        mean += static_cast<double>(lagScratch[static_cast<size_t>(i)]);
    mean /= static_cast<double>(W);

    float bestScore = 0.0f;
    float secondScore = 0.0f;
    float bestBpm = smoothedBpm;

    for (int bpm_i = 80; bpm_i <= 220; ++bpm_i)
    {
        const float bpmCand = static_cast<float>(bpm_i);
        const double periodSec = 60.0 / static_cast<double>(bpmCand);
        int lag = static_cast<int>(std::lround(periodSec * osfRate));
        lag = juce::jmax(2, lag);
        if (lag >= W / 2)
            continue;

        double acc = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        for (int i = lag; i < W; ++i)
        {
            const double a = static_cast<double>(lagScratch[static_cast<size_t>(i)]) - mean;
            const double b = static_cast<double>(lagScratch[static_cast<size_t>(i - lag)]) - mean;
            acc += a * b;
            energyA += a * a;
            energyB += b * b;
        }

        const double denom = std::sqrt(energyA * energyB) + 1.0e-9;
        const float score = static_cast<float>(std::max(0.0, acc / denom));
        if (score > bestScore)
        {
            secondScore = bestScore;
            bestScore = score;
            bestBpm = bpmCand;
        }
        else if (score > secondScore)
        {
            secondScore = score;
        }
    }

    // Octave disambiguation — clean DI often produces strong pick + palm-mute subdivisions.
    // Fold near-double-time peaks back toward the current musical pulse.
    {
        const float prior = smoothedBpm;
        if (prior > 0.0f)
        {
            if (bestBpm / prior > 1.55f) bestBpm *= 0.5f;
            else if (bestBpm / prior < 0.6f) bestBpm *= 2.0f;
        }
        bestBpm = juce::jlimit(kMinBpm, kMaxBpm, bestBpm);
    }

    if (bestScore <= 0.0f)
    {
        confidence *= 0.92f;
        if (confidence < 0.005f)
        {
            locked = false;
            confidence = 0.0f;
        }
    }
    else
    {
        const float ratio = (secondScore > 0.0f) ? (bestScore - secondScore) / (bestScore + secondScore + 1.0e-4f)
                                                 : 1.0f;
        confidence = juce::jlimit(0.0f, 1.0f, ratio * 0.65f + bestScore * 0.35f);

        // EMA toward best peak
        smoothedBpm += 0.15f * (bestBpm - smoothedBpm);
        smoothedBpm = juce::jlimit(kMinBpm, kMaxBpm, smoothedBpm);
    }

    // Phase: advance by inferred period
    {
        const double periodSec = 60.0 / static_cast<double>(smoothedBpm);
        const double periodHops = periodSec * osfRate;
        if (periodHops > 1.0e-6)
            beatPhase01 = std::fmod(static_cast<double>(hopCounter) / periodHops, 1.0);
    }

    // Lock / unlock hysteresis (~1 bar cumulative high confidence)
    const double barSecs = 240.0 / static_cast<double>(juce::jmax(1.0f, smoothedBpm));
    const double hopSecs = static_cast<double>(hopSize) / sampleRate;
    const int barHops = juce::jmax(8, static_cast<int>(std::ceil(barSecs / hopSecs)));

    if (confidence >= kConfHigh)
    {
        ++highConfHops;
        lowConfHops = 0;
        if (!locked && highConfHops >= barHops)
            locked = true;
    }
    else if (confidence <= kConfLow)
    {
        highConfHops = 0;
        ++lowConfHops;
        if (locked && lowConfHops >= juce::jmax(4, barHops / 2))
            locked = false;
    }
    else
    {
        highConfHops = juce::jmin(highConfHops, barHops);
    }
}
