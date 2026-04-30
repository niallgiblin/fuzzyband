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
    genBassHasSteps = false;
    for (int i = 0; i < 16; ++i) { genBassPitchOffset[i] = 0.0f; genBassVelocity[i] = 0.0f; }
    genBassStepRootMidi = 40.0f;
    genBassAbsNoteOffSample = -1;
    genStepsDeferCount = 0;
    genBassLastMidiNote = 40;
    libBassAbsNoteOffSample = -1;
    libBassLastMidiNote = 40;
    fireTransitionCrash = false;
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
    genBassHasSteps = false; // piano-roll overrides single-note path
    genStepsDeferCount = 0;
}

void PatternPlayer::setGenerativeBassSteps(const float pitchOffset[16],
                                            const float velocity[16],
                                            float rootMidi)
{
    for (int i = 0; i < 16; ++i)
    {
        genBassPitchOffset[i] = juce::jlimit(-12.0f, 12.0f, pitchOffset[i]);
        genBassVelocity[i]   = juce::jlimit(0.0f, 127.0f, velocity[i]);
    }
    genBassStepRootMidi = juce::jlimit(28.0f, 55.0f, rootMidi);
    genBassHasSteps = true;
    generativeBassActive = true; // activate the generative path
    genBassAbsNoteOffSample = -1; // mutual exclusion with piano-roll steps defers on separate store
}

void PatternPlayer::setBpm(float newBpm)
{
    const float clamped = juce::jlimit(40.0f, 320.0f, newBpm);
    bpm += 0.1f * (clamped - bpm);
}

void PatternPlayer::setPatternIndex(int index)
{
    patternIndex.store(index, std::memory_order_relaxed);
}

void PatternPlayer::setStructureSilent(bool silent)
{
    structureSilent = silent;
}

void PatternPlayer::snapToBarStart()
{
    beatPosition = 0.0;
    pendingPatternIndex = -1;
}

void PatternPlayer::snapBpm(float newBpm)
{
    bpm = juce::jlimit(40.0f, 320.0f, newBpm);
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

                if (channel == kBassChannel && (off + durSamps) > (numSamples - 1))
                {
                    // Note extends beyond this block — fire any previously held note-off
                    // then defer this one so the bass synth sustains properly.
                    if (libBassAbsNoteOffSample >= 0 && libBassLastMidiNote != outNote)
                        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, libBassLastMidiNote),
                                      sampleOffsetBase + off);
                    libBassAbsNoteOffSample = sampleCounter + static_cast<int64_t>(off)
                                             + static_cast<int64_t>(durSamps);
                    libBassLastMidiNote = outNote;
                }
                else
                {
                    const int noteOffOffset = juce::jmin(numSamples - 1, off + durSamps);
                    midi.addEvent(juce::MidiMessage::noteOff(channel, outNote), sampleOffsetBase + noteOffOffset);
                }

                t += patternLenBeats;
            }
        }
    };

    emitForList(pattern.drumEvents, kDrumChannel);
    if (generativeBassActive)
    {
        if (genBassHasSteps)
            emitGenerativeBassSteps(midi, numSamples, beatStart, beatEnd, sampleOffsetBase);
        else
            emitGenerativeBassForWindow(midi, numSamples, beatStart, beatEnd, sampleOffsetBase);
    }
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

    const int64_t blockStart = sampleCounter;
    const int64_t blockEnd = blockStart + static_cast<int64_t>(numSamples);

    if (genBassAbsNoteOffSample >= 0 && genBassAbsNoteOffSample <= blockStart)
        genBassAbsNoteOffSample = -1;

    const bool noteHeld = genBassAbsNoteOffSample > blockStart;

    if (noteHeld)
    {
        if (genBassAbsNoteOffSample > blockEnd)
            return;

        const int off = static_cast<int>(genBassAbsNoteOffSample - blockStart);
        const int clamped = juce::jlimit(0, numSamples - 1, off);
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, genBassLastMidiNote),
                      sampleOffsetBase + clamped);
        genBassAbsNoteOffSample = -1;
        return;
    }

    const double rel = 0.0;
    int off = static_cast<int>(std::round(rel * (sampleRate * 60.0 / static_cast<double>(bpm))));
    off += humanSamples();
    off = juce::jlimit(0, numSamples - 1, off);

    const int vel = humanVel(100);
    // Y_bass root is absolute (see docs/BASS_ONNX_IO.md); do not add bassSemitoneOffset — pitch is in X_bass.
    const int outNote = juce::jlimit(0, 127, generativeBassRootMidi);
    genBassLastMidiNote = outNote;

    midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, outNote, static_cast<float>(vel) / 127.0f),
                  sampleOffsetBase + off);

    const int64_t noteOnSample = blockStart + static_cast<int64_t>(off);
    const int durSamps = juce::jmax(
        1,
        static_cast<int>(std::round(static_cast<double>(generativeBassDurationBeats)
                                    * (60.0 / static_cast<double>(bpm)) * sampleRate)));
    const int64_t noteOffSample = noteOnSample + static_cast<int64_t>(durSamps);

    if (noteOffSample <= blockEnd)
    {
        const int noteOffOff = static_cast<int>(noteOffSample - blockStart);
        const int clampedOff = juce::jlimit(0, numSamples - 1, noteOffOff);
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, outNote), sampleOffsetBase + clampedOff);
        genBassAbsNoteOffSample = -1;
    }
    else
    {
        genBassAbsNoteOffSample = noteOffSample;
    }
}

