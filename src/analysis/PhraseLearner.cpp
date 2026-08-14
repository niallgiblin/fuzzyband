#include "PhraseLearner.h"
#include <cmath>
#include <algorithm>

PhraseLearner::PhraseLearner()
{
    reset();
}

void PhraseLearner::prepare(double sampleRate) noexcept
{
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    reset();
}

void PhraseLearner::reset() noexcept
{
    state_ = State::Learning;
    attackWrite_ = 0;
    attackCount_ = 0;
    patternLen_ = 0;
    patternLenBeats_ = 4.0;
    locked_ = false;
    playbackPhase_ = 0.0;
    playbackStep_ = 0;
    lastTriggerSample_ = 0;
    prevRms_ = 0.0f;
    rmsSmooth_ = 0.0f;
    lastAttackSample_ = 0;
    silentBlockCount_ = 0;
}

bool PhraseLearner::detectAttack(float rms) noexcept
{
    // Smooth RMS for comparison (faster response)
    rmsSmooth_ = 0.85f * rmsSmooth_ + 0.15f * rms;

    const float delta = rms - prevRms_;
    prevRms_ = rms;

    // Very low threshold - we want to catch most note attacks
    if (rms < 0.002f)
        return false;

    // Attack: either a significant rise OR we're loud and rising
    const float ratio = (rmsSmooth_ > 0.0005f) ? (rms / rmsSmooth_) : 1.0f;
    
    // More permissive: lower ratio threshold, OR just a positive delta when loud
    const bool ratioTrigger = ratio > 1.2f;
    const bool deltaTrigger = delta > 0.001f && rms > 0.01f;
    
    return ratioTrigger || deltaTrigger;
}

bool PhraseLearner::patternsMatch(int len, double bpm) const noexcept
{
    if (len < 2 || attackCount_ < len * 2)  // Reduced minimum from 3 to 2
        return false;

    // Compare IOIs (inter-onset intervals) between two consecutive phrases
    // Phrase A: attacks [attackCount_ - 2*len] to [attackCount_ - len - 1]
    // Phrase B: attacks [attackCount_ - len] to [attackCount_ - 1]

    // 1/4 beat tolerance at the actual tempo (not hardcoded 120 BPM).
    const double bpmSafe = (bpm > 0.0) ? bpm : 120.0;
    const double toleranceSamples = 0.25 * (60.0 / bpmSafe) * sampleRate_;

    for (int i = 0; i < len - 1; ++i)
    {
        int idxA1 = (attackWrite_ - 2 * len + i + kMaxAttacks) % kMaxAttacks;
        int idxA2 = (idxA1 + 1) % kMaxAttacks;
        int64_t ioiA = attacks_[idxA2].sample - attacks_[idxA1].sample;

        int idxB1 = (attackWrite_ - len + i + kMaxAttacks) % kMaxAttacks;
        int idxB2 = (idxB1 + 1) % kMaxAttacks;
        int64_t ioiB = attacks_[idxB2].sample - attacks_[idxB1].sample;

        if (std::abs(static_cast<double>(ioiA - ioiB)) > toleranceSamples)
            return false;
    }

    return true;
}

void PhraseLearner::lockPattern(double bpm) noexcept
{
    if (patternLen_ < 2)
        return;

    // Use the most recent phrase (last patternLen_ attacks) as the pattern
    int startIdx = (attackWrite_ - patternLen_ + kMaxAttacks) % kMaxAttacks;

    // First note is at beat 0
    pattern_[0].beatOffset = 0.0;
    pattern_[0].midiNote = mapToBassRange(attacks_[static_cast<size_t>(startIdx)].pitch);

    // Convert IOIs in samples to beats using the actual tempo at lock time.
    // Playback later re-scales by the transport BPM via playbackPhase_.
    const double bpmSafe = (bpm > 0.0) ? bpm : 120.0;
    const double beatsPerSample = bpmSafe / 60.0 / sampleRate_;

    double totalBeats = 0.0;
    for (int i = 1; i < patternLen_; ++i)
    {
        int prevIdx = (startIdx + i - 1) % kMaxAttacks;
        int curIdx = (startIdx + i) % kMaxAttacks;

        int64_t ioiSamples = attacks_[static_cast<size_t>(curIdx)].sample - 
                            attacks_[static_cast<size_t>(prevIdx)].sample;
        double ioiBeats = static_cast<double>(ioiSamples) * beatsPerSample;

        totalBeats += ioiBeats;
        pattern_[static_cast<size_t>(i)].beatOffset = totalBeats;
        pattern_[static_cast<size_t>(i)].midiNote = mapToBassRange(attacks_[static_cast<size_t>(curIdx)].pitch);
    }

    // Pattern length: use actual length plus a small gap for the repeat
    // Don't round to bars - keep the actual learned timing
    patternLenBeats_ = totalBeats + 0.5;  // Add half beat gap before repeat

    locked_ = true;
    state_ = State::Locked;
    playbackPhase_ = 0.0;
    playbackStep_ = 0;
}

