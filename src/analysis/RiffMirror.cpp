#include "RiffMirror.h"
#include <algorithm>
#include <cmath>

RiffMirror::RiffMirror()
{
    reset();
}

void RiffMirror::reset() noexcept
{
    attackSamples.fill(0);
    attackCount = 0;
    attackWrite = 0;
    patternStartIdx = 0;
    patternLength = 0;
    locked = false;
    lockConfirmCount = 0;
    bassTriggerBeats.fill(0.0f);
    bassStepCount = 0;
    bassStepIdx = 0;
    bassBarPhase = 0.0;
    bassPatternLen = 0.0;
    bassTriggerNow = false;
    lastAttackSample = 0;
    lastRms = 0.0f;
    silenceCount = 0;
    lastBarSample = 0;
}

bool RiffMirror::detectAttack(float rms, float rmsSmooth) const noexcept
{
    // Attack: RMS is significantly above smoothed level, and we're not in a sustain
    if (rms < 0.004f) return false;

    const float ratio = (rmsSmooth > 0.0001f) ? (rms / rmsSmooth) : 1.0f;
    return ratio > 1.6f;
}

double RiffMirror::samplesToBeats(int64_t samples, float bpm, double sr) const noexcept
{
    if (bpm <= 0.0f || sr <= 0.0) return 0.0;
    return static_cast<double>(samples) * static_cast<double>(bpm) / (60.0 * sr);
}

bool RiffMirror::patternsMatch() const noexcept
{
    // Compare the last 'patternLength' IOIs with the previous cycle
    if (patternLength < 3) return false;
    if (attackCount < patternLength * 2) return false;

    const double tolerance = 0.25; // beat tolerance for IOI matching (16th note)

    for (int i = 0; i < patternLength; ++i)
    {
        // Current cycle IOI (newest patternLength attacks)
        int idxNew = (attackWrite - patternLength + i + kMaxAttacks) % kMaxAttacks;
        int idxNewNext = (idxNew + 1) % kMaxAttacks;
        double ioiNew = static_cast<double>(attackSamples[idxNewNext] - attackSamples[idxNew]);

        // Previous cycle IOI
        int idxOld = (idxNew - patternLength + kMaxAttacks) % kMaxAttacks;
        int idxOldNext = (idxOld + 1) % kMaxAttacks;
        double ioiOld = static_cast<double>(attackSamples[idxOldNext] - attackSamples[idxOld]);

        if (std::abs(ioiNew - ioiOld) > tolerance * (60.0 * 44100.0 / 120.0))
            return false;
    }

    return true;
}

void RiffMirror::lockPattern(float bpm, double sr) noexcept
{
    if (patternLength < 3) return;

    locked = true;
    bassStepCount = patternLength;
    bassStepIdx = 0;
    bassBarPhase = 0.0;
    bassPatternLen = 0.0;

    // Compute beat positions from the locked IOI pattern (newest cycle)
    int startIdx = (attackWrite - patternLength + kMaxAttacks) % kMaxAttacks;
    double beatPos = 0.0;
    bassTriggerBeats[0] = 0.0f;

    for (int i = 1; i < patternLength && i < kMaxBassSteps; ++i)
    {
        int prevIdx = (startIdx + i - 1) % kMaxAttacks;
        int curIdx = (startIdx + i) % kMaxAttacks;
        int64_t ioiSamples = attackSamples[curIdx] - attackSamples[prevIdx];
        beatPos += samplesToBeats(ioiSamples, bpm, sr);
        bassTriggerBeats[i] = static_cast<float>(beatPos);
    }

    bassPatternLen = beatPos;
    bassTriggerNow = false;
}

