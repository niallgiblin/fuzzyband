#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "MidiPatternLibrary.h"
#include <atomic>

class PatternPlayer
{
public:
    void setPatternLibrary(const MidiPatternLibrary* lib) { library = lib; }

    void prepare(double sampleRate, int blockSize);

    /** Resets beat clock state; no heap — safe from the audio thread. */
    void reset();

    void setBpm(float bpm);
    void setPatternIndex(int index);
    void setStructureSilent(bool silent);

    void process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition);

private:
    void emitEventsForRange(juce::MidiBuffer& midi,
                            int numSamples,
                            double beatStart,
                            double beatEnd,
                            const MidiPattern& pattern,
                            int sampleOffsetBase);

    int humanVel(int base) const;
    int humanSamples() const;

    const MidiPatternLibrary* library = nullptr;

    double sampleRate = 44100.0;
    mutable juce::Random rng;

    float bpm = 120.0f;
    std::atomic<int> patternIndex{ 0 };
    int activePatternIndex = 0;
    int pendingPatternIndex = -1;

    double beatPosition = 0.0;
    int64_t sampleCounter = 0;

    bool structureSilent = false;
    bool wasSilent = false;

    static constexpr int kDrumChannel = 10;
    static constexpr int kBassChannel = 2;
};
