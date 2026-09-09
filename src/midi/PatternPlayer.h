#pragma once

/**
 * @file
 * @brief Beat-synchronised MIDI drum/bass rendering from @ref MidiPatternLibrary data.
 *
 * The drum/bass clock is anchored to the DAW transport. process() receives the
 * host sample position (getTimeInSamples()) and derives the beat grid from it,
 * so patterns stay locked to the host timeline across seeks, loops and transport
 * start/stop. A stopped playhead free-runs internally; the first moving sample
 * after that snaps onto the host grid without a seek dump (DAW Record must not
 * restart count-in). Tempo is host-authoritative (set via setBpm()/snapBpm()).
 */

#include <juce_audio_basics/juce_audio_basics.h>
#include "MidiPatternLibrary.h"
#include "GrooveTemplate.h"
#include "GrooveGrid.h"
#include <array>
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
        TransitionFillKind fillKind = TransitionFillKind::None;
    };

    /**
     * @brief Per-bar score-level ornaments (Tier-0): subtle, deterministic
     *        mutations of *which* authored hits sound in a bar. All fields are
     *        empty/no-op by default; computed by @ref computeOrnamentation.
     */
    struct BarOrnamentation
    {
        bool openHat = false;       // replace ONE closed-hat cell with an open hat
        int  openHatCell = -1;      // grid16 cell to open
        bool rideSwitch = false;    // closed hats -> ride (bell on the downbeat)
        bool extraGhost = false;    // one extra off-16th ghost snare
        int  extraGhostCell = -1;
        bool dropKick = false;      // omit one non-downbeat/beat-3 kick
        int  dropKickCell = -1;
        bool microFill = false;     // tom pickup at the end of a 4-bar phrase
    };

    /**
     * @brief Deterministic per-bar ornamentation (Tier-0). A pure function of the
     *  bar number + pattern, so a bar straddling two blocks, a DAW seek, or a loop
     *  back always produces the same ornaments. Reads only the pattern's const
     *  events — no allocation, audio-thread safe.
     */
    BarOrnamentation computeOrnamentation(int64_t barNumber, int patternIndex) const noexcept;

    void setPatternLibrary(const MidiPatternLibrary* lib) { library = lib; }

    void prepare(double sampleRate, int blockSize);

    /** @brief Resets beat clock state; no heap — safe from the audio thread. */
    void reset();

    void setBpm(float bpm);
    void setPatternIndex(int index);
    /** @brief Pattern currently sounding (after bar-boundary apply). Tests/UI. */
    int getActivePatternIndex() const noexcept { return activePatternIndex; }
    void setStructureSilent(bool silent);

    /**
     * @brief Replace pattern drums/bass with a metronome: kick on beat 1,
     *        side-stick on 2/3/4. Used while recording a riff.
     */
    void setClickTrack(bool on) noexcept { clickTrack_ = on; }
    bool isClickTrack() const noexcept { return clickTrack_; }

    /**
     * @brief The host-clock sample this block will use (frozen-transport aware).
     *        Matches the clock @ref process will apply; call before process().
     *        @p hostRolling is true when the host reports playing or recording.
     */
    int64_t previewResolvedHostSample(int64_t hostSamplePosition, int numSamples,
                                      bool hostRolling = false) const noexcept;

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

    /** @brief Arm a short bass pickup on the next bar boundary (section hand-off).
        The player emits a brief approach-to-tonic note as a block crosses the last
        beat of the bar, then clears the arm. Audio thread safe. */
    void armBassLeadIn() noexcept { bassLeadInArmed = true; }

    /**
     * @brief Fold a note into the current section's harmony around the live root
     *        (A1.2): snap its pitch class to the nearest chord tone and apply the
     *        bass transpose. Returns a note inside the bass register. Audio thread.
     */
    int snapBassToSectionHarmony(int rawNote) const noexcept;

    /**
     * @brief Beat-grid / pattern bass (Play + RiffBListen fallback). Off during
     *        RiffA / RiffBLocked so only the learned-riff snapshot sounds.
     */
    void setBeatGridBassEnabled(bool enabled) noexcept { beatGridBassEnabled_ = enabled; }

    /** @brief Trigger a single bass note from PhraseLearner. Call from audio thread. */
    void triggerLearnedBassNote(int midiNote, float velocity, int sampleOffset, int durationSamples) noexcept;

    /** @brief Queue a fixed-size drum/bass commit for the next bar boundary. Audio thread safe. */
    void queueGrooveCommit(const GrooveCommit& commit) noexcept;

    /** @brief Cancel a deferred groove commit before its bar-boundary activation. Audio thread safe. */
    void clearPendingGrooveCommit() noexcept;

    /**
     * @brief Fill @p midi for this audio block, anchoring the beat grid to @p hostSamplePosition.
     *        @p hostRolling is true when the host reports playing or recording.
     */
    void process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition,
                 bool hostRolling = false);

    /** @brief Arm a crash cymbal (MIDI 49) hit at the next block start. Audio thread safe. */
    void armTransitionCrash() noexcept { armCrashPending = true; }

    /**
     * @brief Overlay library fill 17/18/19 on the current bar in beat time.
     *        17: beat ≥ 3.0; 18: beat ≥ 2.0; 19: entire bar. Independent of block size.
     */
    void armBarFill(int fillPatternIndex) noexcept;

    // ── Musicality pivot (Workstream A / B1) ──────────────────────────────────

    /** @brief Swing/shuffle ratio in [0,1]; delays off-8th events (A2.3). Audio thread. */
    void setSwing(float newSwing) noexcept;

    /** @brief Current section; drives velocity contrast (A3.1) and bass harmony (A1.2). Audio thread. */
    void setSection(Groove::SongSectionId s) noexcept { sectionId = s; }

    /**
     * @brief Guitarist-energy multiplier for the accompaniment (drums + bass).
     *        Lets the kit swell with the guitarist's picking and relax when they
     *        ease off. Clamped to [0.75, 1.35]. Audio thread.
     */
    void setGuitarEnergy(float e) noexcept { guitarEnergy = juce::jlimit(0.75f, 1.35f, e); }
    float getGuitarEnergy() const noexcept { return guitarEnergy; }

    /** @brief Select the genre preset: groove template, section velocities, ghost density (B1). Audio thread. */
    void setGenrePreset(int presetId) noexcept;

    /**
     * @brief Tier-1: hand a rendered groove grid (from GrooveRenderer) to the
     *  player. When a valid grid matches the active pattern, emitDrumEventsForRange
     *  uses its per-step velocity/offset instead of the fixed Groove::Template.
     *  A non-matching/invalid grid is ignored (template fallback). Audio thread.
     */
    void setGrooveGrid(const GrooveGrid& grid) noexcept { grooveGrid = grid; }

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
                                const BarOrnamentation& orn,
                                int sampleOffsetBase);

    /** @brief Emit off-16th ghost snare notes (A3.2) at cells not occupied by authored snares. */
    void emitGhostNotes(juce::MidiBuffer& midi,
                        int numSamples,
                        double beatStart,
                        double beatEnd,
                        const bool occupied[16],
                        const BarOrnamentation& orn,
                        int sampleOffsetBase);

    /** @brief Tier-0 micro-fill: a two-note tom pickup into the next downbeat. */
    void emitMicroFill(juce::MidiBuffer& midi,
                       int numSamples,
                       double beatStart,
                       double beatEnd,
                       int sampleOffsetBase) noexcept;

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

    /** @brief Note-gate multiplier for the current section (legato vs staccato). */
    float sectionBassGate() const noexcept;

    void emitBarFill(juce::MidiBuffer& midi,
                     int numSamples,
                     double beatStart,
                     double beatEnd,
                     int fillPatternIndex) noexcept;

    /** Emit a crash cymbal hit with a scheduled note-off (no hanging cymbal). */
    void emitCrashHit(juce::MidiBuffer& midi,
                      int numSamples,
                      int64_t hostSamplePosition,
                      int sampleOffset) noexcept;

    /** Metronome: kick on 1, side-stick on 2/3/4. */
    void emitClickTrack(juce::MidiBuffer& midi,
                        int numSamples,
                        double beatStart,
                        double beatEnd,
                        int64_t hostSamplePosition) noexcept;

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
    bool lastTransportFrozen = true; // previous block used the free-run clock

    bool structureSilent = false;
    bool wasSilent = false;
    bool clickTrack_ = false;
    bool wasClickTrack_ = false;
    int64_t clickNoteOffSample = -1;
    int clickNoteOffNote = 37;

    int bassSemitoneOffset = 0;

    // Simple beat-aligned bass
    int bassRootMidi = 40;      // E2 (drop-C metal root)
    int bassNotesPerBar = 2;    // default: half notes (beats 1 and 3)
    int bassLastMidiNote = 40;
    int bassNoteOffMidi = 40;       // note whose note-off is pending (monophonic bass)
    int64_t bassNoteOffSample = -1;  // scheduled note-off sample position

    // Section hand-off: a short bass pickup armed by the processor on a section's
    // last bar. The player emits it once as a block crosses the bar's last beat.
    bool bassLeadInArmed = false;

    bool beatGridBassEnabled_ = true;   // Play / BListen grid fallback; off for frozen riffs
    int pendingBarFillIndex_ = -1;      // 17/18/19 overlay; -1 = none

    // Pending learned-bass note-ons (set by triggerLearnedBassNote, consumed in process).
    // A large block can contain more than one 16th; keep a fixed queue, no heap.
    static constexpr int kMaxPendingLearned = 8;
    struct PendingLearnedNote
    {
        bool active = false;
        int midi = 40;
        float vel = 0.58f;
        int offset = 0;
        int duration = 10000;
    };
    std::array<PendingLearnedNote, kMaxPendingLearned> pendingLearned_{};

    // Transition crash state — note-off is deferred so the cymbal decays cleanly.
    bool armCrashPending = false;
    int64_t crashNoteOffSample = -1;

    // ── Musicality pivot state (Workstream A / B1) ────────────────────────────
    Groove::Template grooveTemplate;            // velocity hierarchy + microtiming
    GrooveGrid grooveGrid;                      // Tier-1 rendered groove (else template)
    Groove::GenrePreset preset;                 // active genre preset (copy)
    Groove::SongSectionId sectionId = Groove::SongSectionId::Verse;
    float swing = 0.0f;                         // 0..1 (A2.3)
    float sectionVelMul = 1.0f;                 // preset section multiplier (A3.1)
    float ghostDensity = 0.0f;                  // 0..1 (A3.2)
    float guitarEnergy = 1.0f;                  // guitarist-energy dynamic (drums + bass)

    static constexpr int kDrumChannel = 10;
    static constexpr int kCrashNote = 49;
    static constexpr int kClickKickNote = 36;
    static constexpr int kClickStickNote = 37;
    static constexpr int kPatternBassRoot = 36;  // library-authored bass root (C2)
    static constexpr double kBassGate = 0.85;    // note gate (85% of written duration)
};
