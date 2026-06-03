#include "StructureTagger.h"
#include <algorithm>

void StructureTagger::prepare(double newSampleRate)
{
    sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
    pendingTransitionSec = 0.0;
    currentState = StructureState::SILENT;
    pendingState = StructureState::SILENT;
}

StructureState StructureTagger::computeDesiredState(float rms, float /*centroid*/, float peakRms) const
{
    const float silentFloor = std::max(kSilentRms, peakRms * kSilentPeakRatio);
    if (rms < silentFloor)
        return StructureState::SILENT;

    // AMBIENT: drone/feedback band between silent and clean playing
    if (rms < kAmbientCeil)
        return StructureState::AMBIENT;

    // BREAKDOWN: crushing heavy above LOUD
    if (rms >= kBreakdownRms)
        return StructureState::BREAKDOWN;

    // LOUD: palm-muted riffing or full-chord playing
    const float loudFloor = (peakRms > 0.0f)
        ? std::min(kLoudRms, std::max(kLoudRmsFloor, peakRms * kLoudPeakRatio))
        : kLoudRms;

    // SOFT-LOUD overlap zone: use sub-bass ratio to discriminate
    // Palm-mute chugs concentrate energy in 30-120 Hz → high subBassRatio
    // Clean arpeggios spread energy across spectrum → low subBassRatio
    if (rms >= kAmbientCeil && rms < kBreakdownRms)
    {
        if (subBassRatio >= kSubBassLoudFloor)
            return StructureState::LOUD;   // chug detected even if RMS below loudFloor
        if (subBassRatio <= kSubBassSoftCeil && rms < loudFloor)
            return StructureState::SOFT;   // clean playing even if RMS ambiguous
    }

    if (rms >= loudFloor)
        return StructureState::LOUD;

    // SOFT: clean arpeggios, sparse playing (catch-all between AMBIENT and LOUD)
    return StructureState::SOFT;
}

double StructureTagger::holdRequiredForTransition(StructureState from, StructureState to) const noexcept
{
    if (from == to)
        return 0.0;

    switch (from)
    {
        case StructureState::SILENT:
            return kHoldSilentSec;

        case StructureState::AMBIENT:
            if (to == StructureState::SILENT) return kHoldAmbientToSilentSec;
            return kHoldAmbientToSoftSec;  // AMBIENT→SOFT or AMBIENT→LOUD/BREAKDOWN

        case StructureState::SOFT:
            if (to == StructureState::SILENT)  return kHoldSoftToSilentSec;
            if (to == StructureState::AMBIENT) return kHoldSoftToAmbientSec;
            if (to == StructureState::LOUD)    return kHoldSoftToLoudSec;
            return kHoldSoftToLoudSec;  // SOFT→BREAKDOWN: same as SOFT→LOUD (must pass through LOUD)

        case StructureState::LOUD:
            if (to == StructureState::SILENT)    return kHoldLoudToSilentSec;
            if (to == StructureState::BREAKDOWN) return kHoldLoudToBreakdownSec;
            return kHoldLoudToSoftSec;  // LOUD→SOFT or LOUD→AMBIENT

        case StructureState::BREAKDOWN:
            if (to == StructureState::SILENT) return kHoldBreakdownToSilentSec;
            if (to == StructureState::LOUD)   return kHoldBreakdownToLoudSec;
            return kHoldBreakdownToSoftSec;  // BREAKDOWN→SOFT or BREAKDOWN→AMBIENT
    }

    return 0.0;
}

StructureState StructureTagger::update(float rms, float centroid, float /*highFreqFlux*/, int numSamples, float peakRms)
{
    const double blockSec = static_cast<double>(numSamples) / sampleRate;
    const StructureState desired = computeDesiredState(rms, centroid, peakRms);

    if (desired == currentState)
    {
        pendingTransitionSec = 0.0;
        pendingState = currentState;
        return currentState;
    }

    if (desired != pendingState)
    {
        pendingState = desired;
        pendingTransitionSec = 0.0;
    }

    pendingTransitionSec += blockSec;

    if (pendingTransitionSec < holdRequiredForTransition(currentState, desired))
        return currentState;

    currentState = desired;
    pendingState = desired;
    pendingTransitionSec = 0.0;
    return currentState;
}
