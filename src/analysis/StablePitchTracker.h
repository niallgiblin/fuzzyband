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
 */
class StablePitchTracker
{
public:
    /** @brief Reset to default (E2 = MIDI 40). Call from prepareToPlay(). */
    void reset() noexcept;

    /** @brief Advance tracker state for this block.
        @param rawMidi     PitchEstimator::getMidiNote() — continuous MIDI note number.
        @param rawConf     PitchEstimator::getConfidence() — YIN confidence [0,1].
        @param bpm         Current stable BPM (for one-beat window calculation).
        @param numSamples  Block size in samples.
        @param sampleRate  Current sample rate.
        @param isSilent    True when StructureState is SILENT (or digital silence).
        @return Semitone offset in [-6, 6] when stable, or INT_MIN if no update. */
    int update(float rawMidi, float rawConf, float bpm,
               int numSamples, double sampleRate,
               bool isSilent) noexcept;

private:
    static constexpr float kMinPitchConfidence = 0.35f;
    static constexpr int   kBassRootPc         = 4;  // E = MIDI 40 % 12

    float heldPitchRootMidi         = 40.0f;
    float heldPitchConfidence       = 0.0f;
    bool  pitchHoldValid            = false;
    int   pitchStableCounterSamples = 0;
    float lastStablePitchMidi       = 40.0f;
};