int PhraseLearner::mapToBassRange(float midiNote) const noexcept
{
    // Simply drop octaves to get into bass range [28, 52] (E1 to E3)
    // This preserves the exact pitch class (same note name, lower octave)
    int bassNote = static_cast<int>(std::round(midiNote));

    // Drop octaves until in bass range
    while (bassNote > 52) bassNote -= 12;
    // But don't go too low
    while (bassNote < 28) bassNote += 12;

    return bassNote;
}

PhraseLearner::BassNote PhraseLearner::process(int64_t sampleTime, float rms, float pitchMidi,
                                                float pitchConf, float bpm, int numSamples) noexcept
{
    BassNote result;
    result.trigger = false;
    result.midiNote = 40;
    result.velocity = 0.9f;

    // Silence detection
    if (rms < 0.003f)
    {
        ++silentBlockCount_;
        if (silentBlockCount_ > kSilenceResetBlocks)
        {
            reset();
            return result;
        }
    }
    else
    {
        silentBlockCount_ = 0;
    }

    // Minimum interval between attacks
    const bool canAttack = (sampleTime - lastAttackSample_) > kMinAttackIntervalSamples;

    // Detect attacks - very low pitch confidence threshold to catch more notes
    const bool attack = canAttack && detectAttack(rms) && pitchConf > 0.05f;

    if (attack)
    {
        lastAttackSample_ = sampleTime;

        // Record attack
        attacks_[attackWrite_].sample = sampleTime;
        attacks_[attackWrite_].pitch = pitchMidi;
        attackWrite_ = (attackWrite_ + 1) % kMaxAttacks;
        if (attackCount_ < kMaxAttacks)
            ++attackCount_;
    }

    // State machine
    switch (state_)
    {
        case State::Learning:
        {
            // Try to find pattern repetition for various lengths
            // Minimum 2 notes * 2 repetitions = 4 attacks
            if (attackCount_ >= 4)
            {
                for (int len = 2; len <= std::min(16, attackCount_ / 2); ++len)
                {
                    if (patternsMatch(len, bpm))
                    {
                        patternLen_ = len;
                        // Lock immediately after first repeat detected (don't wait for 3rd)
                        lockPattern(bpm);
                        break;
                    }
                }
            }
            break;
        }

        case State::Confirming:
        {
            // This state is now unused - we lock immediately from Learning
            // Keep for potential future use
            lockPattern(bpm);
            break;
        }

        case State::Locked:
        {
            // Advance playback position
            const double beatsPerSample = bpm / 60.0 / sampleRate_;
            const double blockBeats = static_cast<double>(numSamples) * beatsPerSample;
            const double prevPhase = playbackPhase_;
            playbackPhase_ += blockBeats;

            // Wrap around pattern
            if (playbackPhase_ >= patternLenBeats_)
            {
                playbackPhase_ = std::fmod(playbackPhase_, patternLenBeats_);
                playbackStep_ = 0;  // Reset to first note
            }

            // Check if we should trigger the next note
            if (playbackStep_ < patternLen_)
            {
                const double noteOffset = pattern_[playbackStep_].beatOffset;

                // Did we cross this note's trigger point?
                bool crossed = false;
                if (prevPhase <= noteOffset && playbackPhase_ > noteOffset)
                    crossed = true;
                // Handle wrap-around case
                if (playbackPhase_ < prevPhase && noteOffset < playbackPhase_)
                    crossed = true;

                if (crossed)
                {
                    result.trigger = true;
                    result.midiNote = pattern_[playbackStep_].midiNote;
                    result.velocity = 0.9f;
                    ++playbackStep_;
                    lastTriggerSample_ = sampleTime;
                }
            }

            // Check for pattern drift - if guitarist changes significantly, unlock
            if (attack && locked_)
            {
                // Compare recent IOI with expected pattern IOI
                if (attackCount_ >= 2 && patternLen_ >= 2)
                {
                    int prevIdx = (attackWrite_ - 2 + kMaxAttacks) % kMaxAttacks;
                    int curIdx = (attackWrite_ - 1 + kMaxAttacks) % kMaxAttacks;
                    int64_t recentIoi = attacks_[curIdx].sample - attacks_[prevIdx].sample;

                    // Expected IOI from pattern at current step
                    int expectedStep = (playbackStep_ > 0) ? playbackStep_ - 1 : patternLen_ - 1;
                    int nextStep = (expectedStep + 1) % patternLen_;
                    double expectedIoiBeats = pattern_[nextStep].beatOffset - pattern_[expectedStep].beatOffset;
                    if (expectedIoiBeats < 0)
                        expectedIoiBeats += patternLenBeats_;

                    double expectedIoiSamples = expectedIoiBeats * (60.0 / bpm) * sampleRate_;
                    double drift = std::abs(static_cast<double>(recentIoi) - expectedIoiSamples);

                    // If drift > 1/4 beat, unlock and re-learn
                    double quarterBeatSamples = 0.25 * (60.0 / bpm) * sampleRate_;
                    if (drift > quarterBeatSamples * 2.0)
                    {
                        state_ = State::Learning;
                        locked_ = false;
                        patternLen_ = 0;
                    }
                }
            }
            break;
        }
    }

    return result;
}
