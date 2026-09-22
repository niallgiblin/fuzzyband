/**
 * @file
 * @brief YIN (cumulative mean normalized difference) pitch estimator.
 *
 * Search range targets roughly MIDI 21–64 (~55–500 Hz at 48 kHz): lag bounds derived from sample rate.
 * The low end (~55 Hz) covers drop-C tuning (C2 = 65.4 Hz); the 4096-sample ring holds several
 * periods at 55 Hz. CPU is bounded per block (O(n * lagRange)); max lag ~872 at 48 kHz.
 */

#include "PitchEstimator.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
float kHzToMidi(float hz)
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
    onsetWindow_.resize(static_cast<size_t>(kRingSize));
    const int maxTau = kRingSize / 2;
    d_.assign(static_cast<size_t>(maxTau + 1), 0.0f);
    cmndf_.assign(static_cast<size_t>(maxTau + 1), 0.0f);

    // Guitar-ish band ~55–500 Hz: lag = sr/freq must stay inside [minLag_, maxLag_]
    // (440 Hz @ 48 kHz → ~109 samples; do not set minLag_ above that.)
    // The 55 Hz floor covers drop-C (C2 = 65.4 Hz) and lower drop tunings.
    const double sr = sampleRate_;
    minLag_ = std::max(2, static_cast<int>(std::floor(sr / 500.0)));
    maxLag_ = std::min(kRingSize / 2 - 1, static_cast<int>(std::floor(sr / 55.0)));

    lastMidiNote_ = 40.0f;
    lastConfidence_ = 0.0f;
    hopSamples_ = std::max(1, static_cast<int>(std::lround(kHopSeconds * sampleRate_)));
    nextHopOffset_ = hopSamples_ - 1;
    hopCount_ = 0;
}

void PitchEstimator::reset()
{
    ringWrite_ = 0;
    ringFilled_ = 0;
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    lastMidiNote_ = 40.0f;
    lastConfidence_ = 0.0f;
    hopCount_ = 0;
    nextHopOffset_ = hopSamples_ - 1;
}

void PitchEstimator::process(const float* mono, int numSamples)
{
    hopCount_ = 0;
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

    // Fixed-hop estimates: YIN at fixed global positions, not once per block. A
    // block-rate estimate makes anything derived from it (the mirror's legato
    // pitch follow) depend on the host buffer size.
    int pos = nextHopOffset_;
    while (pos < numSamples && hopCount_ < kMaxHops)
    {
        recordHop(pos, numSamples);
        ++hopCount_;
        pos += hopSamples_;
    }
    nextHopOffset_ = pos - numSamples;

    if (hopCount_ > 0)
    {
        lastMidiNote_ = hopMidi_[static_cast<size_t>(hopCount_ - 1)];
        lastConfidence_ = hopConf_[static_cast<size_t>(hopCount_ - 1)];
    }
    else if (ringFilled_ < minLag_ + 64)
    {
        lastMidiNote_ = 40.0f;
        lastConfidence_ = 0.0f;
    }
}

void PitchEstimator::recordHop(int posInBlock, int numSamples)
{
    const size_t idx = static_cast<size_t>(hopCount_);
    hopOffset_[idx] = posInBlock;

    // Window is the kBlockWindow samples ending at posInBlock (INCLUSIVE — the
    // hop's last sample, matching EnergyAnalyser::getOnsetHopOffset).
    int w = std::min(ringFilled_, kBlockWindow);
    const int back = numSamples - 1 - posInBlock;   // samples from newest to window end
    if (back < 0)
    {
        hopMidi_[idx] = 40.0f;
        hopConf_[idx] = 0.0f;
        return;
    }
    if (back + w > ringFilled_)
        w = ringFilled_ - back;
    if (w < minLag_ + 64)
    {
        hopMidi_[idx] = 40.0f;
        hopConf_[idx] = 0.0f;
        return;
    }

    int start = ringWrite_ - back - w;
    while (start < 0)
        start += kRingSize;
    double energy = 0.0;
    for (int i = 0; i < w; ++i)
    {
        const float s = ring_[static_cast<size_t>((start + i) % kRingSize)];
        yinWindow_[static_cast<size_t>(i)] = s;
        energy += static_cast<double>(s) * s;
    }
    if (std::sqrt(energy / static_cast<double>(w)) < kMinRms)
    {
        hopMidi_[idx] = 40.0f;
        hopConf_[idx] = 0.0f;
        return;
    }

    const int tauMax = std::min(maxLag_, w / 2 - 1);
    const int tauMin = std::min(minLag_, std::max(2, tauMax - 1));
    float midi = 40.0f, conf = 0.0f;
    runYinRange(yinWindow_.data(), w, tauMin, tauMax, midi, conf);
    hopMidi_[idx] = midi;
    hopConf_[idx] = conf;
}

