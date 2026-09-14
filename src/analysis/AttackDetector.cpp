#include "AttackDetector.h"

#include <algorithm>
#include <cmath>

void AttackDetector::prepare(double sampleRate) noexcept
{
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    // Time-based, so the detector behaves the same at every host buffer size.
    fallWindowSamples_ = static_cast<std::int64_t>(std::llround(kFallWindowSeconds * sampleRate_));
    reset();
}

void AttackDetector::reset() noexcept
{
    prevRms_ = 0.0f;
    rmsSmooth_ = 0.0f;
    lastFallSample_ = -1;
    rmsFloorSinceArm_ = 0.0f;
    risePending_ = false;
    lastAttackSample_ = 0;
}

AttackVerdict AttackDetector::classify(float rms, std::int64_t sampleTime) noexcept
{
    AttackVerdict v;

    const float prevRms = prevRms_;   // previous block's level, before this one
    prevRms_ = rms;
    rmsSmooth_ = 0.85f * rmsSmooth_ + 0.15f * rms;

    // Recent-decay tracking runs even for near-silent blocks: a note decaying
    // into silence IS a fall, and it arms the next note's attack. Any decrease
    // counts (T6.5); the window is a *time* window, not a block count (A1).
    const bool fell = (rms < prevRms);
    if (fell)
    {
        lastFallSample_ = sampleTime;
        rmsFloorSinceArm_ = rms;
    }
    else if (lastFallSample_ >= 0 && (sampleTime - lastFallSample_) < fallWindowSamples_)
    {
        if (rms < rmsFloorSinceArm_)
            rmsFloorSinceArm_ = rms;
    }

    v.riseRatio = (rmsSmooth_ > 0.0f) ? rms / rmsSmooth_ : 0.0f;

    if (rms < kSilenceFloor)
    {
        risePending_ = false;
        v.blockedBy = AttackVerdict::Blocked::BelowAmplitudeFloor;
        return v;
    }

    // A note attack is a sharp rise that follows a *recent decay* — real picking
    // produces per-note pulses (rise → fall → rise). `clearsFloor` additionally
    // demands the rise be a real jump out of the trough: a low (drop-C) note
    // ripples through the sliding onset window, and a bare rise ratio fired on
    // that ripple, so a single pick mirrored as two or three notes.
    v.armed = (lastFallSample_ >= 0)
           && (sampleTime - lastFallSample_) < fallWindowSamples_;
    v.sharpRise = (rms > rmsSmooth_ * kRiseVsSmooth) || (rms > prevRms * kRiseVsPrev);
    v.troughMargin = rms - (rmsFloorSinceArm_ * kFloorRise + kFloorAbs);
    v.clearsFloor = (v.troughMargin > 0.0f);
    v.aboveFloor = (rms > kAmplitudeFloor);

    const bool riseEdge = v.armed && v.sharpRise && v.clearsFloor && v.aboveFloor;
    if (v.armed && v.sharpRise && v.aboveFloor)
    {
        ++debug_.riseEdges;
        if (v.clearsFloor) ++debug_.clearedFloor;
        else               ++debug_.blockedByFloor;
    }
    if (riseEdge)
        risePending_ = true;

    v.gateOpen = (sampleTime - lastAttackSample_) > kMinAttackIntervalSamples;

    if (!risePending_ || !v.gateOpen)
    {
        // Report the first term that refused, in evaluation order; a latched
        // edge held behind the gate is the gate's refusal.
        if (risePending_ && !v.gateOpen)
            v.blockedBy = AttackVerdict::Blocked::MinIntervalGate;
        else if (!v.aboveFloor)
            v.blockedBy = AttackVerdict::Blocked::BelowAmplitudeFloor;
        else if (!v.armed)
            v.blockedBy = AttackVerdict::Blocked::NoRecentFall;
        else if (!v.sharpRise)
            v.blockedBy = AttackVerdict::Blocked::NoSharpRise;
        else
            v.blockedBy = AttackVerdict::Blocked::TroughTooShallow;
        return v;
    }

    // Accepted. Consume the decay→rise edge and the trough so a slow swell or a
    // long sustained note cannot retrigger the mirror while its level climbs.
    v.accepted = true;
    v.blockedBy = AttackVerdict::Blocked::None;
    ++debug_.accepted;
    lastAttackSample_ = sampleTime;
    lastFallSample_ = -1;
    risePending_ = false;
    rmsFloorSinceArm_ = rms;
    return v;
}
