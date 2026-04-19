#include "StructureTagger.h"
#include <algorithm>

void StructureTagger::prepare(double newSampleRate)
{
    sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
    timeInStateSec = 0.0;
    currentState = StructureState::SILENT;
}

StructureState StructureTagger::computeDesiredState(float rms, float centroid, float peakRms) const
{
    const float silentFloor = std::max(kSilentRms, peakRms * 0.15f);
    if (rms < silentFloor)
        return StructureState::SILENT;

    if (centroid < kBreakdownCentroidHz)
        return StructureState::BREAKDOWN;

    if (centroid < kVerseCentroidHz)
        return StructureState::VERSE;

    return StructureState::CHORUS;
}

StructureState StructureTagger::update(float rms, float centroid, float /*highFreqFlux*/, int numSamples, float peakRms)
{
    const double blockSec = static_cast<double>(numSamples) / sampleRate;
    const StructureState desired = computeDesiredState(rms, centroid, peakRms);

    if (desired == currentState)
    {
        timeInStateSec = 0.0;
        return currentState;
    }

    timeInStateSec += blockSec;

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
