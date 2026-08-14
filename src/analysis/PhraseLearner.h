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
    void lockPattern(double bpm) noexcept;
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

    // Attack detection state
    float prevRms_ = 0.0f;
    float rmsSmooth_ = 0.0f;
    int64_t lastAttackSample_ = 0;
    static constexpr int kMinAttackIntervalSamples = 2000;  // ~40ms min between attacks

    // Silence detection - longer threshold to avoid premature reset
    int silentBlockCount_ = 0;
    static constexpr int kSilenceResetBlocks = 200;  // ~4 seconds at 512 samples/block
};
