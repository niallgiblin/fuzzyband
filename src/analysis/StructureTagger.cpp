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

    const float loudFloor = (peakRms > 0.0f)
        ? std::min(kLoudRms, std::max(kLoudRmsFloor, peakRms * kLoudPeakRatio))
        : kLoudRms;
    if (rms >= loudFloor)
        return StructureState::LOUD;

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
            return (to == StructureState::LOUD) ? kHoldSoftToLoudSec : kHoldSoftToSilentSec;
        case StructureState::LOUD:
            return (to == StructureState::SILENT) ? kHoldLoudToSilentSec : kHoldLoudToSoftSec;
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
