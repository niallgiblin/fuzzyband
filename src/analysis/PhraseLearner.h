#pragma once

/**
 * @file
 * @brief Learns guitar riff patterns (rhythm + melody) and mirrors them on bass.
 *
 * Phase 1 (Learning): Records attack timestamps and pitches while guitarist plays.
 * Phase 2 (Confirming): Detects when the riff repeats (IOI pattern matches).
 * Phase 3 (Locked): Plays bass following the learned rhythm and pitch pattern.
 *
 * Reset: On silence or when pattern changes significantly.
 */

#include <array>
#include <cstdint>

class PhraseLearner
{
public:
    struct BassNote
    {
        bool trigger = false;     // Should emit a note this block?
        int midiNote = 40;        // Which note (bass range)
        float velocity = 0.9f;
    };

    PhraseLearner();

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /**
     * @brief Process one audio block.
     * @param sampleTime    Absolute sample position
     * @param rms           Current RMS energy
     * @param pitchMidi     Detected pitch (MIDI note number)
     * @param pitchConf     Pitch confidence [0,1]
     * @param bpm           Current BPM
     * @param numSamples    Block size
     * @return BassNote with trigger=true if bass should play this block
     */
    BassNote process(int64_t sampleTime, float rms, float pitchMidi, float pitchConf,
                     float bpm, int numSamples) noexcept;

    /** True when pattern is locked and bass is actively playing. */
    bool isLocked() const noexcept { return state_ == State::Locked; }

    /** Number of notes in the learned pattern (0 if not locked). */
    int getPatternLength() const noexcept { return locked_ ? patternLen_ : 0; }

    /** Loop length in beats (bar-multiple so bass stays in phase with the drums). */
    double getPatternLenBeats() const noexcept { return locked_ ? patternLenBeats_ : 0.0; }

    /** Current loop phase in beats (debug/tests). */
    double getPlaybackPhase() const noexcept { return locked_ ? playbackPhase_ : 0.0; }

    /** @brief i-th learned note's bass MIDI note (0-based), or -1 when invalid. */
    int getPatternNote(int index) const noexcept
    {
        return (locked_ && index >= 0 && index < patternLen_)
            ? pattern_[static_cast<size_t>(index)].midiNote : -1;
    }

    /**
     * @brief While hold is active the learned riff is kept looping (no
     *        drift-unlock) — the groove lock's bass side. Attacks are still
     *        recorded so the caller can tell whether the guitarist is playing
     *        the riff (isFollowingRiff / justMatchedRiff).
     */
    void setHoldActive(bool active) noexcept { holdActive_ = active; }

    /** @brief Whether the most recent attack landed on the learned riff's grid. */
    bool isFollowingRiff() const noexcept { return following_; }

    /** @brief True on the block where a matching riff attack was just detected. */
    bool justMatchedRiff() const noexcept { return justMatched_; }

    /**
     * @brief Whether a note attack was recorded within the last @p windowSamples.
     *        Drives the immediate-riff-mirror bass (attack-driven, pre-lock).
     */
    bool hasRecentAttack(int64_t now, int64_t windowSamples) const noexcept
    {
        return (now - lastAttackSample_) < windowSamples;
    }

    /**
     * @brief Number of attacks recorded within the last @p windowSamples.
     *        Evidence of active riffing (≥2) vs a lone accent (1).
     */
    int countRecentAttacks(int64_t now, int64_t windowSamples) const noexcept
    {
        if (windowSamples <= 0)
            return 0;
        const int64_t cutoff = now - windowSamples;
        int count = 0;
        const int n = std::min(attackCount_, kMaxAttacks);
        for (int i = 0; i < n; ++i)
        {
            const int idx = (attackWrite_ - 1 - i + kMaxAttacks) % kMaxAttacks;
            if (attacks_[static_cast<size_t>(idx)].sample >= cutoff)
                ++count;
        }
        return count;
    }

    /** Number of attacks detected so far. */
    int getAttackCount() const noexcept { return attackCount_; }

    /** Current state as string for debugging. */
    const char* getStateName() const noexcept {
        switch (state_) {
            case State::Learning: return "Learning";
            case State::Confirming: return "Confirming";
            case State::Locked: return "Locked";
        }
        return "Unknown";
    }

private:
    enum class State { Learning, Confirming, Locked };

    bool detectAttack(float rms) noexcept;
    bool patternsMatch(int len, double bpm) const noexcept;
    void lockPattern(double bpm, int64_t sampleTime) noexcept;
    int mapToBassRange(float midiNote) const noexcept;

    double sampleRate_ = 48000.0;
    State state_ = State::Learning;

    // Attack history ring buffer
    static constexpr int kMaxAttacks = 64;
    struct Attack {
        int64_t sample = 0;
        float pitch = 40.0f;
    };
    std::array<Attack, kMaxAttacks> attacks_{};
    int attackWrite_ = 0;
    int attackCount_ = 0;

    // Learned pattern
    static constexpr int kMaxPattern = 32;
    struct PatternNote {
        double beatOffset = 0.0;   // Offset from pattern start in beats
        int midiNote = 40;         // Bass MIDI note
    };
    std::array<PatternNote, kMaxPattern> pattern_{};
    int patternLen_ = 0;
    double patternLenBeats_ = 4.0;  // Total pattern length in beats
    bool locked_ = false;

    // Playback state
    double playbackPhase_ = 0.0;    // Current position in pattern (beats)
    int playbackStep_ = 0;          // Next note to trigger
    int64_t lastTriggerSample_ = 0;

    // Groove-lock state (set by the processor; see setHoldActive)
    bool holdActive_ = false;       // Suppresses drift-unlock (bass keeps the riff)
    bool following_ = false;        // Last attack matched the learned riff's grid
    bool justMatched_ = false;      // Edge: matched on the current block

    // Attack detection state
    float prevRms_ = 0.0f;
    float rmsSmooth_ = 0.0f;    // fast EMA for the rise-vs-level ratio
    int fallCounter_ = 0;       // blocks since the last meaningful RMS decay
    int64_t lastAttackSample_ = 0;
    static constexpr int kMinAttackIntervalSamples = 2000;  // ~40ms min between attacks
    static constexpr int kFallWindowBlocks = 20;  // ~200 ms at 512/48k — decay recency window

    // Last confidently-estimated pitch. YIN's confidence collapses to ~0 at
    // loud/quiet transitions (the analysis ring mixes loud + quiet samples) —
    // exactly when note attacks fire. Holding the last good pitch lets those
    // attacks still be recorded with a sensible note instead of being dropped.
    float lastGoodPitchMidi_ = 40.0f;
    bool  lastGoodPitchValid_ = false;

    // Silence detection - longer threshold to avoid premature reset
    int silentBlockCount_ = 0;
    static constexpr int kSilenceResetBlocks = 200;  // ~4 seconds at 512 samples/block
};
