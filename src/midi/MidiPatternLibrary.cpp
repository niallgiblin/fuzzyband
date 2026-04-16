#include "MidiPatternLibrary.h"
#include <algorithm>

namespace
{
constexpr uint8_t kKick = 36;
constexpr uint8_t kSnare = 38;
constexpr uint8_t kHatClosed = 42;
constexpr uint8_t kHatOpen = 46;
constexpr uint8_t kRide = 51;
constexpr uint8_t kBassRoot = 40; // E2 placeholder

MidiEvent drum(uint8_t n, uint8_t vel, float beat, float dur = 0.25f)
{
    return MidiEvent{ n, vel, beat, dur };
}

MidiEvent bass(uint8_t n, uint8_t vel, float beat, float dur = 4.0f)
{
    return MidiEvent{ n, vel, beat, dur };
}

MidiPattern buildSilent()
{
    MidiPattern p;
    p.name = "Silent";
    p.lengthInBars = 1.0f;
    return p;
}

MidiPattern buildVerseSlow()
{
    MidiPattern p;
    p.name = "Verse slow";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick, 110, 0.0f),
        drum(kHatClosed, 80, 0.5f),
        drum(kSnare, 105, 1.0f),
        drum(kHatClosed, 80, 1.5f),
        drum(kKick, 100, 2.0f),
        drum(kHatClosed, 78, 2.5f),
        drum(kSnare, 102, 3.0f),
        drum(kHatClosed, 78, 3.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 95, 0.0f, 3.9f),
    };
    return p;
}

MidiPattern buildVerseMid()
{
    MidiPattern p;
    p.name = "Verse mid";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick, 115, 0.0f),
        drum(kHatClosed, 85, 0.5f),
        drum(kSnare, 110, 1.0f),
        drum(kHatClosed, 85, 1.5f),
        drum(kKick, 108, 2.0f),
        drum(kHatClosed, 82, 2.5f),
        drum(kSnare, 108, 3.0f),
        drum(kHatClosed, 82, 3.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 98, 0.0f, 1.9f),
        bass(kBassRoot, 92, 2.0f, 1.9f),
    };
    return p;
}

MidiPattern buildVerseFast()
{
    MidiPattern p;
    p.name = "Verse fast";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick, 118, 0.0f),
        drum(kHatClosed, 88, 0.25f),
        drum(kHatClosed, 86, 0.5f),
        drum(kHatClosed, 86, 0.75f),
        drum(kSnare, 114, 1.0f),
        drum(kHatClosed, 86, 1.25f),
        drum(kKick, 112, 1.5f),
        drum(kHatClosed, 84, 1.75f),
        drum(kHatClosed, 86, 2.0f),
        drum(kHatClosed, 84, 2.25f),
        drum(kSnare, 112, 2.5f),
        drum(kHatClosed, 84, 2.75f),
        drum(kKick, 110, 3.0f),
        drum(kHatClosed, 82, 3.25f),
        drum(kHatClosed, 82, 3.5f),
        drum(kHatClosed, 82, 3.75f),
    };
    p.bassEvents = {
        bass(kBassRoot, 100, 0.0f, 0.95f),
        bass(kBassRoot, 96, 1.0f, 0.95f),
        bass(kBassRoot, 96, 2.0f, 0.95f),
        bass(kBassRoot, 94, 3.0f, 0.95f),
    };
    return p;
}

MidiPattern buildChorusMid()
{
    MidiPattern p;
    p.name = "Chorus mid";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick, 120, 0.0f),
        drum(kHatOpen, 90, 0.5f),
        drum(kSnare, 118, 1.0f),
        drum(kHatOpen, 88, 1.5f),
        drum(kKick, 115, 2.0f),
        drum(kRide, 85, 2.5f),
        drum(kSnare, 116, 3.0f),
        drum(kHatOpen, 86, 3.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 102, 0.0f, 1.45f),
        bass(static_cast<uint8_t>(kBassRoot + 7), 98, 1.5f, 1.45f),
        bass(kBassRoot, 100, 3.0f, 0.95f),
    };
    return p;
}

MidiPattern buildChorusFast()
{
    MidiPattern p;
    p.name = "Chorus fast";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick, 125, 0.0f),
        drum(kKick, 118, 0.25f),
        drum(kHatOpen, 92, 0.5f),
        drum(kKick, 120, 0.75f),
        drum(kSnare, 122, 1.0f),
        drum(kKick, 118, 1.25f),
        drum(kHatOpen, 90, 1.5f),
        drum(kKick, 120, 1.75f),
        drum(kSnare, 120, 2.0f),
        drum(kKick, 118, 2.25f),
        drum(kRide, 88, 2.5f),
        drum(kKick, 120, 2.75f),
        drum(kSnare, 122, 3.0f),
        drum(kKick, 118, 3.25f),
        drum(kHatOpen, 90, 3.5f),
        drum(kKick, 120, 3.75f),
    };
    p.bassEvents = {
        bass(kBassRoot, 104, 0.0f, 0.45f),
        bass(static_cast<uint8_t>(kBassRoot + 7), 100, 0.5f, 0.45f),
        bass(kBassRoot, 102, 1.0f, 0.45f),
        bass(static_cast<uint8_t>(kBassRoot + 7), 100, 1.5f, 0.45f),
        bass(kBassRoot, 102, 2.0f, 0.45f),
        bass(static_cast<uint8_t>(kBassRoot + 7), 100, 2.5f, 0.45f),
        bass(kBassRoot, 102, 3.0f, 0.45f),
        bass(static_cast<uint8_t>(kBassRoot + 7), 100, 3.5f, 0.45f),
    };
    return p;
}

MidiPattern buildBreakdown()
{
    MidiPattern p;
    p.name = "Breakdown";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        drum(kKick, 118, 0.0f),
        drum(kSnare, 70, 0.75f),
        drum(kSnare, 72, 1.25f),
        drum(kKick, 112, 2.0f),
        drum(kSnare, 68, 3.0f),
        drum(kSnare, 75, 3.5f),
        drum(kKick, 116, 4.0f),
        drum(kSnare, 72, 4.75f),
        drum(kSnare, 74, 5.25f),
        drum(kKick, 110, 6.0f),
        drum(kSnare, 70, 7.0f),
        drum(kSnare, 76, 7.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 96, 0.0f, 1.9f),
        bass(kBassRoot, 90, 2.25f, 0.4f),
        bass(kBassRoot, 94, 4.0f, 1.9f),
        bass(kBassRoot, 88, 6.25f, 0.4f),
    };
    return p;
}
} // namespace

MidiPatternLibrary::MidiPatternLibrary()
{
    patterns.push_back(buildSilent());
    patterns.push_back(buildVerseSlow());
    patterns.push_back(buildVerseMid());
    patterns.push_back(buildVerseFast());
    patterns.push_back(buildChorusMid());
    patterns.push_back(buildChorusFast());
    patterns.push_back(buildBreakdown());
}

const MidiPattern& MidiPatternLibrary::getPattern(int index) const
{
    const int i = std::clamp(index, 0, patternCount() - 1);
    return patterns[static_cast<size_t>(i)];
}
