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

    // LOUD: palm-muted riffing or full-chord playing
    const float loudFloor = (peakRms > 0.0f)
        ? std::min(kLoudRms, std::max(kLoudRmsFloor, peakRms * kLoudPeakRatio))
        : kLoudRms;

    // SOFT-LOUD overlap zone: use sub-bass ratio to discriminate
    // Palm-mute chugs concentrate energy in 30-120 Hz → high subBassRatio
    // Clean arpeggios spread energy across spectrum → low subBassRatio
    if (subBassRatio >= kSubBassLoudFloor)
        return StructureState::LOUD;   // chug detected even if RMS below loudFloor
    if (subBassRatio <= kSubBassSoftCeil && rms < loudFloor)
        return StructureState::SOFT;   // clean playing even if RMS ambiguous

    if (rms >= loudFloor)
        return StructureState::LOUD;

    // SOFT: clean arpeggios, sparse playing
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

        case StructureState::SOFT:
            if (to == StructureState::SILENT) return kHoldSoftToSilentSec;
            return kHoldSoftToLoudSec;

        case StructureState::LOUD:
            if (to == StructureState::SILENT) return kHoldLoudToSilentSec;
            return kHoldLoudToSoftSec;
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
