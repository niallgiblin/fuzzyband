#pragma once

/**
 * @file
 * @brief Playback gate: emits reset / crash-arm signals around silence and phrase breaths.
 *
 * With a transport-anchored drum clock, the gate no longer decides when playback
 * starts or snaps beats — the host grid is authoritative. Its remaining job is to
 * detect long silences (request a full reset) and phrase-breath re-entries (arm a
 * transition crash cymbal).
 */

#include "analysis/StructureTagger.h"
#include <cstdint>

/**
 * @brief Decision record returned by PlaybackGate::update().
 *
 * Caller (AccompanimentProcessor) dispatches side-effects based on these flags.
 * No method calls inside PlaybackGate itself.
 */
struct GateDecision
{
    bool armCrash      = false; ///< Caller: patternPlayer.armTransitionCrash()
    bool resetTrackers = false; ///< Caller: full reset of playback/analysis state
};

/**
 * @brief Encapsulates phrase-breath / long-silence gate logic.
 *
 * Value member of AccompanimentProcessor. All methods are noexcept; no heap allocation.
 */
class PlaybackGate
{
public:
    /** @brief Reset all gate state. Call from prepareToPlay(). */
    void reset() noexcept;

    /** @brief Advance gate state for this block.
        @param st          Current StructureState from StructureTagger.
        @param numSamples  Block size in samples.
        @param sampleRate  Current sample rate. */
    GateDecision update(StructureState st, int numSamples, double sampleRate) noexcept;

private:
    static constexpr double kPhraseBreathHoldSec = 8.0;

    bool inPhraseBreath = false;
    int  silenceSamples = 0;
    StructureState prevStructureState = StructureState::SILENT; ///< Internal prev-state tracking
};
