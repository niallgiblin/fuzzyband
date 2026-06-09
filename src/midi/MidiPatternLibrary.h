#pragma once

/**
 * @file
 * @brief constexpr-friendly MIDI pattern definitions for drums and bass.
 */

#include <string>
#include <vector>

/** @brief One note in a scored pattern (beat offset and duration in beats). */
struct MidiEvent
{
    uint8_t note = 0;
    uint8_t velocity = 100;
    float beatOffset = 0.0f;
    float durationBeats = 0.25f;
};

/** @brief Named loop of drum and bass events with bar length. */
struct MidiPattern
{
    std::string name;
    float lengthInBars = 1.0f;
    std::vector<MidiEvent> drumEvents;
    std::vector<MidiEvent> bassEvents;
};

/**
 * @brief Owns the fixed set of metal patterns indexed by @ref IInference output.
 */
class MidiPatternLibrary
{
public:
    MidiPatternLibrary();

    const MidiPattern& getPattern(int index) const;
    int patternCount() const noexcept { return 22; }

private:
    std::vector<MidiPattern> patterns;
};
