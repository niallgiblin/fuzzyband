#pragma once

/**
 * @file
 * @brief Learns guitar riff patterns (rhythm + melody) and mirrors them on bass.
 *
 * Phase 1 (Learning): Records attack timestamps/pitches and detects when the riff
 *   repeats (IOI pattern matches), locking immediately on a match.
 * Phase 2 (Locked): Plays bass following the learned rhythm and pitch pattern.
 *
 * Reset: On silence or when pattern changes significantly.
 */

#include <array>
#include <climits>
#include <cstdint>

class PhraseLearner
{
public:
    struct BassNote
    {
        bool trigger = false;     // Should emit a note this block?
        int midiNote = 36;        // Which note (C2–B2 bass register)
        float velocity = 0.58f;   // MIDI ~74; 0.9 was a wall of sound
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
     * @param stablePitchClassOffset  Pitch-class offset from C in [0,11], or
     *        INT_MIN if the tracker has no held class. Preferred over raw YIN
     *        MIDI (which octave-flips on distorted guitar).
     * @return BassNote with trigger=true if bass should play this block
     */
    BassNote process(int64_t sampleTime, float rms, float pitchMidi, float pitchConf,
                     float bpm, int numSamples,
                     int stablePitchClassOffset = INT_MIN) noexcept;

    /**
     * @brief User-armed capture: reset and record every attack until
     *        @ref commitUserCapture. No auto-lock, no live bass mirror.
     */
    void beginUserCapture() noexcept;

    /** @brief True between @ref beginUserCapture / @ref beginGridCapture and commit/cancel. */
    bool isUserCapturing() const noexcept { return userCapturing_; }

    /**
     * @brief Lock the entire recorded attack sequence as the bass riff.
     *        Returns false if fewer than 2 attacks were captured.
     */
    bool commitUserCapture(double bpm, int64_t sampleTime) noexcept;

    /** @brief Abort a capture without locking. */
    void cancelUserCapture() noexcept;

    static constexpr int kGridBars = 4;
    static constexpr int kGridSlotsPerBar = 16;
    static constexpr int kGridSlots = kGridBars * kGridSlotsPerBar;  // 64 sixteenths

    /** @brief 4-bar piano-roll snapshot. Unoccupied slots are rests. */
    struct LearnedRiff
    {
        bool valid = false;
        double lenBeats = 16.0;
        std::array<bool, kGridSlots> occupied{};
        std::array<int, kGridSlots> midi{};
        std::array<uint8_t, kGridSlots> gate16{};  // 16th counts; 0 = sustain continuation
    };

    /**
     * @brief Arm a metronome-locked piano-roll capture: 4 bars of 16th slots.
     *        Occupancy is stamped with @ref stampGridRange; attacks are ignored.
     */
    void beginGridCapture() noexcept;

    /**
     * @brief Arm a 4-bar 16th-grid listen in follow mode (drums keep playing).
     *        Same occupancy capture as Record riff, aligned to the drum clock.
     */
    void beginLiveGridListen() noexcept;

    /** @brief Abort a passive grid listen without touching capture state. */
    void cancelLiveGridListen() noexcept;

    bool isGridCapturing() const noexcept { return gridCapturing_; }

    /** @brief True during follow-mode grid LISTEN (passive). Unlike active
     *         capture, listening does NOT disable auto-lock or the fallback
     *         bass — it only stamps the grid as a side observation. */
    bool isGridListening() const noexcept { return gridListening_; }

    /** @brief True while any grid take (record or listen) is armed. */
    bool isGridTakeActive() const noexcept { return gridCapturing_ || gridListening_; }

    /**
     * @brief Mark 16th slots that overlap [@p beat0, @p beat1) when @p peak is
     *        above the guitar-playing floor. Each overlapping slot is tested
     *        independently — a ringing palm-mute must not paint neighbors.
     *        Beats are relative to riff start (0 = bar 1 beat 1, 16 = end of bar 4).
     *        @p onset is a re-attack (D6); false marks a sustain continuation.
     */
    void stampGridRange(double beat0, double beat1, float peak, int bassMidi,
                        bool onset = true) noexcept;

