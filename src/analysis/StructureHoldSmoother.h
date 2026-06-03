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
    // Hold times mirror StructureTagger (scaled for sludge metal pacing)
    static constexpr double kHoldSilentSec    = 0.0;
    static constexpr double kHoldAmbientSec   = 6.0;
    static constexpr double kHoldSoftSec      = 6.0;   // was 2.0 (×3)
    static constexpr double kHoldLoudSec      = 6.0;   // was 2.5 (×2.4)
    static constexpr double kHoldBreakdownSec = 8.0;

    double timeInStateSec = 0.0;
    StructureState currentState = StructureState::SILENT;
};
