#pragma once

/**
 * @file
 * @brief Hold/hysteresis smoothing for structure class predictions (mirrors @ref StructureTagger).
 */

#include "StructureTagger.h"

/**
 * @brief Same min-hold semantics as @ref StructureTagger::update for transitions between @ref StructureState values.
 *
 * Input @a desired is the structure class to move toward (e.g. ML argmax or rule state). RMS/centroid
 * thresholding is not applied here — callers pass an already-decided class.
 */
class StructureHoldSmoother
{
public:
    void reset();

    /** @brief Advance hold timers and possibly transition toward @a desired. */
    StructureState advance(StructureState desired, double dtSec);

    StructureState getCurrentState() const noexcept { return currentState; }

private:
    static constexpr double kHoldSilentSec = 0.0;
    static constexpr double kHoldVerseSec = 2.0;
    static constexpr double kHoldChorusSec = 2.5;
    static constexpr double kHoldBreakdownSec = 2.5;

    double timeInStateSec = 0.0;
    StructureState currentState = StructureState::SILENT;
};
