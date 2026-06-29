#pragma once

/**
 * @file
 * @brief Rule-based bass generator: doubles guitar pitch at bar boundaries.
 *
 * Replaces the broken ONNX bass model (D006). Samples detected guitar pitch,
 * maps to bass range, and produces PatternPlayer::GrooveCommit with bass data.
 * Section-aware: different behavior for VERSE/CHORUS/BREAKDOWN/AMBIENT/SILENT.
 */

#include "midi/PatternPlayer.h"
#include "analysis/StructureSequencer.h"

/**
 * @brief Stateless bass pattern generator.
 *
 * Call @ref generate from the inference thread. All inputs are passed as parameters.
 */
class RuleBasedBass
{
public:
    /** Velocity multiplier to make bass audible (ONNX produces ~0.2-0.5 of 127). */
    static constexpr float kVelocityMultiplier = 2.5f;

    /** Bass note range: C1 (24) to E3 (52) — accommodates drop C tuning lowest root. */
    static constexpr float kBassMinMidi = 24.0f;
    static constexpr float kBassMaxMidi = 52.0f;

    /** Map guitar MIDI down one octave to bass range. */
    static float mapToBassRange(float guitarMidi) noexcept;

    /**
     * @brief Generate a GrooveCommit with bass data for one bar.
     *
     * @param guitarRootMidi detected guitar pitch (MIDI, may be fractional)
     * @param sectionName current section from StructureSequencer
     * @param isFirstBar whether this is the first bar of a section
     * @param bpm current BPM (used to determine note durations)
     * @return GrooveCommit with hasBassFrame=true and populated bass fields
     */
    static PatternPlayer::GrooveCommit generate(
        float guitarRootMidi,
        const char* sectionName,
        bool isFirstBar,
        float bpm) noexcept;

private:
    /** Determine how many bass notes to play based on section. */
    static int notesPerBar(const char* sectionName) noexcept;

    /** Get base velocity for section (before multiplier). */
    static float baseVelocity(const char* sectionName) noexcept;
};
