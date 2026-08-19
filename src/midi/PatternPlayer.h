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
#include "GrooveTemplate.h"
#include <atomic>

/**
 * @brief Emits humanised drum (ch 10) and bass (ch 2) MIDI from the active pattern.
 *
 * Musicality pivot (Workstream A): drums render through a groove template
 * (velocity hierarchy + structured microtiming + bounded gaussian, A2), section
 * velocity contrast (A3.1) and ghost notes (A3.2); bass plays the authored
 * `pattern.bassEvents` transposed to the guitarist's root, with a harmonic
 * fallback engine (A1).
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

    // ── Musicality pivot (Workstream A / B1) ──────────────────────────────────

    /** @brief Swing/shuffle ratio in [0,1]; delays off-8th events (A2.3). Audio thread. */
    void setSwing(float newSwing) noexcept;

    /** @brief Current section; drives velocity contrast (A3.1) and bass harmony (A1.2). Audio thread. */
    void setSection(Groove::SongSectionId s) noexcept { sectionId = s; }

    /** @brief Select the genre preset: groove template, section velocities, ghost density (B1). Audio thread. */
    void setGenrePreset(int presetId) noexcept;

    /** @brief Seed the humanisation RNG deterministically (tests; A2.4 seed stability). */
    void setRandomSeed(juce::int64 seed) noexcept { rng.setSeed(seed); }

    float getSwing() const noexcept { return swing; }

    static constexpr int kBassChannel = 2;

private:
    /** Emit drum events from a pattern for an absolute beat range. */
    void emitDrumEventsForRange(juce::MidiBuffer& midi,
                                int numSamples,
                                double beatStart,
                                double beatEnd,
                                const MidiPattern& pattern,
                                int sampleOffsetBase);

    /** @brief Emit off-16th ghost snare notes (A3.2) at cells not occupied by authored snares. */
    void emitGhostNotes(juce::MidiBuffer& midi,
                        int numSamples,
                        double beatStart,
                        double beatEnd,
                        const bool occupied[16],
                        int sampleOffsetBase);

    /** @brief Route bass for a range: authored pattern bass, else harmonic fallback. */
    void emitBassRange(juce::MidiBuffer& midi,
                       int numSamples,
                       double beatStart,
                       double beatEnd,
                       const MidiPattern& pattern,
                       int sampleOffsetBase);

    /** @brief Emit authored @c pattern.bassEvents transposed to the live root (A1.1). */
    void emitPatternBass(juce::MidiBuffer& midi,
                         int numSamples,
                         double beatStart,
                         double beatEnd,
                         const MidiPattern& pattern,
                         int sampleOffsetBase);

    /** @brief Harmonic bass engine: root/fourth/fifth/octave per section (A1.2). */
    void emitHarmonicBass(juce::MidiBuffer& midi,
                          int numSamples,
                          double beatStart,
                          double beatEnd,
                          int sampleOffsetBase);

    /**
     * @brief Emit one bass note. The bass is monophonic (note duration is always
     * shorter than the note spacing), so this closes any previously scheduled
     * note before the new note-on, and defers the new note's note-off with the
     * correct note number — no stuck notes when successive notes differ in pitch.
     */
    void emitBassNote(juce::MidiBuffer& midi,
                      int numSamples,
                      int64_t blockStart,
                      int outNote,
                      int vel,
                      int off,
                      int durSamps,
                      int sampleOffsetBase);

    /** @brief Semitone interval for the harmonic bass on a beat within a bar. */
    int harmonyDegree(int beatInBar, int bar) const noexcept;

    void emitTransitionFill(juce::MidiBuffer& midi,
                            int numSamples,
                            TransitionFillKind kind,
                            int sampleOffsetBase) noexcept;

    /** Emit a crash cymbal hit with a scheduled note-off (no hanging cymbal). */
    void emitCrashHit(juce::MidiBuffer& midi,
                      int numSamples,
                      int64_t hostSamplePosition,
                      int sampleOffset) noexcept;

    /** @brief Bounded gaussian around a mean; clamps to ±2.5 sigma (A2.4). */
    static float boundedGaussian(juce::Random& r, float mean, float sigma) noexcept;

    /** @brief Whether ghost notes may be injected for the active section. */
    bool sectionAllowsGhosts() const noexcept;

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
    int64_t lastHostSample = -1;     // raw host position from the previous block (frozen-transport detection)

    bool structureSilent = false;
    bool wasSilent = false;

    int bassSemitoneOffset = 0;

    // Simple beat-aligned bass
    int bassRootMidi = 40;      // E2 (drop-C metal root)
    int bassNotesPerBar = 2;    // default: half notes (beats 1 and 3)
    int bassLastMidiNote = 40;
    int bassNoteOffMidi = 40;       // note whose note-off is pending (monophonic bass)
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

    // ── Musicality pivot state (Workstream A / B1) ────────────────────────────
    Groove::Template grooveTemplate;            // velocity hierarchy + microtiming
    Groove::GenrePreset preset;                 // active genre preset (copy)
    Groove::SongSectionId sectionId = Groove::SongSectionId::Verse;
    float swing = 0.0f;                         // 0..1 (A2.3)
    float sectionVelMul = 1.0f;                 // preset section multiplier (A3.1)
    float ghostDensity = 0.0f;                  // 0..1 (A3.2)

    static constexpr int kDrumChannel = 10;
    static constexpr int kCrashNote = 49;
    static constexpr int kPatternBassRoot = 36;  // library-authored bass root (C2)
    static constexpr double kBassGate = 0.85;    // note gate (85% of written duration)
};
