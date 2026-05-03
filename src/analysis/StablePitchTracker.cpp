#include "StablePitchTracker.h"

#include <climits>
#include <cmath>

void StablePitchTracker::reset() noexcept
{
    heldPitchRootMidi         = 40.0f;
    heldPitchConfidence       = 0.0f;
    pitchHoldValid            = false;
    pitchStableCounterSamples = 0;
    lastStablePitchMidi       = 40.0f;
}

int StablePitchTracker::update(float rawMidi, float rawConf, float bpm,
                                int numSamples, double sampleRate,
                                bool isSilent) noexcept
{
    // ── 1. Silence branch ────────────────────────────────────────────────────
    if (isSilent)
    {
        heldPitchRootMidi         = 40.0f;
        heldPitchConfidence       = 0.0f;
        pitchHoldValid            = false;
        pitchStableCounterSamples = 0;
        return INT_MIN;
    }

    // ── 2. Low-confidence branch ─────────────────────────────────────────────
    if (rawConf < kMinPitchConfidence)
    {
        if (!pitchHoldValid)
        {
            heldPitchConfidence = 0.0f;
        }
        pitchStableCounterSamples = 0;
        return INT_MIN;
    }

    // ── 3. Valid pitch: update held values ───────────────────────────────────
    heldPitchRootMidi   = rawMidi;
    heldPitchConfidence = rawConf;
    pitchHoldValid      = true;

    // ── 4. Stability window — compare pitch class (octave-flip tolerance) ────
    const int currentPc = ((int)std::round(heldPitchRootMidi) % 12 + 12) % 12;
    const int lastPc    = ((int)std::round(lastStablePitchMidi) % 12 + 12) % 12;

    if (currentPc == lastPc)
    {
        pitchStableCounterSamples += numSamples;
    }
    else
    {
        pitchStableCounterSamples = numSamples;
        lastStablePitchMidi       = heldPitchRootMidi;
    }

    const int oneBeatSamples = (bpm > 0.0f)
        ? static_cast<int>((60.0f / bpm) * static_cast<float>(sampleRate))
        : static_cast<int>(sampleRate / 2);

    if (pitchStableCounterSamples < oneBeatSamples)
        return INT_MIN;

    // ── 5. Map to semitone offset ±6 from bass root E (pc=4) ─────────────────
    int delta = currentPc - kBassRootPc;
    if (delta > 6)  delta -= 12;
    if (delta < -6) delta += 12;
    return delta;
}