void PatternPlayer::insertGenStepsDefer(juce::MidiBuffer& midi,
                                        int numSamples,
                                        int sampleOffsetBase,
                                        int64_t absOff,
                                        int midiNote) noexcept
{
    while (genStepsDeferCount >= kMaxGenStepsDeferredOffs)
    {
        const int chop = juce::jmax(0, numSamples - 1);
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, genStepsDeferMidiNote[0]),
                        sampleOffsetBase + chop);
        for (int i = 1; i < genStepsDeferCount; ++i)
        {
            genStepsDeferAbsSample[static_cast<size_t>(i - 1)] =
                genStepsDeferAbsSample[static_cast<size_t>(i)];
            genStepsDeferMidiNote[static_cast<size_t>(i - 1)] = genStepsDeferMidiNote[static_cast<size_t>(i)];
        }
        --genStepsDeferCount;
    }

    int insertIdx = 0;
    while (insertIdx < genStepsDeferCount
           && genStepsDeferAbsSample[static_cast<size_t>(insertIdx)] < absOff)
        ++insertIdx;

    for (int j = genStepsDeferCount; j > insertIdx; --j)
    {
        genStepsDeferAbsSample[static_cast<size_t>(j)] = genStepsDeferAbsSample[static_cast<size_t>(j - 1)];
        genStepsDeferMidiNote[static_cast<size_t>(j)] = genStepsDeferMidiNote[static_cast<size_t>(j - 1)];
    }
    genStepsDeferAbsSample[static_cast<size_t>(insertIdx)] = absOff;
    genStepsDeferMidiNote[static_cast<size_t>(insertIdx)] = midiNote;
    ++genStepsDeferCount;
}