void RiffMirror::process(int64_t sampleTime, float rms, float rmsSmooth,
                          float bpm, double sampleRate, int numSamples) noexcept
{
    bassTriggerNow = false;

    if (rms < 0.002f)
    {
        // Silence: reset lock after a short hold
        if (++silenceCount > 50)  // ~250ms at 512-sample blocks @ 44.1kHz
        {
            reset();
            silenceCount = 0;
        }
        return;
    }

    const bool attack = detectAttack(rms, rmsSmooth);

    if (attack)
    {
        // Record attack
        attackSamples[attackWrite] = sampleTime;
        attackWrite = (attackWrite + 1) % kMaxAttacks;
        if (attackCount < kMaxAttacks) ++attackCount;
        lastAttackSample = sampleTime;
    }

    lastRms = rms;

    if (locked)
    {
        // ── Mirror mode: advance through locked pattern ──────────────
        double blockBeats = static_cast<double>(numSamples) * static_cast<double>(bpm) / (60.0 * sampleRate);
        bassBarPhase += blockBeats;

        // Wrap around pattern
        if (bassPatternLen > 0.01 && bassBarPhase >= bassPatternLen)
            bassBarPhase = std::fmod(bassBarPhase, bassPatternLen);

        // Check if we've passed the next trigger beat
        while (bassStepIdx < bassStepCount)
        {
            float nextBeat = bassTriggerBeats[bassStepIdx];
            if (bassBarPhase >= static_cast<double>(nextBeat) - 0.05)
            {
                bassTriggerNow = true;
                ++bassStepIdx;
                if (bassStepIdx >= bassStepCount)
                    bassStepIdx = 0; // wrap
            }
            else
            {
                break;
            }
        }

        // ── Drift check: has the player changed riff? ──────────────
        if (attack && patternLength >= 3)
        {
            // Compare recent IOI with expected IOI from locked pattern
            if (attackCount >= 2)
            {
                int prevIdx = (attackWrite - 2 + kMaxAttacks) % kMaxAttacks;
                int curIdx = (attackWrite - 1 + kMaxAttacks) % kMaxAttacks;
                int64_t ioiNew = attackSamples[curIdx] - attackSamples[prevIdx];

                // Find expected IOI at this point in the pattern
                int expectedStep = (bassStepIdx > 0 ? bassStepIdx - 1 : bassStepCount - 1);
                int nextStep = (expectedStep + 1) % bassStepCount;
                float expectedIoIBeats = (nextStep > expectedStep)
                    ? bassTriggerBeats[nextStep] - bassTriggerBeats[expectedStep]
                    : bassPatternLen - bassTriggerBeats[expectedStep] + bassTriggerBeats[nextStep];
                if (expectedIoIBeats < 0.0f) expectedIoIBeats += bassPatternLen;

                double expectedIoiSamples = expectedIoIBeats * (60.0 * sampleRate / static_cast<double>(bpm));
                double drift = std::abs(static_cast<double>(ioiNew) - expectedIoiSamples);

                // If drifted more than a 16th note, unlock
                double sixteenthInSamples = 0.25 * (60.0 * sampleRate / static_cast<double>(bpm));
                if (drift > sixteenthInSamples * 1.5)
                {
                    locked = false;
                    lockConfirmCount = 0;
                    patternStartIdx = attackWrite > 0 ? attackWrite - 1 : kMaxAttacks - 1;
                    patternLength = 1;
                    bassBarPhase = 0.0;
                    bassPatternLen = 0.0;
                }
            }
        }
    }
    else
    {
        // ── Learning mode: detect pattern repeats ──────────────────
        if (attack && attackCount >= 2)
        {
            // Count attacks since last pattern start
            patternLength = (attackWrite - patternStartIdx + kMaxAttacks) % kMaxAttacks;
            if (patternLength < 1) patternLength = 1;

            // Check for pattern repeat
            if (patternLength >= 3 && attackCount >= patternLength * 2)
            {
                if (patternsMatch())
                {
                    ++lockConfirmCount;
                    if (lockConfirmCount >= 1)  // lock after one confirmation
                    {
                        lockPattern(bpm, sampleRate);
                        bassTriggerNow = true; // trigger on first beat of locked pattern
                    }
                }
                else
                {
                    lockConfirmCount = 0;
                    // Shift pattern start forward to look for new pattern
                    patternStartIdx = (patternStartIdx + 1) % kMaxAttacks;
                }
            }
        }
    }

    // Fallback: if not locked, trigger bass on beat 1 of each bar
    if (!locked)
    {
        const int64_t samplesPerBar = static_cast<int64_t>(4.0 * (60.0 * sampleRate / static_cast<double>(bpm)));
        if (sampleTime - lastBarSample >= samplesPerBar)
        {
            bassTriggerNow = true;
            lastBarSample = sampleTime;
        }
    }
}