    /**
     * @brief Lock occupied 16th slots as a 4-bar (16-beat) bass loop.
     *        Rest slots stay in the piano roll (lenBeats = 16). Returns false
     *        if fewer than 2 slots were occupied.
     */
    bool commitGridCapture() noexcept;

    /** @brief Copy the 64-slot occupancy+midi snapshot (rests included). */
    void exportPattern(LearnedRiff& dest) const noexcept;

    /** @brief Load a 64-slot snapshot and rewind playback to phase 0. */
    bool loadPattern(const LearnedRiff& src) noexcept;

    /**
     * @brief Keep a copy of riff A for IOI+pitch matching after a listen reset.
     *        Used to cut a post-lock transition short when the same riff returns
     *        (T6.2). Does not lock or alter attack history.
     */
    void setMatchReference(const LearnedRiff& src) noexcept;

    /** @brief Occupied 16th slots in the current grid take (0 if not capturing). */
    int getGridOccupiedCount() const noexcept { return gridOccupied_; }

    /** @brief Whether 16th slot @p i is occupied (0..63). */
    bool getGridSlotOccupied(int i) const noexcept
    {
        return i >= 0 && i < kGridSlots && gridSlots_[static_cast<size_t>(i)].occupied;
    }

    /** @brief Bass MIDI for 16th slot @p i, or -1 when empty/invalid. */
    int getGridSlotMidi(int i) const noexcept
    {
        if (i < 0 || i >= kGridSlots || !gridSlots_[static_cast<size_t>(i)].occupied)
            return -1;
        return gridSlots_[static_cast<size_t>(i)].midiNote;
    }

    /** @brief True when slot @p i was stamped as a re-attack (T5.2). */
    bool getGridSlotOnset(int i) const noexcept
    {
        return i >= 0 && i < kGridSlots && gridSlots_[static_cast<size_t>(i)].occupied
            && gridSlots_[static_cast<size_t>(i)].onset;
    }

    /** @brief Coalesced gate in 16ths for slot @p i, or 0 when empty/sustain. */
    uint8_t getGridSlotGate(int i) const noexcept
    {
        if (i < 0 || i >= kGridSlots || !gridSlots_[static_cast<size_t>(i)].occupied)
            return 0;
        return gridSlots_[static_cast<size_t>(i)].gate16;
    }

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

    /** @brief Play = false (never auto-lock). RiffBListen = true. */
    void setAutoLockEnabled(bool enabled) noexcept { autoLockEnabled_ = enabled; }
    bool isAutoLockEnabled() const noexcept { return autoLockEnabled_; }

    /**
     * @brief Rewind the learned riff to bar 1 beat 1 (phase 0) so a re-locked
     *        groove re-enters on the downbeat instead of mid-riff.
     *        No-op when not locked. Audio-thread safe.
     */
    void rewindRiffToDownbeat() noexcept
    {
        if (locked_)
        {
            playbackPhase_ = 0.0;
            playbackStep_ = 0;
        }
    }

    /** @brief Whether the most recent attack landed on the learned riff's grid. */
    bool isFollowingRiff() const noexcept { return following_; }

    /** @brief True on the block where a matching riff attack was just detected. */
    bool justMatchedRiff() const noexcept { return justMatched_; }

    /**
     * @brief True on the block where the attack matched @ref setMatchReference
     *        (riff A) by IOI and pitch class. Independent of the live lock.
     */
    bool justMatchedReference() const noexcept { return justMatchedRef_; }

