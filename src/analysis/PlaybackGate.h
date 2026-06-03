#pragma once

/**
 * @file
 * @brief Playback gate: controls when drum/bass MIDI fires after silence/transitions.
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
    bool gateOpen      = false; ///< Whether playback is allowed this block.
    bool snapBeatNow   = false; ///< Caller: patternPlayer.snapBpm() + snapToBarStart()
    bool armCrash      = false; ///< Caller: patternPlayer.armTransitionCrash()
    bool resetTrackers = false; ///< Caller: onsetDetector.resetTempoLock() + beatTracker.reset() + tempoStabiliser.reset()
};

/**
 * @brief Encapsulates phrase-breath / beat-snap / active-fallback gate logic.
 *
 * Value member of AccompanimentProcessor. All methods are noexcept; no heap allocation.
 */
class PlaybackGate
{
public:
    /** @brief Reset all gate state. Call from prepareToPlay(). */
    void reset() noexcept;

    /** @brief Advance gate state for this block.
        @param st                 Current StructureState from StructureTagger.
        @param beatPhase          beatTracker.getBeatPhase01().
        @param isTempoLocked      beatTracker.isLocked().
        @param isOnsetTempoLocked onsetDetector.isTempoLocked().
        @param beatConfidence     beatTracker.getConfidence().
        @param numSamples         Block size in samples.
        @param sampleRate         Current sample rate. */
    GateDecision update(StructureState st,
                        double beatPhase,
                        bool isTempoLocked,
                        bool isOnsetTempoLocked,
                        float beatConfidence,
                        int numSamples,
                        double sampleRate) noexcept;

    /** @brief True when playback is gated open. */
    bool isGateOpen() const noexcept { return gateOpen; }

private:
    static constexpr double kPhraseBreathHoldSec    = 8.0;
    static constexpr float  kPlaybackConfidenceStart = 0.35f;
    static constexpr double kActiveFallbackStartSec  = 0.35;
    static constexpr double kBeatSnapTimeoutSec      = 2.0;

    bool     gateOpen               = false;
    bool     prevGateOpen           = false;
    bool     inPhraseBreath         = false;
    bool     pendingBeatSnap        = false;
    double   prevBeatPhase01        = 0.0;
    int64_t  pendingBeatSnapSamples = 0;
    int      silenceSamples         = 0;
    int      activeNonSilentSamples = 0;
    StructureState prevStructureState = StructureState::SILENT; ///< Internal prev-state tracking
};
