#pragma once

/**
 * @file
 * @brief The plugin's one monophonic bass voice: producer arbitration + provenance.
 *
 * Five producers can sound the bass (MIDI ch. 2): the live **mirror**, the
 * **frozen** learned-riff snapshot, the authored **grid** line, the **harmonic**
 * grid fallback, and the bar-line **pickup**. There is exactly one voice, so they
 * compete. This module owns that competition — it closes a ringing note when a
 * new one starts, holds a mirror note until the guitarist stops, gates the grid
 * out while the mirror owns the voice, and stamps every note-on with the producer
 * that emitted it.
 *
 * Why it exists: "bass doesn't mirror" regressed nine times because ownership
 * was an untyped `int64_t` written in one module and read by three emitters
 * (`docs/BASS_MIRRORING.md` §2, §6). The same contract now lives here, and
 * @ref getRecentNoteOns makes "who is playing the bass" observable.
 *
 * Real-time safe: fixed-size arrays, no allocation, no locks. The behaviour is
 * frozen to the 1.0.3 ownership contract (`docs/BASS_MIRRORING.md` §7).
 */

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

class BassVoice
{
public:
    /** Which producer emitted (or was refused) a bass note. */
    enum class Producer : std::uint8_t
    {
        None = 0,
        Mirror,        // live mirror of the guitarist's attacks
        Frozen,        // learned-riff snapshot (RiffA / RiffBLocked)
        GridAuthored,  // pattern.bassEvents transposed to the live root
        GridHarmonic,  // root/fourth/fifth/octave fallback
        Pickup         // section hand-off approach note
    };

    /** One emitted bass note-on, tagged with its producer (provenance). */
    struct NoteOn
    {
        Producer producer = Producer::None;
        int midi = 0;
        std::int64_t sample = 0;
        bool held = false;
    };

    static constexpr int kBassChannel = 2;

    /** @brief Drop all voice state and counters (no MIDI emitted). */
    void reset() noexcept;

    // ── Producer requests ────────────────────────────────────────────────────

    /**
     * @brief Queue a learned note (live mirror or frozen snapshot).
     * @param hold Sustain until the guitarist stops instead of gating at @p duration.
     */
    void requestLearned(int midiNote, float velocity, int sampleOffset, int duration,
                        bool hold, Producer source) noexcept;

    /** @brief Emit a section hand-off pickup. Bypasses the grid gate. */
    void emitPickup(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart,
                    int note, int vel, int off, int durSamps) noexcept;

    /**
     * @brief Emit one grid note (authored or harmonic).
     *
     * Returns false (emitting nothing) when the mirror/frozen voice still owns
     * @p absSample — the grid is a gap-filler, never a layer.
     */
    bool emitGrid(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart,
                  std::int64_t absSample, int note, int vel, int off, int durSamps,
                  int sampleOffsetBase, bool forceRetrigger, Producer source) noexcept;

    // ── Per-block lifecycle (driven by PatternPlayer::process) ──────────────

    /** @brief Emit queued learned notes and extend the grid gate. */
    void flushLearned(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart) noexcept;

    /** @brief Release a held note once the guitarist has actually stopped. */
    void releaseHeldIfStopped(juce::MidiBuffer& midi, std::int64_t blockStart) noexcept;

    /** @brief Close a note whose natural gate expires in this block. */
    void closeNaturalNoteOff(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart) noexcept;

    /** @brief Drop queued learned notes (seek / silence). */
    void clearPending() noexcept;

    /** @brief Emit every pending bass note-off and drop the voice's grid claim. */
    void flushAll(juce::MidiBuffer& midi, int sampleOffset) noexcept;

    void setGuitarAudible(bool audible) noexcept { guitarAudible_ = audible; }
    bool isGuitarAudible() const noexcept { return guitarAudible_; }

    /**
     * @brief Current input (onset) level, 0..1. Used to release a held mirror
     *        note when the note has actually decayed, instead of ringing through
     *        a rest until the slow structure gate notices silence.
     */
    void setInputLevel(float level) noexcept { inputLevel_ = level; }

    /** @brief Grid events before this absolute sample are gap-fills only. */
    std::int64_t getGridGateSample() const noexcept { return gridGateSample_; }

    // ── Provenance / diagnostics ─────────────────────────────────────────────

    /** @brief Producer of the most recent note-on. */
    Producer getLastProducer() const noexcept { return lastProducer_; }

    /** @brief Most-recent-first note-ons. Returns how many were written. */
    int getRecentNoteOns(NoteOn* out, int maxCount) const noexcept;

    /** @brief Note-ons emitted by @p source (emit-time, provenance). */
    int getProducerCount(Producer source) const noexcept;

    /** @brief Mirror + frozen request count (legacy `getLearnedBassNoteCount`). */
    int getLearnedCount() const noexcept { return learnedRequests_; }

    /** @brief Authored + harmonic emit count (legacy `getGridBassNoteCount`). */
    int getGridCount() const noexcept { return gridEmits_; }

    void resetCounters() noexcept;

private:
    void emit(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart,
              int note, int vel, int off, int durSamps, int sampleOffsetBase,
              bool forceRetrigger, bool hold, Producer source) noexcept;

    void record(Producer source, int midi, std::int64_t sample, bool held) noexcept;

    static constexpr int kMaxPending = 8;
    static constexpr int kMaxProvenance = 32;
    static constexpr int kProducerCount = 6;

    struct PendingNote
    {
        bool active = false;
        int midi = 40;
        float vel = 0.58f;
        int offset = 0;
        int duration = 10000;
        bool hold = false;
        Producer source = Producer::Mirror;
    };
    std::array<PendingNote, kMaxPending> pending_{};

    // The ringing note (monophonic voice).
    int bassNoteOffMidi_ = 40;
    std::int64_t bassNoteOffSample_ = -1;
    bool bassNoteHeld_ = false;

    bool guitarAudible_ = false;
    float inputLevel_ = 0.0f;      // current onset level (see setInputLevel)
    float heldNoteLevel_ = 0.0f;   // onset level when the held note started

    // A held note releases when the input falls to this fraction of its attack
    // level (the note ended) - not only when the slow structure gate says silent.
    static constexpr float kDecayRelease = 0.25f;

    // Absolute sample until which the mirror/frozen voice owns the grid.
    // std::numeric_limits<int64_t>::max() means "held until the guitar stops".
    std::int64_t gridGateSample_ = -1;

    std::array<NoteOn, kMaxProvenance> recent_{};
    int recentWrite_ = 0;
    int recentCount_ = 0;
    Producer lastProducer_ = Producer::None;
    std::array<int, kProducerCount> counts_{};

    int learnedRequests_ = 0;
    int gridEmits_ = 0;
};
