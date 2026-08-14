#include "analysis/PlaybackGate.h"

void PlaybackGate::reset() noexcept
{
    inPhraseBreath = false;
    silenceSamples = 0;
    prevStructureState = StructureState::SILENT;
}

GateDecision PlaybackGate::update(StructureState st, int numSamples, double sampleRate) noexcept
{
    GateDecision gd{};

    if (st == StructureState::SILENT)
    {
        // Start a fresh silence measurement when first entering silence.
        if (prevStructureState != StructureState::SILENT)
            silenceSamples = 0;

        silenceSamples += numSamples;
        const int phraseBreathSamples = static_cast<int>(kPhraseBreathHoldSec * sampleRate);

        if (silenceSamples > phraseBreathSamples)
        {
            // Long silence: request a full reset of playback state.
            inPhraseBreath = false;
            gd.resetTrackers = true;
        }
        else
        {
            // Short silence: a phrase breath — keep state, arm crash on re-entry.
            inPhraseBreath = true;
        }
    }
    else
    {
        // Transitioning out of silence after a phrase breath → arm crash cymbal.
        if (prevStructureState == StructureState::SILENT && inPhraseBreath)
            gd.armCrash = true;

        inPhraseBreath = false;
        silenceSamples = 0;
    }

    prevStructureState = st;
    return gd;
}
