#pragma once

/**
 * @file
 * @brief Pitch-class stability tracker for bass semitone offset (ARCH-02).
 */

#include <climits>

/**
 * @brief Accumulates consecutive blocks of the same pitch class and maps to bass semitone offset.
 *
 * Value member of AccompanimentProcessor. No heap allocation; all state is POD.
 *
 * Returns INT_MIN from update() when the stability window has not yet elapsed or when
 * confidence is below threshold. Caller should call patternPlayer.setBassSemitoneOffset()
 * only when the return value is not INT_MIN.
 *
 * The returned offset is the detected pitch class (0–11) relative to C (drop-C root,
 * MIDI 36 % 12 = 0). The processor folds it onto C2 (MIDI 36), so C→36, E→40, G→43,
 * B→47: every root lands in the C2–B2 bass octave. Octave information is deliberately
 * discarded — YIN on distorted guitar often flips octaves, but the pitch class is stable.
 */
class StablePitchTracker
{
public:
    /** @brief Reset to default (C2 = MIDI 36, drop C tuning). Call from prepareToPlay(). */
    void reset() noexcept;

    /** @brief Advance tracker state for this block.
        @param rawMidi     PitchEstimator::getMidiNote() — continuous MIDI note number.
        @param rawConf     PitchEstimator::getConfidence() — YIN confidence [0,1].
        @param bpm         Current stable BPM (for stability window calculation).
        @param numSamples  Block size in samples.
        @param sampleRate  Current sample rate.
        @param isSilent    True when StructureState is SILENT (or digital silence).
        @return Pitch-class offset in [0, 11] (0 = C, drop-C root) when stable,
                or INT_MIN if no update. */
    int update(float rawMidi, float rawConf, float bpm,
               int numSamples, double sampleRate,
               bool isSilent) noexcept;

private:
    static constexpr int   kBassRootPc         = 0;  // C = MIDI 36 % 12 (drop C)

    float heldPitchRootMidi         = 36.0f;  // C2 (drop C)
    float heldPitchConfidence       = 0.0f;
    bool  pitchHoldValid            = false;
    int   pitchStableCounterSamples = 0;
    float lastStablePitchMidi       = 36.0f;  // C2 — matches kBassRootPc=0
};
