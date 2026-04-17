#include "PatternPlayer.h"
#include <cmath>

void PatternPlayer::prepare(double newSampleRate, int blockSize)
{
    (void)blockSize;
    sampleRate = newSampleRate;
    rng.setSeedRandomly();
    reset();
}

void PatternPlayer::reset()
{
    beatPosition = 0.0;
    sampleCounter = 0;
    activePatternIndex = patternIndex.load(std::memory_order_relaxed);
    pendingPatternIndex = -1;
    wasSilent = false;
    bassSemitoneOffset = 0;
    generativeBassActive = false;
    generativeBassRootMidi = 40;
    generativeBassDurationBeats = 1.0f;
}

void PatternPlayer::setBassSemitoneOffset(int semitones)
{
    bassSemitoneOffset = juce::jlimit(-24, 24, semitones);
}

void PatternPlayer::setGenerativeBassActive(bool active, int rootMidi, float durationBeats)
{
    generativeBassActive = active;
    generativeBassRootMidi = juce::jlimit(28, 55, rootMidi);
    generativeBassDurationBeats = juce::jlimit(0.0625f, 4.0f, durationBeats);
}

void PatternPlayer::setBpm(float newBpm)
{
    bpm = juce::jlimit(40.0f, 320.0f, newBpm);
}

void PatternPlayer::setPatternIndex(int index)
{
    patternIndex.store(index, std::memory_order_relaxed);
}

void PatternPlayer::setStructureSilent(bool silent)
{
    structureSilent = silent;
}

int PatternPlayer::humanVel(int base) const
{
    const int delta = rng.nextInt(21) - 10;
    return juce::jlimit(1, 127, base + delta);
}

int PatternPlayer::humanSamples() const
{
    const int maxOff = static_cast<int>(std::round(0.002 * sampleRate));
    return rng.nextInt(maxOff * 2 + 1) - maxOff;
}

void PatternPlayer::emitEventsForRange(juce::MidiBuffer& midi,
                                       int numSamples,
                                       double beatStart,
                                       double beatEnd,
                                       const MidiPattern& pattern,
                                       int sampleOffsetBase)
{
    if (pattern.lengthInBars <= 0.0f)
        return;

    const double patternLenBeats = static_cast<double>(pattern.lengthInBars) * 4.0;

    auto emitForList = [&](const std::vector<MidiEvent>& list, int channel) {
        for (const auto& ev : list)
        {
            double t = static_cast<double>(ev.beatOffset);
            while (t < beatStart)
                t += patternLenBeats;

            while (t < beatEnd - 1.0e-9)
            {
                const double rel = t - beatStart;
                int off = static_cast<int>(std::round(rel * (sampleRate * 60.0 / static_cast<double>(bpm))));
                off += humanSamples();
                off = juce::jlimit(0, numSamples - 1, off);

                const int vel = humanVel(static_cast<int>(ev.velocity));
                const int baseNote = static_cast<int>(ev.note);
                const int outNote = juce::jlimit(
                    0,
                    127,
                    baseNote + (channel == kBassChannel ? bassSemitoneOffset : 0));

                midi.addEvent(juce::MidiMessage::noteOn(channel, outNote, static_cast<float>(vel) / 127.0f),
                                sampleOffsetBase + off);

                const int durSamps = juce::jmax(
                    1,
                    static_cast<int>(std::round(static_cast<double>(ev.durationBeats) * (60.0 / static_cast<double>(bpm)) * sampleRate)));
                const int noteOffOffset = juce::jmin(numSamples - 1, off + durSamps);
                midi.addEvent(juce::MidiMessage::noteOff(channel, outNote), sampleOffsetBase + noteOffOffset);

                t += patternLenBeats;
            }
        }
    };

    emitForList(pattern.drumEvents, kDrumChannel);
    if (generativeBassActive)
        emitGenerativeBassForWindow(midi, numSamples, beatStart, beatEnd, sampleOffsetBase);
    else
        emitForList(pattern.bassEvents, kBassChannel);
}

void PatternPlayer::emitGenerativeBassForWindow(juce::MidiBuffer& midi,
                                                int numSamples,
                                                double beatStart,
                                                double beatEnd,
                                                int sampleOffsetBase)
{
    if (beatEnd <= beatStart + 1.0e-9)
        return;

    const double rel = 0.0;
    int off = static_cast<int>(std::round(rel * (sampleRate * 60.0 / static_cast<double>(bpm))));
    off += humanSamples();
    off = juce::jlimit(0, numSamples - 1, off);

    const int vel = humanVel(100);
    const int outNote = juce::jlimit(
        0,
        127,
        generativeBassRootMidi + bassSemitoneOffset);

    midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, outNote, static_cast<float>(vel) / 127.0f),
                  sampleOffsetBase + off);

    const int durSamps = juce::jmax(
        1,
        static_cast<int>(std::round(static_cast<double>(generativeBassDurationBeats)
                                    * (60.0 / static_cast<double>(bpm)) * sampleRate)));
    const int noteOffOffset = juce::jmin(numSamples - 1, off + durSamps);
    midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, outNote), sampleOffsetBase + noteOffOffset);
}

void PatternPlayer::process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition)
{
    (void)hostSamplePosition;

    if (library == nullptr)
        return;

    const int requested = patternIndex.load(std::memory_order_relaxed);
    if (requested != activePatternIndex && pendingPatternIndex < 0)
        pendingPatternIndex = requested;

    if (structureSilent)
    {
        if (!wasSilent)
        {
            for (int ch = 1; ch <= 16; ++ch)
            {
                midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
            }
        }
        wasSilent = true;
        beatPosition += static_cast<double>(numSamples) * bpm / (60.0 * sampleRate);
        sampleCounter += static_cast<int64_t>(numSamples);
        return;
    }

    wasSilent = false;

    if (pendingPatternIndex >= 0)
    {
        const double barPhase = std::fmod(beatPosition, 4.0);
        const bool atBar = barPhase < 1.0e-4 || (4.0 - barPhase) < 1.0e-3;
        if (atBar)
        {
            activePatternIndex = pendingPatternIndex;
            pendingPatternIndex = -1;
        }
    }

    const MidiPattern& pattern = library->getPattern(activePatternIndex);

    const double samplesPerBeat = (60.0 / static_cast<double>(bpm)) * sampleRate;
    const double beatStart = beatPosition;
    const double beatEnd = beatPosition + static_cast<double>(numSamples) / samplesPerBeat;

    // Pattern index 0 skips library pattern content; with generative bass we still render
    // drums from the pattern (often empty) and the generative bass note for this window.
    if (activePatternIndex != 0 || generativeBassActive)
        emitEventsForRange(midi, numSamples, beatStart, beatEnd, pattern, 0);

    beatPosition = beatEnd;
    sampleCounter += static_cast<int64_t>(numSamples);
}
