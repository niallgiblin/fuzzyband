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
#include "BassVoice.h"
#include "FillGrammar.h"
#include "FillBankData.h"
#include <array>
#include <atomic>
#include <cstdint>

/**
 * @brief Emits humanised drum (ch 10) and bass (ch 2) MIDI from the active pattern.
 *
 * Musicality pivot (Workstream A): drums render through a groove template
 * (velocity hierarchy + structured microtiming + bounded gaussian, A2), section
 * velocity contrast (A3.1) and ghost notes (A3.2). Grid bass goes through
 * emitBassRange: authored `pattern.bassEvents` transpose to the live root when
 * present (T5.1); empty `bassEvents` fall back to the harmonic engine (A1).
 *
 * Call @ref process from the audio thread; methods are real-time safe when documented.
 */
class PatternPlayer
{
public:
    struct GrooveCommit
    {
        int patternIndex = 0;
        bool alignToBeat = false;  // T6.1: next beat instead of next bar
    };

    /** @brief Which producer emitted a bass note (see @ref BassVoice::Producer). */
    using BassSource = BassVoice::Producer;

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

        // Tier-0 additive ornaments (B): each is a NEW note, not a modification
        // of an existing event. Every cell is chosen so it never collides with an
        // authored pattern voice or another ornament, except snareFlam which is a
        // deliberate grace note immediately before an authored backbeat snare.
        bool extraTom = false;       int extraTomCell = -1;    // accent on an empty off-16th
        bool kickDouble = false;     int kickDoubleCell = -1;  // 16th kick before a beat-1/3 kick
        bool snareFlam = false;      int snareFlamCell = -1;   // grace hit before a backbeat snare
        bool rideBellAccent = false; int rideBellCell = -1;    // bell on a phrase downbeat
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

    /**
     * @brief True if the last process() saw a seek / loop wrap. Clears the flag.
     *        The processor re-anchors lock/transition clocks from this edge.
     */
    bool consumeTransportJumped() noexcept
    {
        const bool j = transportJumped_;
        transportJumped_ = false;
        return j;
    }

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
     * @brief Beat-grid / pattern bass (Play + RiffBListen fallback). Off during
     *        RiffA / RiffBLocked so only the learned-riff snapshot sounds.
     */
    void setBeatGridBassEnabled(bool enabled) noexcept { beatGridBassEnabled_ = enabled; }

    /**
     * @brief Whether the guitarist is currently sounding (not SILENT).
     *
     * The harmony/grid line is a *total fallback*: it is heard only when the
     * guitarist has stopped. A sustain is not a gap — while this is true the
     * mirror owns the bass, and a held mirror note is sustained rather than
     * released, so a missed attack can never open the harmony underneath the
     * player (the recurring "bass goes into harmony" regression).
     */
    void setGuitarAudible(bool audible) noexcept { bassVoice.setGuitarAudible(audible); }

    /** @brief Current onset level, 0..1 (see BassVoice::setInputLevel). */
    void setGuitarLevel(float level) noexcept { bassVoice.setInputLevel(level); }

    /** @brief Trigger a single bass note from PhraseLearner. Call from audio thread.
     *  @param hold sustain until the guitar stops, instead of a fixed gate.
     *  @param source provenance tag (live mirror vs frozen snapshot). */
    void triggerLearnedBassNote(int midiNote, float velocity, int sampleOffset,
                                int durationSamples, bool hold = false,
                                BassSource source = BassSource::Mirror) noexcept
    {
        bassVoice.requestLearned(midiNote, velocity, sampleOffset, durationSamples, hold, source);
    }

    /**
     * @brief Diagnostic counters: which producer emitted each bass note-on.
     *
     * `learned` counts mirror/frozen-snapshot notes (triggerLearnedBassNote),
     * `grid` counts the authored/harmonic fallback line. The recurring
     * "bass doesn't mirror" bug is a *ratio* question — if `grid` dominates
     * `learned`, the fallback is the bass part and the mirror is being drowned.
     * Plain ints: written on the audio thread, read by tests/diagnostics only.
     */
    int getLearnedBassNoteCount() const noexcept { return bassVoice.getLearnedCount(); }
    int getGridBassNoteCount() const noexcept { return bassVoice.getGridCount(); }
    void resetBassSourceCounters() noexcept { bassVoice.resetCounters(); }

