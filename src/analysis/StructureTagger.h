#pragma once

/**
 * @file
 * @brief Guitar "section" state machine (SILENT / SOFT / LOUD).
 *
 * Three-state model: silent gaps, soft clean arpeggios, loud palm-muted riffing.
 * Simplified from 5-state for v0.8.0 unified Mel-CNN pipeline.
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

    /** @brief Set sub-bass ratio for next update() call. High = palm-mute chug, low = clean arpeggio. */
    void setSubBassRatio(float ratio) { subBassRatio = ratio; }

    StructureState getCurrentState() const { return currentState; }

private:
    StructureState computeDesiredState(float rms, float centroid, float peakRms) const;
    double holdRequiredForTransition(StructureState from, StructureState to) const noexcept;

    double sampleRate = 44100.0;
    double pendingTransitionSec = 0.0;
    StructureState currentState = StructureState::SILENT;
    StructureState pendingState = StructureState::SILENT;

    float subBassRatio = 0.0f;

    // Energy thresholds for 3-state classification
    static constexpr float kSilentRms = 0.012f;
    static constexpr float kSilentPeakRatio = 0.02f;
    static constexpr float kLoudRmsFloor = 0.055f;
    static constexpr float kLoudRms = 0.075f;       // RMS floor: SOFT → LOUD boundary
    static constexpr float kLoudPeakRatio = 0.50f;

    // Hold times (seconds) — responsive pacing
    static constexpr double kHoldSilentSec         = 0.0;
    static constexpr double kHoldSoftToLoudSec     = 0.4;
    static constexpr double kHoldSoftToSilentSec   = 2.0;
    static constexpr double kHoldLoudToSoftSec     = 2.0;
    static constexpr double kHoldLoudToSilentSec   = 3.0;

    // Sub-bass ratio thresholds for SOFT/LOUD discrimination
    static constexpr float kSubBassLoudFloor = 0.35f;   // Above this: palm-mute chug → LOUD bias
    static constexpr float kSubBassSoftCeil = 0.20f;    // Below this: clean arpeggio → SOFT bias
};
