#pragma once

/**
 * @file
 * @brief constexpr-friendly MIDI pattern definitions for drums and bass.
 */

#include <cstdint>
#include <string>
#include <vector>

/** @brief One note in a scored pattern (beat offset and duration in beats). */
struct MidiEvent
{
    uint8_t note = 0;
    uint8_t velocity = 100;
    float beatOffset = 0.0f;
    float durationBeats = 0.25f;
    bool isGhost = false;  // authored ghost; must sit at velocity <= ghostThreshold (T1.5)
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
 * @brief Owns the fixed set of patterns indexed by @ref IInference output.
 *
 * Indices 0-21 are the original metal set; 22-27 are the rock-first set
 * added by the Musicality & Rock Pivot (Workstream A4.1). Keep this constant
 * in sync with PatternRules::kPatternCount (enforced by a unit test).
 */
class MidiPatternLibrary
{
public:
    static constexpr int kPatternCount = 28;

    MidiPatternLibrary();

    const MidiPattern& getPattern(int index) const;
    int patternCount() const noexcept { return kPatternCount; }

private:
    std::vector<MidiPattern> patterns;
};
