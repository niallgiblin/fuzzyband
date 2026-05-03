#include "analysis/PlaybackGate.h"

void PlaybackGate::reset() noexcept
{
    gateOpen               = false;
    prevGateOpen           = false;
    inPhraseBreath         = false;
    pendingBeatSnap        = false;
    prevBeatPhase01        = 0.0;
    pendingBeatSnapSamples = 0;
    silenceSamples         = 0;
    activeNonSilentSamples = 0;
    prevStructureState     = StructureState::SILENT;
}

GateDecision PlaybackGate::update(StructureState st,
                                   double beatPhase,
                                   bool isTempoLocked,
                                   bool isOnsetTempoLocked,
                                   float beatConfidence,
                                   int numSamples,
                                   double sampleRate) noexcept
{
    GateDecision gd{};

    if (st == StructureState::SILENT)
    {
        // Reset the silence counter when first entering silence.
        if (prevStructureState != StructureState::SILENT)
            silenceSamples = 0;

        silenceSamples += numSamples;
        const int phraseBreathSamples = static_cast<int>(kPhraseBreathHoldSec * sampleRate);

        if (silenceSamples > phraseBreathSamples)
        {
            // Full reset: wipe all gate state.
            gateOpen               = false;
            prevGateOpen           = false;
            pendingBeatSnap        = false;
            prevBeatPhase01        = 0.0;
            pendingBeatSnapSamples = 0;
            activeNonSilentSamples = 0;
            inPhraseBreath         = false;
            gd.resetTrackers       = true;
        }
        else
        {
            // Phrase breath — keep gate and beat tracker alive; arm crash on re-entry.
            inPhraseBreath = true;
        }
    }
    else
    {
        // Transitioning out of silence while in phrase breath → arm crash cymbal.
        if (prevStructureState == StructureState::SILENT && inPhraseBreath)
            gd.armCrash = true;

        inPhraseBreath = false;
        silenceSamples = 0;
        activeNonSilentSamples += numSamples;

        const bool activeFallbackReady =
            activeNonSilentSamples >= static_cast<int>(kActiveFallbackStartSec * sampleRate);
        const bool allowPlayback = isTempoLocked
            || isOnsetTempoLocked
            || (beatConfidence >= kPlaybackConfidenceStart)
            || activeFallbackReady;

        // Rising edge of allowPlayback → arm beat-snap.
        if (allowPlayback && !prevGateOpen && !pendingBeatSnap && !gateOpen)
            pendingBeatSnap = true;

        if (pendingBeatSnap)
        {
            pendingBeatSnapSamples += numSamples;
            const bool timedOut    = (pendingBeatSnapSamples > static_cast<int64_t>(kBeatSnapTimeoutSec * sampleRate));
            const bool fallbackSnap = activeFallbackReady && !isTempoLocked;
            const bool beatBoundaryNow = fallbackSnap || (beatPhase < prevBeatPhase01) || timedOut;
            if (beatBoundaryNow)
            {
                gd.snapBeatNow         = true;
                gateOpen               = true;
                pendingBeatSnap        = false;
                pendingBeatSnapSamples = 0;
            }
        }
        else
        {
            gateOpen = allowPlayback;
        }

        prevGateOpen    = allowPlayback;
        prevBeatPhase01 = beatPhase;
    }

    gd.gateOpen = gateOpen;
    prevStructureState = st;
    return gd;
}
