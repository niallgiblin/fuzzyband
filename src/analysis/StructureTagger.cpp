#include "StructureTagger.h"

void StructureTagger::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
    timeInStateSec = 0.0;
    currentState = StructureState::SILENT;
}

StructureState StructureTagger::computeDesiredState(float rms, float centroid) const
{
    if (rms < kSilentRms)
        return StructureState::SILENT;

    if (centroid < kBreakdownCentroidHz)
        return StructureState::BREAKDOWN;

    if (centroid < kVerseCentroidHz)
        return StructureState::VERSE;

    return StructureState::CHORUS;
}

StructureState StructureTagger::update(float rms, float centroid, float /*highFreqFlux*/, int numSamples)
{
    const double blockSec = static_cast<double>(numSamples) / sampleRate;
    timeInStateSec += blockSec;

    const StructureState desired = computeDesiredState(rms, centroid);

    if (desired == currentState)
        return currentState;

    double holdRequired = 0.0;
    switch (currentState)
    {
        case StructureState::SILENT:    holdRequired = kHoldSilentSec;    break;
        case StructureState::VERSE:     holdRequired = kHoldVerseSec;     break;
        case StructureState::CHORUS:    holdRequired = kHoldChorusSec;    break;
        case StructureState::BREAKDOWN: holdRequired = kHoldBreakdownSec; break;
    }
    if (timeInStateSec < holdRequired)
        return currentState;

    currentState = desired;
    timeInStateSec = 0.0;
    return currentState;
}
