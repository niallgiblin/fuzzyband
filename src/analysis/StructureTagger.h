#pragma once

/**
 * @file
 * @brief Guitar “section” state machine (SILENT / VERSE / CHORUS / BREAKDOWN).
 */

/**
 * @brief High-level musical section inferred from energy and timbre features.
 */
enum class StructureState
{
    SILENT,
    VERSE,
    CHORUS,
    BREAKDOWN
};

/**
 * @brief Hysteresis-gated state machine mapping analyser outputs to @ref StructureState.
 *
 * `update` should be called once per audio block with values from @ref EnergyAnalyser.
 */
class StructureTagger
{
public:
    void prepare(double sampleRate);

    /** @brief Advance state from latest RMS, centroid, and HF flux for this block. */
    StructureState update(float rms, float centroid, float highFreqFlux, int numSamples);

    StructureState getCurrentState() const { return currentState; }

private:
    StructureState computeDesiredState(float rms, float centroid) const;

    double sampleRate = 44100.0;
    double timeInStateSec = 0.0;
    StructureState currentState = StructureState::SILENT;
    static constexpr float kSilentRms = 0.05f;
    static constexpr float kBreakdownCentroidHz = 1000.0f;
    static constexpr float kVerseCentroidHz = 2200.0f;
    static constexpr double kHoldSilentSec    = 0.0;  // D-07: immediate exit from SILENT
    static constexpr double kHoldVerseSec     = 2.0;
    static constexpr double kHoldChorusSec    = 2.5;
    static constexpr double kHoldBreakdownSec = 2.5;
};
