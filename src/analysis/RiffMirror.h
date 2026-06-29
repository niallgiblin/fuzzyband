#pragma once

/**
 * @file
 * @brief Riff-mirroring bass: listens to guitar attacks, learns the riff pattern,
 *        then mirrors it. Resets when the riff changes.
 *
 * Phase 1 (learning): plays root notes on beat 1.
 * Phase 2 (locked):   plays bass following the learned IOI pattern.
 * Phase 3 (reset):    detects pattern change, restarts learning.
 */

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

/**
 * @brief Tracks guitar attack timing, detects repeating riff patterns,
 *        and schedules bass notes that mirror the learned pattern.
 *
 * Thread-safe: audio thread only (no atomics needed).
 */
class RiffMirror
{
public:
    RiffMirror();

    /** Call every block with current sample timestamp and RMS energy.
     *  @param sampleTime  absolute host sample position
     *  @param rms         current RMS energy
     *  @param rmsSmooth   smoothed RMS (for attack detection)
     *  @param bpm         current BPM (for beat conversion)
     *  @param sampleRate  current sample rate
     */
    void process(int64_t sampleTime, float rms, float rmsSmooth,
                 float bpm, double sampleRate, int numSamples) noexcept;

    /** True when the bass should play a note this block. */
    bool shouldTriggerBass() const noexcept { return bassTriggerNow; }

    /** True when the riff pattern is locked (mirroring mode). */
    bool isLocked() const noexcept { return locked; }

    /** Reset learning state (e.g. on silence). */
    void reset() noexcept;

private:
    /** Detect a guitar note attack from RMS rise. */
    bool detectAttack(float rms, float rmsSmooth) const noexcept;

    /** Convert a sample delta to beats. */
    double samplesToBeats(int64_t samples, float bpm, double sr) const noexcept;

    /** Compare two IOI sequences for pattern match. */
    bool patternsMatch() const noexcept;

    /** Lock in the current IOI pattern and schedule bass triggers. */
    void lockPattern(float bpm, double sr) noexcept;

    // Ring buffer of recent attack sample positions
    static constexpr int kMaxAttacks = 64;
    std::array<int64_t, kMaxAttacks> attackSamples{};
    int attackCount = 0;       // total attacks recorded
    int attackWrite = 0;       // ring write position

    // Learning state
    int patternStartIdx = 0;   // attack index where current pattern started
    int patternLength = 0;     // number of attacks in the current pattern
    bool locked = false;
    int lockConfirmCount = 0;  // number of times the pattern has repeated

    // Locked pattern: bass trigger beat positions (relative to bar start)
    static constexpr int kMaxBassSteps = 64;
    std::array<float, kMaxBassSteps> bassTriggerBeats{};  // beat positions
    int bassStepCount = 0;     // number of steps in locked pattern
    int bassStepIdx = 0;       // current step index
    double bassBarPhase = 0.0; // current position within the pattern (beats)
    double bassPatternLen = 0.0; // total length of locked pattern in beats
    bool bassTriggerNow = false;

    // Drift tracking
    int64_t lastAttackSample = 0;
    float lastRms = 0.0f;

    // Silence detection
    int silenceCount = 0;

    // Fallback bar trigger — start negative so first bar triggers immediately
    int64_t lastBarSample = std::numeric_limits<int64_t>::min();
};
