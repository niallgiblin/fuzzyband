#pragma once

/**
 * @file
 * @brief Beat-synchronised MIDI drum/bass rendering from @ref MidiPatternLibrary data.
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
        float bassRootMidi = 40.0f;
        TransitionFillKind fillKind = TransitionFillKind::None;
    };

    void setPatternLibrary(const MidiPatternLibrary* lib) { library = lib; }

    void prepare(double sampleRate, int blockSize);

    /** @brief Resets beat clock state; no heap — safe from the audio thread. */
    void reset();

    void setBpm(float bpm);
    void setPatternIndex(int index);
    void setStructureSilent(bool silent);
    /** @brief Resets beat clock to bar 1; call when count-in gate completes. Audio thread safe (no heap). */
    void snapToBarStart();
    /** @brief Set BPM directly with no EMA smoothing. Use only at gate-open. Audio thread safe. */
    void snapBpm(float newBpm);
    /** @brief Semitone transpose for bass (ch @c kBassChannel) only; clamped [-24,24]. Audio thread. */
    void setBassSemitoneOffset(int semitones);

    /** @brief Generative bass (Phase 13): when active, library bass events are not emitted. Audio thread.
        @p rootMidi is absolute (matches ONNX `Y_bass`); not summed with @ref setBassSemitoneOffset. */
    void setGenerativeBassActive(bool active, int rootMidi, float durationBeats);

    /** @brief Piano-roll generative bass (Phase 23): deliver 16-step decoded ONNX output. Audio thread.
        @p pitchOffset[16] are relative semitone offsets; @p velocity[16] are 0=rest.
        @p rootMidi is the conditioned absolute root from X_bass. */
    void setGenerativeBassSteps(const float pitchOffset[16], const float velocity[16], float rootMidi);

    /** @brief Queue a fixed-size drum/bass commit for the next bar boundary. Audio thread safe. */
    void queueGrooveCommit(const GrooveCommit& commit) noexcept;

    /** @brief Cancel a deferred groove commit before its bar-boundary activation. Audio thread safe. */
    void clearPendingGrooveCommit() noexcept;

    /** @brief Fill @p midi for this audio block using host sample position for timing. */
    void process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition);

    /** @brief Arm a crash cymbal (MIDI 49) for the next active block. Audio thread safe. */
    void armTransitionCrash() noexcept { fireTransitionCrash = true; }

    static constexpr int kBassChannel = 2;

private:
    void emitEventsForRange(juce::MidiBuffer& midi,
                            int numSamples,
                            double beatStart,
                            double beatEnd,
                            const MidiPattern& pattern,
                            int sampleOffsetBase);

    void emitGenerativeBassForWindow(juce::MidiBuffer& midi,
                                     int numSamples,
                                     double beatStart,
                                     double beatEnd,
                                     int sampleOffsetBase);

    /** Phase 23: piano-roll generative bass emission. */
    void emitGenerativeBassSteps(juce::MidiBuffer& midi,
                                 int numSamples,
                                 double beatStart,
                                 double beatEnd,
                                 int sampleOffsetBase);

    void emitTransitionFill(juce::MidiBuffer& midi,
                            int numSamples,
                            TransitionFillKind kind) noexcept;

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

    double beatPosition = 0.0;
    int64_t sampleCounter = 0;

    bool structureSilent = false;
    bool wasSilent = false;

    int bassSemitoneOffset = 0;

    bool generativeBassActive = false;
    int generativeBassRootMidi = 40;
    float generativeBassDurationBeats = 1.0f;

    /** Piano-roll bass state (Phase 23). */
    bool genBassHasSteps = false;
    float genBassPitchOffset[16] = {};
    float genBassVelocity[16] = {};
    float genBassStepRootMidi = 40.0f;

    /** Absolute sample index (exclusive) for single-note generative bass note-off (non–piano-roll path). */
    int64_t genBassAbsNoteOffSample = -1;
    int genBassLastMidiNote = 40;

    /** Deferred note-offs from @ref emitGenerativeBassSteps (sorted by time); real-time safe fixed cap. */
    static constexpr int kMaxGenStepsDeferredOffs = 16;
    int genStepsDeferCount = 0;
    int64_t genStepsDeferAbsSample[kMaxGenStepsDeferredOffs] = {};
    int genStepsDeferMidiNote[kMaxGenStepsDeferredOffs] = {};

    void insertGenStepsDefer(juce::MidiBuffer& midi,
                             int numSamples,
                             int sampleOffsetBase,
                             int64_t absOff,
                             int midiNote) noexcept;

    /** Absolute sample index for library-pattern bass note-off; -1 = no held note. */
    int64_t libBassAbsNoteOffSample = -1;
    int libBassLastMidiNote = 40;

    bool fireTransitionCrash = false;

    static constexpr int kDrumChannel = 10;
};
