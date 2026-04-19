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
        case StructureState::VERSE:
            holdRequired = kHoldVerseSec;
            break;
        case StructureState::CHORUS:
            holdRequired = kHoldChorusSec;
            break;
        case StructureState::BREAKDOWN:
            holdRequired = kHoldBreakdownSec;
            break;
    }

    if (timeInStateSec < holdRequired - 1e-9)
        return currentState;

    currentState = desired;
    timeInStateSec = 0.0;
    return currentState;
}
