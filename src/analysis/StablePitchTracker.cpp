#include "StablePitchTracker.h"

#include <climits>
#include <cmath>

void StablePitchTracker::reset() noexcept
{
    heldPitchRootMidi         = 36.0f;  // C2 (drop C)
    heldPitchConfidence       = 0.0f;
    pitchHoldValid            = false;
    pitchStableCounterSamples = 0;
    lastStablePitchMidi       = 36.0f;  // C2 (drop C)
}

int StablePitchTracker::update(float rawMidi, float rawConf, float bpm,
                                int numSamples, double sampleRate,
                                bool isSilent) noexcept
{
    // ── 1. Silence branch ────────────────────────────────────────────────────
    if (isSilent)
    {
        heldPitchRootMidi         = 36.0f;  // C2 (drop C)
        heldPitchConfidence       = 0.0f;
        pitchHoldValid            = false;
        pitchStableCounterSamples = 0;
        return INT_MIN;
    }

    // ── 2. Low-confidence branch ─────────────────────────────────────────────
    // Lower threshold (0.20) for faster response - we'd rather play a slightly
    // wrong note in time than the right note late
    constexpr float kFastConfThreshold = 0.20f;
    if (rawConf < kFastConfThreshold)
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

    // Use 1/8th beat (~60ms at 120bpm) for faster response
    // Pitch detection is decoupled from timing - we just need enough stability
    // to avoid spurious pitch jumps, not to gate when notes play
    const int stabilityWindowSamples = (bpm > 0.0f)
        ? static_cast<int>((60.0f / bpm / 8.0f) * static_cast<float>(sampleRate))
        : static_cast<int>(sampleRate / 16);

    if (pitchStableCounterSamples < stabilityWindowSamples)
        return INT_MIN;

    // ── 5. Map to a bass-root semitone offset ────────────────────────────────
    // Pitch-class distance from C (drop-C root pc=0), in [0, 11]. No ±6 wrap:
    // the processor folds this onto C2 (MIDI 36) so every root lands in the
    // C2–B2 octave — never below C2 (the v0.9.7 "too low for the bass VST"
    // issue) and never on the wrong pitch class.
    return currentPc - kBassRootPc;
}

int StablePitchTracker::getLastPitchClassOffset() const noexcept
{
    if (!pitchHoldValid)
        return INT_MIN;
    const int pc = ((static_cast<int>(std::round(lastStablePitchMidi)) % 12) + 12) % 12;
    return pc - kBassRootPc;
}
