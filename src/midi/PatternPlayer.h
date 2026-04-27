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
    void setPatternLibrary(const MidiPatternLibrary* lib) { library = lib; }

    void prepare(double sampleRate, int blockSize);

    /** @brief Resets beat clock state; no heap — safe from the audio thread. */
    void reset();

    void setBpm(float bpm);
    void setPatternIndex(int index);
    void setStructureSilent(bool silent);
    /** @brief Resets beat clock to bar 1; call when count-in gate completes. Audio thread safe (no heap). */
    void snapToBarStart();
    /** @brief Semitone transpose for bass (ch @c kBassChannel) only; clamped [-24,24]. Audio thread. */
    void setBassSemitoneOffset(int semitones);

    /** @brief Generative bass (Phase 13): when active, library bass events are not emitted. Audio thread.
        @p rootMidi is absolute (matches ONNX `Y_bass`); not summed with @ref setBassSemitoneOffset. */
    void setGenerativeBassActive(bool active, int rootMidi, float durationBeats);

    /** @brief Fill @p midi for this audio block using host sample position for timing. */
    void process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition);

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

    int humanVel(int base) const;
    int humanSamples() const;

    const MidiPatternLibrary* library = nullptr;

    double sampleRate = 44100.0;
    mutable juce::Random rng;

    float bpm = 120.0f;
    std::atomic<int> patternIndex{ 0 };
    int activePatternIndex = 0;
    int pendingPatternIndex = -1;

    double beatPosition = 0.0;
    int64_t sampleCounter = 0;

    bool structureSilent = false;
    bool wasSilent = false;

    int bassSemitoneOffset = 0;

    bool generativeBassActive = false;
    int generativeBassRootMidi = 40;
    float generativeBassDurationBeats = 1.0f;

    /** Absolute sample index (exclusive) for generative bass note-off; -1 = no held note. */
    int64_t genBassAbsNoteOffSample = -1;
    int genBassLastMidiNote = 40;

    static constexpr int kDrumChannel = 10;
};
