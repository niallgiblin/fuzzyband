#include <juce_audio_basics/juce_audio_basics.h>
#include "midi/MidiPatternLibrary.h"
#include <iostream>

static void appendPattern(juce::MidiMessageSequence& track,
                          const MidiPattern& pattern,
                          double bpm,
                          double& timeSec)
{
    const double secPerBeat = 60.0 / bpm;
    const double patternLenBeats = static_cast<double>(pattern.lengthInBars) * 4.0;

    auto addList = [&](const std::vector<MidiEvent>& list, int channel) {
        for (const auto& ev : list)
        {
            const double tOn = timeSec + static_cast<double>(ev.beatOffset) * secPerBeat;
            const double tOff = tOn + static_cast<double>(ev.durationBeats) * secPerBeat;
            track.addEvent(juce::MidiMessage::noteOn(channel, static_cast<int>(ev.note),
                                                     static_cast<float>(ev.velocity) / 127.0f),
                           tOn);
            track.addEvent(juce::MidiMessage::noteOff(channel, static_cast<int>(ev.note)), tOff);
        }
    };

    addList(pattern.drumEvents, 10);
    addList(pattern.bassEvents, 2);

    timeSec += patternLenBeats * secPerBeat;
}

int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);

    const double bpm = 120.0;
    MidiPatternLibrary lib;

    juce::MidiFile file;
    file.setTicksPerQuarterNote(960);

    juce::MidiMessageSequence meta;
    meta.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / bpm)), 0.0);
    file.addTrack(meta);

    juce::MidiMessageSequence track;
    double t = 0.0;
    for (int i = 0; i < lib.patternCount(); ++i)
        appendPattern(track, lib.getPattern(i), bpm, t);

    file.addTrack(track);

    const juce::File out = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                               .getParentDirectory()
                               .getChildFile("metal_accompaniment_patterns.mid");

    juce::FileOutputStream stream(out);
    if (!stream.openedOk())
    {
        std::cerr << "Failed to open output file.\n";
        return 1;
    }

    file.writeTo(stream);
    std::cout << "Wrote " << out.getFullPathName().toStdString() << "\n";
    return 0;
}
