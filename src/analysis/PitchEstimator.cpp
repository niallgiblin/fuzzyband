/**
 * @file
 * @brief YIN (cumulative mean normalized difference) pitch estimator.
 *
 * Search range targets roughly MIDI 28–64 (~82–329 Hz at 48 kHz): lag bounds derived from sample rate.
 * CPU is bounded per block (O(n * lagRange)); max lag ~600 at 48 kHz.
 */

#include "PitchEstimator.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr float kHzToMidi(float hz)
{
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}
} // namespace

void PitchEstimator::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    (void)maxBlockSize;

    ring_.assign(static_cast<size_t>(kRingSize), 0.0f);
    ringWrite_ = 0;
    ringFilled_ = 0;

    yinWindow_.resize(static_cast<size_t>(kRingSize));
    const int maxTau = kRingSize / 2;
    d_.assign(static_cast<size_t>(maxTau + 1), 0.0f);
    cmndf_.assign(static_cast<size_t>(maxTau + 1), 0.0f);

    // Guitar-ish band ~75–500 Hz: lag = sr/freq must stay inside [minLag_, maxLag_]
    // (440 Hz @ 48 kHz → ~109 samples; do not set minLag_ above that.)
    const double sr = sampleRate_;
    minLag_ = std::max(2, static_cast<int>(std::floor(sr / 500.0)));
    maxLag_ = std::min(kRingSize / 2 - 1, static_cast<int>(std::floor(sr / 75.0)));

    lastMidiNote_ = 40.0f;
    lastConfidence_ = 0.0f;
}

void PitchEstimator::reset()
{
    ringWrite_ = 0;
    ringFilled_ = 0;
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    lastMidiNote_ = 40.0f;
    lastConfidence_ = 0.0f;
}

void PitchEstimator::process(const float* mono, int numSamples)
{
    if (mono == nullptr || numSamples <= 0)
    {
        lastMidiNote_ = 40.0f;
        lastConfidence_ = 0.0f;
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        ring_[static_cast<size_t>(ringWrite_)] = mono[i];
        ringWrite_ = (ringWrite_ + 1) % kRingSize;
        if (ringFilled_ < kRingSize)
            ++ringFilled_;
    }

    const int n = ringFilled_;
    if (n < minLag_ + 64)
    {
        lastMidiNote_ = 40.0f;
        lastConfidence_ = 0.0f;
        return;
    }

    int oldest = ringWrite_ - ringFilled_;
    while (oldest < 0)
        oldest += kRingSize;
    for (int i = 0; i < n; ++i)
    {
        yinWindow_[static_cast<size_t>(i)] =
            ring_[static_cast<size_t>((oldest + i) % kRingSize)];
    }

    runYin(yinWindow_.data(), n);
}

void PitchEstimator::runYin(const float* x, int n)
{
    const int tauMax = std::min(maxLag_, n / 2 - 1);
    const int tauMin = std::min(minLag_, std::max(2, tauMax - 1));
    if (tauMin >= tauMax || tauMax < 2)
    {
        lastMidiNote_ = 40.0f;
        lastConfidence_ = 0.0f;
        return;
    }

    // Step 1: YIN difference d(tau), then cumulative mean normalized (CMNDF) for confidence
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double acc = 0.0;
        const int limit = n - tau;
        for (int i = 0; i < limit; ++i)
        {
            const double diff = static_cast<double>(x[i]) - static_cast<double>(x[i + tau]);
            acc += diff * diff;
        }
        d_[static_cast<size_t>(tau)] = static_cast<float>(acc);
    }

    // Cumulative sum for CMNDF denominator
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += static_cast<double>(d_[static_cast<size_t>(tau)]);
        if (running <= 1.0e-12)
        {
            cmndf_[static_cast<size_t>(tau)] = 1.0f;
            continue;
        }
        const double cmndf = static_cast<double>(d_[static_cast<size_t>(tau)]) * static_cast<double>(tau) / running;
        cmndf_[static_cast<size_t>(tau)] =
            std::clamp(static_cast<float>(cmndf), 0.0f, 1.0f);
    }

    // Smallest lag among argmin of d(tau): for periodic signals d is zero at T,2T,…; smallest is the fundamental.
    float minD = d_[static_cast<size_t>(tauMin)];
    for (int tau = tauMin + 1; tau <= tauMax; ++tau)
        minD = std::min(minD, d_[static_cast<size_t>(tau)]);

    const float tol = 1.0e-5f * static_cast<float>(std::max(1, n));
    int bestTau = tauMax;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (d_[static_cast<size_t>(tau)] <= minD + tol)
            bestTau = std::min(bestTau, tau);
    }

    if (bestTau < 2)
    {
        lastMidiNote_ = 40.0f;
        lastConfidence_ = 0.0f;
        return;
    }

    // Parabolic interpolation on d(tau) (same curve as fundamental pick). Skip fractional
    // refinement at band edges so d_[t±1] are always from this frame's YIN loop (see phase 11 review).
    const int t = bestTau;
    float offset = 0.0f;
    if (t >= tauMin + 1 && t <= tauMax - 1)
    {
        const float y0 = d_[static_cast<size_t>(t - 1)];
        const float y1 = d_[static_cast<size_t>(t)];
        const float y2 = d_[static_cast<size_t>(t + 1)];
        const float denom = y0 - 2.0f * y1 + y2;
        if (std::abs(denom) > 1.0e-8f)
            offset = 0.5f * (y0 - y2) / denom;
    }

    const float tauInterp = static_cast<float>(t) + std::clamp(offset, -0.5f, 0.5f);
    const double hz = static_cast<double>(sampleRate_) / static_cast<double>(tauInterp);

    if (!std::isfinite(hz) || hz < 60.0 || hz > 500.0)
    {
        lastMidiNote_ = 40.0f;
        lastConfidence_ = 0.0f;
        return;
    }

    lastMidiNote_ = kHzToMidi(static_cast<float>(hz));

    // Confidence: CMNDF depth at chosen lag + separation from next dip
    float firstMin = 1.0f;
    float secondMin = 1.0f;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        const float v = cmndf_[static_cast<size_t>(tau)];
        if (v < firstMin)
        {
            secondMin = firstMin;
            firstMin = v;
        }
        else if (v < secondMin && std::abs(tau - bestTau) > 2)
        {
            secondMin = v;
        }
    }
    const float spread = std::max(0.0f, secondMin - firstMin);
    float conf = std::clamp(spread * 4.0f, 0.0f, 1.0f);
    if (firstMin < 0.05f)
        conf = std::max(conf, 1.0f - firstMin * 2.0f);
    lastConfidence_ = std::clamp(conf, 0.0f, 1.0f);

    if (firstMin > 0.5f)
        lastConfidence_ *= 0.5f;
}