    /**
     * @brief Whether a note attack was recorded within the last @p windowSamples.
     *        Drives the immediate-riff-mirror bass (attack-driven, pre-lock).
     */
    bool hasRecentAttack(int64_t now, int64_t windowSamples) const noexcept
    {
        // `attackCount_ > 0` guards the pre-first-attack state: lastAttackSample_
        // defaults to 0, so without it the very first few seconds would read as
        // "recently attacked" even though nothing has been picked yet.
        return attackCount_ > 0 && (now - lastAttackSample_) < windowSamples;
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

    /** @brief Guitar attacks per beat over a recent window (R1 rhythm feature).
     *  ~0.5 = half-notes, ~1 = quarters, ~2 = 8ths, ~4 = 16ths. 0 if no window. */
    float getOnsetDensityPerBeat(int64_t now, int64_t windowSamples, double samplesPerBeat) const noexcept
    {
        if (windowSamples <= 0 || samplesPerBeat <= 0.0)
            return 0.0f;
        const int n = countRecentAttacks(now, windowSamples);
        const double beats = static_cast<double>(windowSamples) / samplesPerBeat;
        return (beats > 0.0) ? static_cast<float>(static_cast<double>(n) / beats) : 0.0f;
    }

    /** @brief Mean inter-onset interval in samples over a recent window (R1).
     *  0 if fewer than 2 attacks in the window. */
    float getMeanIoiSamples(int64_t now, int64_t windowSamples) const noexcept
    {
        if (windowSamples <= 0 || attackCount_ < 2)
            return 0.0f;
        const int64_t cutoff = now - windowSamples;
        int k = 0;
        int64_t sum = 0;
        int64_t prev = -1;
        const int n = std::min(attackCount_, kMaxAttacks);
        for (int i = 0; i < n; ++i)
        {
            const int idx = (attackWrite_ - 1 - i + kMaxAttacks) % kMaxAttacks;
            const int64_t s = attacks_[static_cast<size_t>(idx)].sample;
            if (s < cutoff)
                break;  // ring is chronological (newest first)
            if (prev >= 0)
            {
                sum += (prev - s);
                ++k;
            }
            prev = s;
        }
        return (k > 0) ? static_cast<float>(static_cast<double>(sum) / k) : 0.0f;
    }

    /** Number of attacks detected so far. */
    int getAttackCount() const noexcept { return attackCount_; }

    /** Current state as string for debugging. */
    const char* getStateName() const noexcept {
        switch (state_) {
            case State::Learning: return "Learning";
            case State::Locked: return "Locked";
        }
        return "Unknown";
    }

private:
    enum class State { Learning, Locked };

    bool detectAttack(float rms) noexcept;
    bool patternsMatch(int len, double bpm) const noexcept;
    void lockPattern(double bpm, int64_t sampleTime) noexcept;
    int mapToBassRange(float midiNote) const noexcept;
    int resolveBassNote(float pitchMidi, int stablePitchClassOffset) const noexcept;
    static float bassVelocityForRms(float rms) noexcept;
    void coalesceSlotGates() noexcept;

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
    static constexpr int kMaxPattern = 64;
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
    bool userCapturing_ = false;    // User-armed whole-riff capture (no auto-lock)
    bool gridCapturing_ = false;    // Active metronome-locked 16th-grid take (Record riff)
    bool gridListening_ = false;    // Passive follow-mode grid listen (side observation)
    struct GridSlot {
        bool occupied = false;
        int midiNote = 36;
        uint8_t gate16 = 1;
        bool onset = true;
    };
    std::array<GridSlot, kGridSlots> gridSlots_{};
    int gridOccupied_ = 0;
    bool holdActive_ = false;       // Suppresses drift-unlock (bass keeps the riff)
    bool autoLockEnabled_ = true;   // Play disables; RiffBListen enables
    bool following_ = false;        // Last attack matched the learned riff's grid
    bool justMatched_ = false;      // Edge: matched on the current block
    bool justMatchedRef_ = false;   // Edge: matched the A-riff match reference (T6.2)
    std::array<PatternNote, kMaxPattern> matchPattern_{};
    int matchLen_ = 0;
    double matchLenBeats_ = 16.0;
    int64_t mismatchStartSample_ = -1;  // first non-following attack while locked
    static constexpr int kDriftUnlockBars = 4;  // T6.3: escape a held lock after this

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
    float lastGoodPitchMidi_ = 36.0f;
    bool  lastGoodPitchValid_ = false;

    // Silence detection - longer threshold to avoid premature reset
    int silentBlockCount_ = 0;
    static constexpr int kSilenceResetBlocks = 200;  // ~4 seconds at 512 samples/block
};
