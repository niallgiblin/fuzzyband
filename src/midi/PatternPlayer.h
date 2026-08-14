#pragma once

/**
 * @file
 * @brief Beat-synchronised MIDI drum/bass rendering from @ref MidiPatternLibrary data.
 *
 * The drum/bass clock is anchored to the DAW transport. process() receives the
 * host sample position (getTimeInSamples()) and derives the beat grid from it,
 * so patterns stay locked to the host timeline across seeks, loops and transport
 * start/stop. Tempo is host-authoritative (set via setBpm()/snapBpm()).
 */

#include <juce_audio_basics/juce_audio_basics.h>
#include "MidiPatternLibrary.h"
#include <atomic>

/**
 * @brief Emits humanised drum (ch 10) and bass (ch 2) MIDI from the active pattern.
 *
 * Call @ref process from the audio thread; methods are real-time safe when documented.
 */
class PatternPlayer
{
public:
    enum class TransitionFillKind
    {
        None,
        Entry,
        BuildUp,
        Release,
        BreakdownOrImpact
    };

    struct GrooveCommit
    {
        int patternIndex = 0;
        bool hasBassFrame = false;
        float bassPitchOffset[16] = {};
        float bassVelocity[16] = {};
        float bassRootMidi = 40.0f;  // E2
        TransitionFillKind fillKind = TransitionFillKind::None;
    };

    void setPatternLibrary(const MidiPatternLibrary* lib) { library = lib; }

    void prepare(double sampleRate, int blockSize);

    /** @brief Resets beat clock state; no heap — safe from the audio thread. */
    void reset();

    void setBpm(float bpm);
    void setPatternIndex(int index);
    void setStructureSilent(bool silent);

    /** @brief Drops deferred pattern changes; the host grid is authoritative, so no beat reset occurs. */
    void snapToBarStart();

    /** @brief Set BPM directly with no smoothing (host-authoritative). */
    void snapBpm(float newBpm);

    /** @brief Semitone transpose for bass (ch @c kBassChannel) only; clamped [-24,24]. Audio thread. */
    void setBassSemitoneOffset(int semitones);

    /** @brief Set bass parameters for simple beat-aligned playback.
        @p rootMidi is the bass root note (e.g., 40 = E2)
        @p notesPerBar controls density: 1=whole, 2=half, 4=quarter notes */
    void setBassParams(int rootMidi, int notesPerBar) noexcept;

    /** @brief Enable/disable phrase-learned bass mode. When enabled, beat-aligned bass is suppressed. */
    void setPhraseLearnerActive(bool active) noexcept { phraseLearnerActive_ = active; }

    /** @brief Trigger a single bass note from PhraseLearner. Call from audio thread. */
    void triggerLearnedBassNote(int midiNote, float velocity, int sampleOffset, int durationSamples) noexcept;

    /** @brief Queue a fixed-size drum/bass commit for the next bar boundary. Audio thread safe. */
    void queueGrooveCommit(const GrooveCommit& commit) noexcept;

    /** @brief Cancel a deferred groove commit before its bar-boundary activation. Audio thread safe. */
    void clearPendingGrooveCommit() noexcept;

    /** @brief Fill @p midi for this audio block, anchoring the beat grid to @p hostSamplePosition. */
    void process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition);

    /** @brief Arm a crash cymbal (MIDI 49) hit at the next block start. Audio thread safe. */
    void armTransitionCrash() noexcept { armCrashPending = true; }

    static constexpr int kBassChannel = 2;

private:
    /** Emit drum events from a pattern for an absolute beat range. */
    void emitDrumEventsForRange(juce::MidiBuffer& midi,
                                int numSamples,
                                double beatStart,
                                double beatEnd,
                                const MidiPattern& pattern,
                                int sampleOffsetBase);

    /** Simple beat-aligned bass: emits root notes on beats based on bassNotesPerBar. */
    void emitBeatAlignedBass(juce::MidiBuffer& midi,
                             int numSamples,
                             double beatStart,
                             double beatEnd,
                             int sampleOffsetBase);

    void emitTransitionFill(juce::MidiBuffer& midi,
                            int numSamples,
                            TransitionFillKind kind,
                            int sampleOffsetBase) noexcept;

    /** Emit a crash cymbal hit with a scheduled note-off (no hanging cymbal). */
    void emitCrashHit(juce::MidiBuffer& midi,
                      int numSamples,
                      int64_t hostSamplePosition,
                      int sampleOffset) noexcept;

    int humanVel(int base) const;
    int humanSamples() const;

    const MidiPatternLibrary* library = nullptr;

    double sampleRate = 44100.0;
    mutable juce::Random rng;

    float bpm = 120.0f;
    std::atomic<int> patternIndex{ 0 };
    int activePatternIndex = 0;
    int pendingPatternIndex = -1;
    bool pendingGrooveCommitValid = false;
    GrooveCommit pendingGrooveCommit{};

    int64_t sampleCounter = 0;       // last host sample position
    int64_t expectedHostSample = 0;  // next expected host position (jump detection)

    bool structureSilent = false;
    bool wasSilent = false;

    int bassSemitoneOffset = 0;

    // Simple beat-aligned bass
    int bassRootMidi = 40;      // E2 (drop-C metal root)
    int bassNotesPerBar = 2;    // default: half notes (beats 1 and 3)
    int bassLastMidiNote = 40;
    int64_t bassNoteOffSample = -1;  // scheduled note-off sample position

    bool phraseLearnerActive_ = false;  // When true, beat-aligned bass is suppressed

    // Pending note from PhraseLearner (set by triggerLearnedBassNote, consumed in process)
    bool pendingLearnedNote_ = false;
    int pendingLearnedMidi_ = 40;
    float pendingLearnedVel_ = 0.9f;
    int pendingLearnedOffset_ = 0;
    int pendingLearnedDuration_ = 10000;

    // Transition crash state — note-off is deferred so the cymbal decays cleanly.
    bool armCrashPending = false;
    int64_t crashNoteOffSample = -1;

    static constexpr int kDrumChannel = 10;
    static constexpr int kCrashNote = 49;
};
