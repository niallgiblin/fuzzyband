#pragma once

/**
 * @file
 * @brief Guitar "section" state machine (SILENT / AMBIENT / SOFT / LOUD / BREAKDOWN).
 *
 * Five-state model for sludge metal: silent gaps, ambient drones/feedback,
 * soft clean arpeggios, loud palm-muted riffing, and crushing breakdowns.
 * Hold times scaled ×4-8 from original 3-state model for sludge's slow-burn pacing.
 */

/**
 * @brief High-level musical section inferred from energy features.
 */
enum class StructureState
{
    SILENT,
    AMBIENT,
    SOFT,
    LOUD,
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

    // Energy thresholds for 5-state classification
    static constexpr float kSilentRms = 0.012f;
    static constexpr float kSilentPeakRatio = 0.02f;
    static constexpr float kAmbientCeil = 0.025f;   // RMS ceiling: AMBIENT → SOFT boundary
    static constexpr float kLoudRmsFloor = 0.055f;
    static constexpr float kLoudRms = 0.075f;       // RMS floor: SOFT → LOUD boundary
    static constexpr float kLoudPeakRatio = 0.50f;
    static constexpr float kBreakdownRms = 0.12f;   // RMS floor: LOUD → BREAKDOWN boundary

    // Hold times (seconds) — scaled ×4-8 for sludge metal pacing
    static constexpr double kHoldSilentSec = 0.0;           // Immediate exit from SILENT
    static constexpr double kHoldAmbientToSoftSec = 6.0;
    static constexpr double kHoldAmbientToSilentSec = 8.0;
    static constexpr double kHoldSoftToLoudSec = 1.6;       // was 0.20 (×8)
    static constexpr double kHoldSoftToAmbientSec = 6.0;
    static constexpr double kHoldSoftToSilentSec = 6.0;     // was 1.50 (×4)
    static constexpr double kHoldLoudToBreakdownSec = 0.4;  // quick crush entry
    static constexpr double kHoldLoudToSoftSec = 6.0;       // was 1.50 (×4)
    static constexpr double kHoldLoudToSilentSec = 8.0;     // was 2.00 (×4)
    static constexpr double kHoldBreakdownToLoudSec = 8.0;
    static constexpr double kHoldBreakdownToSoftSec = 8.0;
    static constexpr double kHoldBreakdownToSilentSec = 8.0;

    // Sub-bass ratio thresholds for SOFT/LOUD discrimination
    static constexpr float kSubBassLoudFloor = 0.35f;   // Above this: palm-mute chug → LOUD bias
    static constexpr float kSubBassSoftCeil = 0.20f;    // Below this: clean arpeggio → SOFT bias
};
