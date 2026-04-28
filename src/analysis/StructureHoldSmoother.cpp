#include "StructureHoldSmoother.h"

void StructureHoldSmoother::reset()
{
    timeInStateSec = 0.0;
    currentState = StructureState::SILENT;
}

StructureState StructureHoldSmoother::advance(StructureState desired, double dtSec)
{
    if (desired == currentState)
    {
        timeInStateSec = 0.0;
        return currentState;
    }

    if (dtSec > 0.0)
        timeInStateSec += dtSec;

    double holdRequired = 0.0;
    switch (currentState)
    {
        case StructureState::SILENT:
            holdRequired = kHoldSilentSec;
            break;
        case StructureState::SOFT:
            holdRequired = kHoldSoftSec;
            break;
        case StructureState::LOUD:
            holdRequired = kHoldLoudSec;
            break;
    }

    if (timeInStateSec < holdRequired - 1e-9)
        return currentState;

    currentState = desired;
    timeInStateSec = 0.0;
    return currentState;
}