bool PitchEstimator::estimateOnset(std::int64_t blockEndAbs, std::int64_t onsetAbs,
                                   int length, float& midiOut, float& confOut) noexcept
{
    if (length < 64 || length > kRingSize || blockEndAbs <= 0)
        return false;

    // `back` is how far the window end sits behind the newest ring sample.
    const std::int64_t back = blockEndAbs - onsetAbs - static_cast<std::int64_t>(length);
    if (back < 0 || back + static_cast<std::int64_t>(length) > ringFilled_)
        return false;

    int start = ringWrite_ - static_cast<int>(back) - length;
    while (start < 0)
        start += kRingSize;
    for (int i = 0; i < length; ++i)
        onsetWindow_[static_cast<size_t>(i)] = ring_[static_cast<size_t>((start + i) % kRingSize)];

    // Allow taus up to almost the whole (short) window: a 1024-sample window
    // must still reach drop-C's ~674-sample period, even though that is < n/2.
    const int tauMax = std::min(maxLag_, length - 2);
    const int tauMin = std::min(minLag_, std::max(2, tauMax - 1));
    if (tauMin >= tauMax || tauMax < 2)
        return false;

    runYinRange(onsetWindow_.data(), length, tauMin, tauMax, midiOut, confOut);
    return true;
}

void PitchEstimator::runYinRange(const float* x, int n, int tauMin, int tauMax,
                                 float& midiOut, float& confOut)
{
    if (tauMin >= tauMax || tauMax < 2)
    {
        midiOut = 40.0f;
        confOut = 0.0f;
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

    // Relative tolerance: an ABSOLUTE tolerance scales with n but not with the
    // signal amplitude, so on a quiet window every lag qualifies and bestTau
    // collapses to tauMin (measured: a 0.002-amplitude E2 read as MIDI 71,
    // polluting the stable tracker). Tie it to the minimum instead.
    const float tol = minD * 0.1f + 1.0e-6f;
    int bestTau = tauMax;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        if (d_[static_cast<size_t>(tau)] <= minD + tol)
            bestTau = std::min(bestTau, tau);
    }

    if (bestTau < 2)
    {
        midiOut = 40.0f;
        confOut = 0.0f;
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

    if (!std::isfinite(hz) || hz < 50.0 || hz > 500.0)
    {
        midiOut = 40.0f;
        confOut = 0.0f;
        return;
    }

    midiOut = kHzToMidi(static_cast<float>(hz));

    // Confidence: classic YIN — how deep the CMNDF dips at the CHOSEN lag.
    //
    // The previous formula measured the spread between the two smallest CMNDF
    // samples. On a real DI the CMNDF trough is smooth, so that spread is
    // ~0.0003 for every window and confidence came out 0.00 *even when the
    // estimate was correct* (a pure sine, by contrast, dips below 0.05 and
    // scored ~1.0 — which is why the synthetic unit tests never caught it).
    // Four consumers gate on this value (`flushMirrorTriggers` conf > 0.25,
    // `StablePitchTracker` 0.20, `PhraseLearner` 0.30/0.05), so on real audio
    // the whole pitch chain silently fell back to stale notes. See
    // docs/BASS_MIRRORING.md §16.
    const float cmndfAtBest = cmndf_[static_cast<size_t>(bestTau)];
    const float conf = std::clamp(1.0f - cmndfAtBest, 0.0f, 1.0f);
    confOut = conf;
}
