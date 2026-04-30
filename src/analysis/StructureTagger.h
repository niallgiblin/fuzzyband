#pragma once

/**
 * @file
 * @brief Guitar "section" state machine (SILENT / SOFT / LOUD).
 */

/**
 * @brief High-level musical section inferred from energy features.
 */
enum class StructureState
{
    SILENT,
    SOFT,
    LOUD
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

    /** @brief Advance state from latest RMS, centroid, HF flux, and peak RMS for this block. */
    StructureState update(float rms, float centroid, float highFreqFlux, int numSamples, float peakRms = 0.0f);

    StructureState getCurrentState() const { return currentState; }

private:
    StructureState computeDesiredState(float rms, float centroid, float peakRms) const;
    double holdRequiredForTransition(StructureState from, StructureState to) const noexcept;

    double sampleRate = 44100.0;
    double pendingTransitionSec = 0.0;
    StructureState currentState = StructureState::SILENT;
    StructureState pendingState = StructureState::SILENT;
    static constexpr float kSilentRms = 0.012f;
    static constexpr float kSilentPeakRatio = 0.02f;
    static constexpr float kLoudRmsFloor = 0.055f;
    static constexpr float kLoudRms = 0.075f;
    static constexpr float kLoudPeakRatio = 0.50f;
    static constexpr double kHoldSilentSec = 0.0;  // D-07: immediate exit from SILENT
    static constexpr double kHoldSoftToLoudSec = 0.20;
    static constexpr double kHoldSoftToSilentSec = 1.50;
    static constexpr double kHoldLoudToSoftSec = 1.50;
    static constexpr double kHoldLoudToSilentSec = 2.00;
};
