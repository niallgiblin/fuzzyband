#pragma once

/**
 * @file
 * @brief Structure sequencer: drives section transitions on bar boundaries via sample counter.
 *
 * Replaces the reactive StructureTagger as the primary section driver (D006).
 * Holds an ordered list of song sections, advances on bar boundaries, and exposes
 * the current section for pattern selection and display.
 */

#include <string>
#include <vector>

/**
 * @brief One section in a song form: name + duration in bars.
 */
struct SongSection
{
    std::string name;
    int bars = 8; // duration in bars (4 beats each)
};

/**
 * @brief Preset song form — list of sections that repeats or plays once.
 */
struct SongForm
{
    std::string name;
    std::vector<SongSection> sections;
};

/**
 * @brief Drives section transitions on bar boundaries using accumulated sample count.
 *
 * Audio-thread safe: all state updates are plain value writes.
 * Call @ref advance from the audio thread each block.
 */
class StructureSequencer
{
public:
    StructureSequencer();

    /** @brief Load a song form and reset to start. */
    void loadForm(const SongForm& form);

    /** @brief Current section index in the form [0, sectionCount-1]. */
    int getCurrentSectionIndex() const noexcept { return currentSection; }

    /** @brief Name of the current section (e.g. "VERSE", "CHORUS"). */
    const char* getCurrentSectionName() const noexcept;

    /** @brief Bars elapsed in the current section [0, totalBars-1]. */
    int getBarsElapsed() const noexcept { return barsElapsed; }

    /** @brief Total bars in the current section. */
    int getBarsInSection() const noexcept;

    /** @brief Whether this is the first bar of a new section (for fill triggering). */
    bool isFirstBar() const noexcept { return barsElapsed == 0; }

    /** @brief Whether this is the last bar of the section (for fill triggering). */
    bool isLastBar() const noexcept;

    /** @brief Total number of bars processed since reset (global bar counter). */
    int getGlobalBarCount() const noexcept { return globalBarCount; }

    /**
     * @brief Advance the sequencer by @p numSamples at the given @p bpm and @p sampleRate.
     *
     * When enough samples accumulate for a bar boundary, advances barsElapsed.
     * When barsElapsed reaches the section's bar count, advances to the next section.
     * When the form ends, loops back to the first section.
     *
     * @param numSamples samples processed in this block
     * @param bpm current BPM
     * @param sampleRate current sample rate
     */
    void advance(int numSamples, float bpm, double sampleRate);

    /** @brief Forcibly reset to the first section, bar 0. Call on session restart or gate close. */
    void reset();

    /** @brief Access available song form presets. */
    static const std::vector<SongForm>& getPresets() noexcept { return presets; }

    /** @brief Load a preset by index. */
    void loadPreset(int presetIndex);

private:
    static std::vector<SongForm> buildPresets();

    int currentSection = 0;
    int barsElapsed = 0;
    int globalBarCount = 0;
    double barAccumulator = 0.0;  // fractional samples accumulated toward next bar
    double samplesPerBar = 0.0;   // cached: (60.0 / bpm) * sampleRate * 4.0

    SongForm currentForm;
    static const std::vector<SongForm> presets;
};
