#pragma once

#include <string>
#include <vector>

struct MidiEvent
{
    uint8_t note = 0;
    uint8_t velocity = 100;
    float beatOffset = 0.0f;
    float durationBeats = 0.25f;
};

struct MidiPattern
{
    std::string name;
    float lengthInBars = 1.0f;
    std::vector<MidiEvent> drumEvents;
    std::vector<MidiEvent> bassEvents;
};

class MidiPatternLibrary
{
public:
    MidiPatternLibrary();

    const MidiPattern& getPattern(int index) const;
    int patternCount() const noexcept { return 7; }

private:
    std::vector<MidiPattern> patterns;
};
