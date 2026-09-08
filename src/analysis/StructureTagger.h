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

    /** @brief Advance state from latest RMS, centroid, HF flux, peak RMS, and a
     *         note-ringing flag for this block. When @p noteRinging is true a note
     *         was attacked recently and is still ringing out, so the block is not
     *         treated as silence even if RMS is low — the drums keep playing
     *         through a held/sustained note instead of dropping out mid-note. */
    StructureState update(float rms, float centroid, float highFreqFlux, int numSamples, float peakRms = 0.0f, bool noteRinging = false);

    /** @brief Set sub-bass ratio for next update() call. High = palm-mute chug, low = clean arpeggio. */
    void setSubBassRatio(float ratio) { subBassRatio = ratio; }

    StructureState getCurrentState() const { return currentState; }

    /** @brief Current adaptive noise floor (RMS), for diagnostics. */
    float getNoiseFloorRms() const noexcept { return noiseFloorRms; }

private:
    StructureState computeDesiredState(float rms, float centroid, float peakRms, float silentFloor, bool noteRinging) const;
    double holdRequiredForTransition(StructureState from, StructureState to) const noexcept;

    double sampleRate = 44100.0;
    double pendingTransitionSec = 0.0;
    StructureState currentState = StructureState::SILENT;
    StructureState pendingState = StructureState::SILENT;

    float subBassRatio = 0.0f;

    // Adaptive noise floor for solid SILENT detection: tracks the quietest RMS
    // seen (snaps down instantly, creeps up slowly) so the silent threshold
    // clears the DAW/input noise floor (guitar hum, hiss) instead of hovering
    // at the fixed kSilentRms and flickering SILENT/SOFT.
    float noiseFloorRms = kNoiseFloorInit;

    // Energy thresholds for 3-state classification
    static constexpr float kSilentRms = 0.012f;
    static constexpr float kSilentPeakRatio = 0.02f;
    static constexpr float kLoudRmsFloor = 0.20f;
    static constexpr float kLoudRms = 0.45f;        // RMS floor: SOFT → LOUD boundary
    static constexpr float kLoudPeakRatio = 0.40f;

    // Adaptive silence gate: silentFloor = max(kSilentRms, min(noiseFloor × margin, ceiling))
    static constexpr float kSilentMargin = 1.5f;        // threshold sits 1.5× above the noise floor
    static constexpr float kSilentFloorCeiling = 0.06f; // never declare silence above this RMS
    static constexpr float kNoiseFloorRelease = 0.001f; // upward creep per block (~15 s to adapt to a hotter noise floor)
    static constexpr float kNoiseFloorInit = 0.02f;     // start above typical hum so it snaps down to it

    // Hold times (seconds) — responsive pacing
    static constexpr double kHoldSilentSec         = 0.0;
    static constexpr double kHoldSoftToLoudSec     = 0.4;
    static constexpr double kHoldSoftToSilentSec   = 1.0;
    static constexpr double kHoldLoudToSoftSec     = 2.0;
    static constexpr double kHoldLoudToSilentSec   = 1.0;

    // Sub-bass ratio thresholds for SOFT/LOUD discrimination
    static constexpr float kSubBassLoudFloor = 0.35f;   // Above this: palm-mute chug → LOUD bias
    static constexpr float kSubBassSoftCeil = 0.20f;    // Below this: clean arpeggio → SOFT bias
};