void PatternPlayer::emitGenerativeBassSteps(juce::MidiBuffer& midi,
                                            int numSamples,
                                            double beatStart,
                                            double beatEnd,
                                            int sampleOffsetBase)
{
    if (beatEnd <= beatStart + 1.0e-9)
        return;

    const int64_t blockStart = sampleCounter;
    const int64_t blockEnd = blockStart + static_cast<int64_t>(numSamples);

    while (genStepsDeferCount > 0 && genStepsDeferAbsSample[0] < blockStart)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, genStepsDeferMidiNote[0]), sampleOffsetBase);
        for (int i = 1; i < genStepsDeferCount; ++i)
        {
            genStepsDeferAbsSample[static_cast<size_t>(i - 1)] =
                genStepsDeferAbsSample[static_cast<size_t>(i)];
            genStepsDeferMidiNote[static_cast<size_t>(i - 1)] = genStepsDeferMidiNote[static_cast<size_t>(i)];
        }
        --genStepsDeferCount;
    }

    while (genStepsDeferCount > 0 && genStepsDeferAbsSample[0] < blockEnd)
    {
        const int off = static_cast<int>(genStepsDeferAbsSample[0] - blockStart);
        const int clamped = juce::jlimit(0, numSamples - 1, off);
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, genStepsDeferMidiNote[0]),
                      sampleOffsetBase + clamped);
        for (int i = 1; i < genStepsDeferCount; ++i)
        {
            genStepsDeferAbsSample[static_cast<size_t>(i - 1)] =
                genStepsDeferAbsSample[static_cast<size_t>(i)];
            genStepsDeferMidiNote[static_cast<size_t>(i - 1)] = genStepsDeferMidiNote[static_cast<size_t>(i)];
        }
        --genStepsDeferCount;
    }

    const double samplesPerBeat = (60.0 / static_cast<double>(bpm)) * sampleRate;
    constexpr double stepBeats = 0.25;
    constexpr double barBeats = 4.0;

    for (int step = 0; step < 16; ++step)
    {
        const float vel = genBassVelocity[step];
        if (vel <= 0.0f)
            continue;

        double stepBeatPos = static_cast<double>(step) * stepBeats;
        while (stepBeatPos + 1.0e-9 < beatStart)
            stepBeatPos += barBeats;

        while (stepBeatPos < beatEnd - 1.0e-9)
        {
            const double rel = stepBeatPos - beatStart;
            int off = static_cast<int>(std::round(rel * samplesPerBeat));
            off = juce::jlimit(0, numSamples - 1, off);

            const float offset = genBassPitchOffset[step];
            const int midiNote = juce::jlimit(
                0,
                127,
                static_cast<int>(std::floor(genBassStepRootMidi + offset)));
            const int noteVel = juce::jlimit(1, 127, static_cast<int>(vel));

            genBassLastMidiNote = midiNote;
            midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, midiNote,
                             static_cast<float>(noteVel) / 127.0f),
                          sampleOffsetBase + off);

            const int durSamps = juce::jmax(
                1,
                static_cast<int>(std::round(stepBeats * (60.0 / static_cast<double>(bpm)) * sampleRate)));
            const int64_t noteOnSampleAbs = blockStart + static_cast<int64_t>(off);
            const int64_t noteOffSampleAbs = noteOnSampleAbs + static_cast<int64_t>(durSamps);

            if (noteOffSampleAbs < blockEnd)
            {
                const int offRel = static_cast<int>(noteOffSampleAbs - blockStart);
                const int clampedOff = juce::jlimit(0, numSamples - 1, offRel);
                midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, midiNote),
                              sampleOffsetBase + clampedOff);
            }
            else
            {
                insertGenStepsDefer(midi, numSamples, sampleOffsetBase, noteOffSampleAbs, midiNote);
            }

            stepBeatPos += barBeats;
        }
    }
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
        genBassAbsNoteOffSample = -1;
        genStepsDeferCount = 0;
        libBassAbsNoteOffSample = -1;
        beatPosition += static_cast<double>(numSamples) * bpm / (60.0 * sampleRate);
        sampleCounter += static_cast<int64_t>(numSamples);
        return;
    }

    wasSilent = false;

    if (fireTransitionCrash)
    {
        midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, 49, 0.9f), 0);
        fireTransitionCrash = false;
    }

    if (!generativeBassActive && genBassAbsNoteOffSample > sampleCounter)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, genBassLastMidiNote), 0);
        genBassAbsNoteOffSample = -1;
        genStepsDeferCount = 0;
    }

    if (!generativeBassActive && libBassAbsNoteOffSample >= 0)
    {
        const int64_t blockEnd = sampleCounter + static_cast<int64_t>(numSamples);
        if (libBassAbsNoteOffSample <= sampleCounter)
        {
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, libBassLastMidiNote), 0);
            libBassAbsNoteOffSample = -1;
        }
        else if (libBassAbsNoteOffSample < blockEnd)
        {
            const int off = static_cast<int>(libBassAbsNoteOffSample - sampleCounter);
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, libBassLastMidiNote),
                          juce::jlimit(0, numSamples - 1, off));
            libBassAbsNoteOffSample = -1;
        }
    }

    if (pendingPatternIndex >= 0)
    {
        const double barPhase = std::fmod(beatPosition, 4.0);
        const bool atBar = barPhase < 1.0e-4 || (4.0 - barPhase) < 1.0e-3;
        if (atBar)
        {
            const int prevIndex = activePatternIndex;
            activePatternIndex = pendingPatternIndex;
            pendingPatternIndex = -1;
            if (prevIndex != 0 && activePatternIndex != 0)
                fireTransitionCrash = true;
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
