#include "StructureTagger.h"
#include <algorithm>

void StructureTagger::prepare(double newSampleRate)
{
    sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
    pendingTransitionSec = 0.0;
    currentState = StructureState::SILENT;
    pendingState = StructureState::SILENT;
    noiseFloorRms = kNoiseFloorInit;
}

StructureState StructureTagger::computeDesiredState(float rms, float /*centroid*/, float peakRms, float silentFloor, bool noteRinging) const
{
    // A note was attacked recently and is still ringing — never call it silence,
    // even if its RMS has decayed below the silent floor (otherwise the drums
    // drop out mid-note). Falls through to the SOFT/LOUD energy decision.
    if (rms < silentFloor && !noteRinging)
        return StructureState::SILENT;

    // LOUD: palm-muted riffing or full-chord playing
    const float loudFloor = (peakRms > 0.0f)
        ? std::min(kLoudRms, std::max(kLoudRmsFloor, peakRms * kLoudPeakRatio))
        : kLoudRms;

    // SOFT-LOUD overlap zone: use sub-bass ratio to discriminate
    // Palm-mute chugs concentrate energy in 30-120 Hz → high subBassRatio
    // Clean arpeggios spread energy across spectrum → low subBassRatio
    if (subBassRatio >= kSubBassLoudFloor)
        return StructureState::LOUD;   // chug detected even if RMS below loudFloor
    if (subBassRatio <= kSubBassSoftCeil && rms < loudFloor)
        return StructureState::SOFT;   // clean playing even if RMS ambiguous

    if (rms >= loudFloor)
        return StructureState::LOUD;

    // SOFT: clean arpeggios, sparse playing
    return StructureState::SOFT;
}

double StructureTagger::holdRequiredForTransition(StructureState from, StructureState to) const noexcept
{
    if (from == to)
        return 0.0;

    switch (from)
    {
        case StructureState::SILENT:
            return kHoldSilentSec;

        case StructureState::SOFT:
            if (to == StructureState::SILENT) return kHoldSoftToSilentSec;
            return kHoldSoftToLoudSec;

        case StructureState::LOUD:
            if (to == StructureState::SILENT) return kHoldLoudToSilentSec;
            return kHoldLoudToSoftSec;
    }

    return 0.0;
}

StructureState StructureTagger::update(float rms, float centroid, float /*highFreqFlux*/, int numSamples, float peakRms, bool noteRinging)
{
    const double blockSec = static_cast<double>(numSamples) / sampleRate;

    // ── Adaptive noise floor (solid SILENT) ─────────────────────────────────
    // Snap down instantly when a quieter moment appears (the real noise floor),
    // creep up slowly while still inside the quiet band (rms below the ceiling)
    // so loud playing never raises the floor. The silent threshold sits
    // kSilentMargin × above the floor, capped so genuinely quiet playing is
    // never misread as silence.
    if (rms < noiseFloorRms)
        noiseFloorRms = rms;
    else if (rms < kSilentFloorCeiling && !noteRinging)
        noiseFloorRms += kNoiseFloorRelease * (rms - noiseFloorRms);
    if (noiseFloorRms < 0.0f)
        noiseFloorRms = 0.0f;

    const float silentFloor = std::max(kSilentRms,
        std::min(noiseFloorRms * kSilentMargin, kSilentFloorCeiling));
    const float silentFloorWithPeak = std::max(silentFloor, peakRms * kSilentPeakRatio);

    const StructureState desired = computeDesiredState(rms, centroid, peakRms, silentFloorWithPeak, noteRinging);

    if (desired == currentState)
    {
        pendingTransitionSec = 0.0;
        pendingState = currentState;
        return currentState;
    }

    if (desired != pendingState)
    {
        pendingState = desired;
        pendingTransitionSec = 0.0;
    }

    pendingTransitionSec += blockSec;

    if (pendingTransitionSec < holdRequiredForTransition(currentState, desired))
        return currentState;

    currentState = desired;
    pendingState = desired;
    pendingTransitionSec = 0.0;
    return currentState;
}