    // ── Bass-voice provenance (BassVoice) ────────────────────────────────────
    /** @brief Producer of the most recent bass note-on. */
    BassSource getLastBassProducer() const noexcept { return bassVoice.getLastProducer(); }
    /** @brief Most-recent-first bass note-ons. Returns how many were written. */
    int getRecentBassNoteOns(BassVoice::NoteOn* out, int maxCount) const noexcept
    {
        return bassVoice.getRecentNoteOns(out, maxCount);
    }
    /** @brief Note-ons emitted by @p source (provenance). */
    int getBassProducerCount(BassSource source) const noexcept
    {
        return bassVoice.getProducerCount(source);
    }

    /** @brief Queue a fixed-size drum/bass commit for the next bar (or beat
     *         when @c commit.alignToBeat). Audio thread safe. */
    void queueGrooveCommit(const GrooveCommit& commit) noexcept;

    /** @brief Cancel a deferred groove commit before its bar-boundary activation. Audio thread safe. */
    void clearPendingGrooveCommit() noexcept;

    /**
     * @brief Fill @p midi for this audio block, anchoring the beat grid to @p hostSamplePosition.
     *        @p hostRolling is true when the host reports playing or recording.
     */
    void process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition,
                 bool hostRolling = false);

    /**
     * @brief Arm a crash cymbal (MIDI 49) at the next beat boundary, or at the
     *        pending section bar line when a groove commit is waiting.
     *        Audio thread safe.
     */
    void armTransitionCrash() noexcept { armCrashPending = true; }

    /**
     * @brief Emit every deferred drum/bass/click note-off at @p sampleOffset and
     *        clear the tables. Used on seek, silence, and host bypass so ringing
     *        cymbals/bass cannot stick. Audio thread safe.
     */
    void flushAllPendingNoteOffs(juce::MidiBuffer& midi, int sampleOffset = 0) noexcept;

    /**
     * @brief Overlay library fill 17/18/19 in beat time (barStart + beatOffset).
     *        17: beat ≥ 3.0; 18: beat ≥ 2.0; 19: entire bar. Independent of block size.
     *        @p fromNextBar defers emission until the next bar downbeat (fill 19
     *        armed on the penultimate bar so last-bar beat 1 is already the fill).
     */
    void armBarFill(int fillPatternIndex, bool fromNextBar = false) noexcept;

    /**
     * @brief Arm fill 17/18/19 at an explicit host-grid bar origin (T7.2).
     *        The origin is snapped to the nearest bar line so a late latch
     *        inside the last bar still plays that bar, not the next section.
     */
    void armBarFillAtBeat(int fillPatternIndex, double originBeat) noexcept;

    double getBarFillStartBeat() const noexcept { return barFillStartBeat_; }
    int getPendingBarFillIndex() const noexcept { return pendingBarFillIndex_; }

    // ── Musicality pivot (Workstream A / B1) ──────────────────────────────────

    /** @brief Swing/shuffle ratio in [0,1]; delays off-8th events (A2.3). Audio thread. */
    void setSwing(float newSwing) noexcept;

    /**
     * @brief Ornament probability scale in [0,1] (T4.3). 0 disables every
     *        per-bar mutation; the plugin default is 0.35. Audio thread.
     */
    void setHumanize(float amount) noexcept;
    float getHumanize() const noexcept { return humanizeAmount; }

    /** @brief Current section; drives velocity contrast (A3.1) and bass harmony (A1.2). Audio thread. */
    void setSection(Groove::SongSectionId s) noexcept { sectionId = s; }

    /**
     * @brief Guitarist-energy multiplier for the accompaniment (drums + bass).
     *        Lets the kit swell with the guitarist's picking and relax when they
     *        ease off. Clamped to [0.85, 1.20] (≈ −1.4 dB .. +1.6 dB). Audio thread.
     */
    void setGuitarEnergy(float e) noexcept { guitarEnergy = juce::jlimit(0.85f, 1.20f, e); }
    float getGuitarEnergy() const noexcept { return guitarEnergy; }

    /**
     * @brief Fill-grammar context (Phase 37 A1). Set per block on the audio
     *        thread from the same signals the selector uses. `energy` is the raw
     *        input RMS, `density` the guitar onset density (attacks/beat), and
     *        `style` the committed playing-style class (-1 .. 3).
     */
    void setFillEnergy(float rms) noexcept { fillEnergy_ = juce::jlimit(0.0f, 1.0f, rms); }
    void setFillDensity(float attacksPerBeat) noexcept { fillDensity_ = juce::jlimit(0.0f, 8.0f, attacksPerBeat); }
    void setFillStyle(int style) noexcept { fillStyle_ = style; }

    /** @brief Playing-cue nudge for the fill tier (Phase 39-02): +1 = the guitarist
     *  is winding up (swell + dense picking), -1 = dropping out, 0 = neutral.
     *  Latched per bar by the processor; nudges the bank tier by one step. */
    void setFillCue(int cue) noexcept { fillCue_ = juce::jlimit(-1, 1, cue); }

    /**
     * @brief Map a long-window RMS onto the bidirectional energy multiplier.
     *        Silence sits at 0.94 (below unity); a hot signal reaches 1.20.
     *        T3.2: the input must be a *swell* window, not a per-block RMS.
     */
    static float guitarEnergyFromRms(float smoothedRms) noexcept
    {
        return juce::jlimit(0.85f, 1.20f, 0.94f + smoothedRms * 0.55f);
    }

    /** T3.1: scales the velocity product so accents peak near 118, not 127. */
    static constexpr float kVelocityTrim = 0.80f;

    /** @brief Select the genre preset: groove template, section velocities, ghost density (B1). Audio thread. */
    void setGenrePreset(int presetId) noexcept;

    /**
     * @brief Offline / unit-test hook for a rendered GrooveGrid. The live
     *        processor does not call this — drums use Groove::Template humanize.
     *        Kept so GrooveRenderer unit tests (and a later milestone) can
     *        still inject a grid. A non-matching/invalid grid is ignored.
     */
    void setGrooveGrid(const GrooveGrid& grid) noexcept { grooveGrid = grid; }

    /**
     * @brief Salt the deterministic per-event humanisation hash (tests; T3.4).
     *        Same seed + same timeline always yields the same velocities/offsets.
     */
    void setRandomSeed(juce::int64 seed) noexcept
    {
        humanizeSeed_ = static_cast<unsigned>(static_cast<uint64_t>(seed));
    }

    float getSwing() const noexcept { return swing; }

    static constexpr int kBassChannel = BassVoice::kBassChannel;

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
                        const bool occupied[16]);

    /** @brief Tier-0 micro-fill: a two-note tom pickup into the next downbeat. */
    void emitMicroFill(juce::MidiBuffer& midi,
                       int numSamples,
                       double beatStart,
                       double beatEnd,
                       int sampleOffsetBase) noexcept;

    /** @brief Tier-0 additive ornaments (B): extra tom, kick double, snare flam,
     *  ride-bell accent. Absolute-sample placement, bounded to <=4 notes/bar. */
    void emitExtraOrnaments(juce::MidiBuffer& midi,
                            int numSamples,
                            double beatStart,
                            double beatEnd,
                            int sampleOffsetBase) noexcept;

    /** @brief Build the generated fill for a bar (Phase 37 A1). Pure/deterministic;
     *  returns an authored-equivalent score the emit loop renders by absolute sample. */
    FillGrammar::FillScore currentFillScore(int fillIndex, double fillBarStart) const noexcept;

    /** @brief Genre template with the per-pattern feel blended in (Phase 37 C1).
     *  Returns the plain genre template when humanize is off, so humanize=0 output
     *  stays byte-identical to pre-C1. Audio thread; returns a fixed-size POD copy. */
    Groove::Template effectiveTemplate() const noexcept;

    /** @brief Pick a GMD fill-bank entry for the energy tier (Phase 39-01).
     *  @p tier 0=sparse,1=mid,2=dense; @p allowCrash false excludes crash fills.
     *  Returns an index into FillBank::kFills, or -1 when none matches. Pure. */
    int selectBankFillIndex(int tier, bool allowCrash, unsigned seed) const noexcept;

    /** @brief Live fill tier 0=sparse,1=mid,2=dense from energy/density + the
     *  playing cue. Latched at the fill's first block so it stays constant for the
     *  whole fill (a mid-fill tier change would swap the bank entry). */
    int computeFillTier() const noexcept;

    /**
     * @brief Route bass for a range: authored pattern bass, else harmonic fallback.
     *
     * The mirror-owned voice is gated inside @ref BassVoice::emitGrid, so the
     * grid line is a pure gap-filler.
     */
    void emitBassRange(juce::MidiBuffer& midi,
                       int numSamples,
                       double beatStart,
                       double beatEnd,
                       const MidiPattern& pattern,
                       int sampleOffsetBase,
                       bool clampEarly = false);

    /** @brief Emit authored @c pattern.bassEvents transposed to the live root (A1.1). */
    void emitPatternBass(juce::MidiBuffer& midi,
                         int numSamples,
                         double beatStart,
                         double beatEnd,
                         const MidiPattern& pattern,
                         int sampleOffsetBase,
                         bool clampEarly = false);

    /** @brief Harmonic bass engine: root/fourth/fifth/octave per section (A1.2). */
    void emitHarmonicBass(juce::MidiBuffer& midi,
                          int numSamples,
                          double beatStart,
                          double beatEnd,
                          int sampleOffsetBase,
                          bool clampEarly = false);

    /** @brief Semitone interval for the harmonic bass on a beat within a bar. */
    int harmonyDegree(int beatInBar, int bar) const noexcept;

    /** @brief Note-gate multiplier for the current section (legato vs staccato). */
    float sectionBassGate() const noexcept;

    void emitBarFill(juce::MidiBuffer& midi,
                     int numSamples,
                     double beatStart,
                     double beatEnd,
                     int fillPatternIndex,
                     double fillBarStart) noexcept;
    /** Emit a crash cymbal hit with a scheduled note-off (no hanging cymbal). */
    void emitCrashHit(juce::MidiBuffer& midi,
                      int numSamples,
                      int64_t hostSamplePosition,
                      int sampleOffset) noexcept;

    /**
     * @brief Defer a drum note-off past the current block when needed.
     *        Last-write-wins per MIDI note: a re-trigger closes the previous
     *        instance at @p off before the new duration is scheduled.
     *        Call *before* the matching note-on so a same-sample close sorts first.
     */
    void scheduleDrumNoteOff(juce::MidiBuffer& midi, int numSamples,
                             int64_t blockStart, int note, int off, int durSamps) noexcept;

    /** @brief Emit drum note-offs whose absolute sample falls inside this block. */
    void flushDueDrumNoteOffs(juce::MidiBuffer& midi, int numSamples, int64_t blockStart) noexcept;

    void clearDrumNoteOffTable() noexcept;

    /** @brief True when @p pattern already has MIDI 49 within ±20 ms of @p targetBeat. */
    bool patternCrashesNear(const MidiPattern& pattern, double targetBeat) const noexcept;

    /** Metronome: kick on 1, side-stick on 2/3/4. */
    void emitClickTrack(juce::MidiBuffer& midi,
                        int numSamples,
                        double beatStart,
                        double beatEnd,
                        int64_t hostSamplePosition) noexcept;

    /** @brief Bounded gaussian from two unit draws; clamps to ±2.5 sigma (A2.4). */
    static float boundedGaussian(float u1, float u2, float mean, float sigma) noexcept;

    /**
     * @brief How far (in beats) microtiming may pull an event outside
     *        [beatStart, beatEnd). Used to enumerate which pattern occurrences
     *        can land inside a block, so placement is buffer-size independent
     *        (review T9.2).
     */
    void microtimingSlackBeats(double samplesPerBeat, double samplesPerMs, double swingDelayMs,
                               double& earlyBeats, double& lateBeats) const noexcept;

    /**
     * @brief Block-relative sample offset for an absolute event sample, or -1 when
     *        this block must not emit it (a neighbouring block owns it).
     *
     *        An event that microtiming pulled before sample 0 has no earlier block,
     *        so the first block clamps it to 0 — but only within @p slackSamples, so
     *        a distant occurrence cannot be dragged onto the timeline start.
     */
    int placeEvent(int64_t absSample, int numSamples, int64_t slackSamples,
                   bool clampEarly = false) const noexcept;

    /**
     * @brief Deterministic per-event draw keyed by (bar, grid16, voice, salt).
     *        Independent of block size and of how many events were emitted first.
     */
    float eventGaussian(int64_t barNumber, int grid16, int voice,
                        unsigned salt, float sigma) const noexcept;

    /** @brief Soft-knee above 110 so residual peaks keep headroom after jitter (T3.1). */
    static int applyVelocityHeadroom(int vel) noexcept;

    /** @brief Whether ghost notes may be injected for the active section. */
    bool sectionAllowsGhosts() const noexcept;

    const MidiPatternLibrary* library = nullptr;

    double sampleRate = 44100.0;
    unsigned humanizeSeed_ = 0;  // T3.4: salts the per-event hash (not a stream RNG)

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
    bool transportJumped_ = false;   // set in the seek branch; consumed by the processor (T2.2)

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

    // Section hand-off: a short bass pickup armed by the processor on a section's
    // last bar. The player emits it once as a block crosses the bar's last beat.
    bool bassLeadInArmed = false;

    bool beatGridBassEnabled_ = true;   // Play / BListen grid fallback; off for frozen riffs
    bool beatGridBassPrev_ = false;     // previous block's grid-bass state (phase onset)

    // The one monophonic bass voice: producer arbitration, the grid gate, and
    // per-note provenance. See BassVoice and docs/BASS_MIRRORING.md §2/§6.
    BassVoice bassVoice;

    int pendingBarFillIndex_ = -1;      // 17/18/19 overlay; -1 = none
    double barFillStartBeat_ = -1.0;    // >=0: defer emit until this beat; -1 now; -2 resolve next process

    // Pending learned-bass note-ons live in BassVoice (fixed queue, no heap).

    // Transition crash state. The sounding note-off lives on drumNoteOffSample[kCrashNote]
    // (T1.1); crashNoteOffSample mirrors that slot so seek/silence flushes stay explicit
    // about the armed-crash voice (T1.2). Not a second scheduler.
    bool armCrashPending = false;
    int64_t crashNoteOffSample = -1;

    // Deferred drum note-offs (T1.1). Pattern crashes, hats, fills and ghosts used
    // to clamp the off into the triggering block, so cymbals choked at the buffer
    // boundary. One slot per MIDI note; -1 = none. Armed-crash offs share note 49.
    static constexpr int kDrumVoices = 128;
    std::array<int64_t, kDrumVoices> drumNoteOffSample{};

    // ── Musicality pivot state (Workstream A / B1) ────────────────────────────
    Groove::Template grooveTemplate;            // genre velocity hierarchy + microtiming
    int currentTemplateId = 0;                  // genre template id (per-pattern feel lookup)
    GrooveGrid grooveGrid;                      // Tier-1 rendered groove (else template)
    Groove::GenrePreset preset;                 // active genre preset (copy)
    Groove::SongSectionId sectionId = Groove::SongSectionId::Verse;
    float swing = 0.0f;                         // 0..1 (A2.3)
    float humanizeAmount = 1.0f;                // 0..1 ornament scale (T4.3; tests default full)

    // Fill-grammar context (Phase 37 A1). Written per block on the audio thread.
    float fillEnergy_ = 0.0f;                   // raw input RMS [0,1]
    float fillDensity_ = 0.0f;                  // guitar onset density (attacks/beat)
    int   fillStyle_ = -1;                      // committed style -1..3
    int   fillCue_ = 0;                         // playing-cue nudge -1..+1 (39-02)
    int   fillGenreBias_ = 0;                   // per-genre fill-tier bias (39-04)
    int   fillTierLatched_ = 1;                 // tier latched at the fill's start
    bool  fillWasEmitting_ = false;             // fill edge detector for the latch
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
