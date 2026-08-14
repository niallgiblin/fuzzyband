#include "PatternPlayer.h"
#include <cmath>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<PatternPlayer::GrooveCommit>);

void PatternPlayer::prepare(double newSampleRate, int blockSize)
{
    (void)blockSize;
    sampleRate = newSampleRate;
    rng.setSeedRandomly();
    reset();
}

void PatternPlayer::reset()
{
    activePatternIndex = patternIndex.load(std::memory_order_relaxed);
    pendingPatternIndex = -1;
    pendingGrooveCommitValid = false;
    pendingGrooveCommit = GrooveCommit{};
    wasSilent = false;
    bassSemitoneOffset = 0;
    bassRootMidi = 40;  // E2
    bassNotesPerBar = 2;
    bassLastMidiNote = 40;
    bassNoteOffSample = -1;
    crashNoteOffSample = -1;
    armCrashPending = false;
    phraseLearnerActive_ = false;
    pendingLearnedNote_ = false;
    sampleCounter = 0;
    expectedHostSample = 0;
}

void PatternPlayer::setBassSemitoneOffset(int semitones)
{
    bassSemitoneOffset = juce::jlimit(-24, 24, semitones);
}

void PatternPlayer::setBassParams(int rootMidi, int notesPerBar) noexcept
{
    bassRootMidi = juce::jlimit(28, 55, rootMidi);  // E1 (28) to G3 (55)
    bassNotesPerBar = juce::jlimit(1, 8, notesPerBar);
}

void PatternPlayer::triggerLearnedBassNote(int midiNote, float velocity, int sampleOffset, int durationSamples) noexcept
{
    pendingLearnedNote_ = true;
    pendingLearnedMidi_ = juce::jlimit(28, 55, midiNote);
    pendingLearnedVel_ = juce::jlimit(0.0f, 1.0f, velocity);
    pendingLearnedOffset_ = sampleOffset;
    pendingLearnedDuration_ = juce::jmax(100, durationSamples);
}

void PatternPlayer::queueGrooveCommit(const GrooveCommit& commit) noexcept
{
    pendingGrooveCommit = commit;
    pendingGrooveCommitValid = true;
    pendingPatternIndex = -1;
    patternIndex.store(commit.patternIndex, std::memory_order_relaxed);
}

void PatternPlayer::clearPendingGrooveCommit() noexcept
{
    pendingGrooveCommitValid = false;
    pendingGrooveCommit = GrooveCommit{};
}

void PatternPlayer::setBpm(float newBpm)
{
    // Host-authoritative: snap directly. EMA smoothing would drift the internal
    // beat grid away from the DAW transport.
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

void PatternPlayer::snapToBarStart()
{
    // The beat grid is anchored to the host transport (see process()). Nothing to
    // snap here; only drop deferred changes so a stale commit does not fire on
    // gate re-open.
    pendingPatternIndex = -1;
    pendingGrooveCommitValid = false;
    pendingGrooveCommit = GrooveCommit{};
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

void PatternPlayer::emitCrashHit(juce::MidiBuffer& midi,
                                 int numSamples,
                                 int64_t hostSamplePosition,
                                 int sampleOffset) noexcept
{
    if (numSamples <= 0)
        return;

    const int off = juce::jlimit(0, numSamples - 1, sampleOffset);
    midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, kCrashNote, 0.9f), off);

    // Let the crash ring ~1 beat, then note-off (deferred across blocks if needed).
    const int dur = static_cast<int>((60.0 / juce::jmax(1.0f, bpm)) * sampleRate);
    const int64_t noteOffAbs = hostSamplePosition + static_cast<int64_t>(off) + static_cast<int64_t>(dur);

    if (noteOffAbs < hostSamplePosition + numSamples)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, kCrashNote),
                      static_cast<int>(noteOffAbs - hostSamplePosition));
    }
    else
    {
        crashNoteOffSample = noteOffAbs;
    }
}

void PatternPlayer::emitTransitionFill(juce::MidiBuffer& midi,
                                       int numSamples,
                                       TransitionFillKind kind,
                                       int sampleOffsetBase) noexcept
{
    if (numSamples <= 0 || kind == TransitionFillKind::None)
        return;

    const auto clampOffset = [numSamples, sampleOffsetBase](int offset) noexcept {
        return juce::jlimit(0, numSamples - 1, sampleOffsetBase + offset);
    };
    const auto addDrum = [&](int note, int velocity, int offset) noexcept {
        midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel,
                                                note,
                                                static_cast<float>(juce::jlimit(1, 127, velocity)) / 127.0f),
                      clampOffset(offset));
    };

    switch (kind)
    {
        case TransitionFillKind::None:
            break;
        case TransitionFillKind::Entry:
            addDrum(49, 116, 0);
            addDrum(36, 118, 0);
            break;
        case TransitionFillKind::BuildUp:
            addDrum(38, 108, 0);
            addDrum(45, 112, numSamples / 2);
            break;
        case TransitionFillKind::Release:
            addDrum(38, 82, 0);
            addDrum(42, 70, numSamples / 2);
            break;
        case TransitionFillKind::BreakdownOrImpact:
            addDrum(36, 122, 0);
            addDrum(49, 118, numSamples / 4);
            addDrum(43, 112, numSamples / 2);
            break;
    }
}

void PatternPlayer::emitDrumEventsForRange(juce::MidiBuffer& midi,
                                           int numSamples,
                                           double beatStart,
                                           double beatEnd,
                                           const MidiPattern& pattern,
                                           int sampleOffsetBase)
{
    if (pattern.lengthInBars <= 0.0f || numSamples <= 0)
        return;

    const double patternLenBeats = static_cast<double>(pattern.lengthInBars) * 4.0;
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;

    for (const auto& ev : pattern.drumEvents)
    {
        // Phase of this event within the looping pattern.
        const double phase = std::fmod(static_cast<double>(ev.beatOffset), patternLenBeats);
        // First occurrence at or after beatStart, aligned to the pattern grid.
        const double startPhase = std::fmod(beatStart, patternLenBeats);
        const double first = beatStart + std::fmod(phase - startPhase + patternLenBeats, patternLenBeats);

        for (double t = first; t < beatEnd - 1.0e-9; t += patternLenBeats)
        {
            const double rel = t - beatStart;
            int off = static_cast<int>(std::round(rel * samplesPerBeat));
            off += humanSamples();
            off = juce::jlimit(0, numSamples - 1, off);

            const int vel = humanVel(static_cast<int>(ev.velocity));
            const int outNote = juce::jlimit(0, 127, static_cast<int>(ev.note));

            midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, outNote, static_cast<float>(vel) / 127.0f),
                          sampleOffsetBase + off);

            const int durSamps = juce::jmax(
                1,
                static_cast<int>(std::round(static_cast<double>(ev.durationBeats) * samplesPerBeat)));
            const int noteOffOffset = juce::jmin(numSamples - 1, off + durSamps);
            midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, outNote), sampleOffsetBase + noteOffOffset);
        }
    }
}

void PatternPlayer::emitBeatAlignedBass(juce::MidiBuffer& midi,
                                        int numSamples,
                                        double beatStart,
                                        double beatEnd,
                                        int sampleOffsetBase)
{
    if (beatEnd <= beatStart + 1.0e-9 || bassNotesPerBar <= 0)
        return;

    const int64_t blockStart = sampleCounter;
    const int64_t blockEnd = blockStart + static_cast<int64_t>(numSamples);
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;

    // Handle any pending note-off from previous block
    if (bassNoteOffSample >= 0 && bassNoteOffSample < blockEnd)
    {
        if (bassNoteOffSample <= blockStart)
        {
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassLastMidiNote), sampleOffsetBase);
        }
        else
        {
            const int off = static_cast<int>(bassNoteOffSample - blockStart);
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassLastMidiNote),
                          sampleOffsetBase + juce::jlimit(0, numSamples - 1, off));
        }
        bassNoteOffSample = -1;
    }

    // Calculate beat interval for bass notes
    const double beatsPerNote = 4.0 / static_cast<double>(bassNotesPerBar);

    // Find the first beat in this window that should trigger a bass note
    double firstBeat = std::ceil(beatStart / beatsPerNote) * beatsPerNote;

    for (double beat = firstBeat; beat < beatEnd - 1.0e-9; beat += beatsPerNote)
    {
        // Calculate sample offset for this beat — NO humanization for tight lock to drums
        const double rel = beat - beatStart;
        int off = static_cast<int>(std::round(rel * samplesPerBeat));
        off = juce::jlimit(0, numSamples - 1, off);

        const int outNote = juce::jlimit(0, 127, bassRootMidi + bassSemitoneOffset);
        const int vel = 100;  // Fixed velocity for consistency

        // Note-on
        midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, outNote, static_cast<float>(vel) / 127.0f),
                      sampleOffsetBase + off);
        bassLastMidiNote = outNote;

        // Schedule note-off at 85% of beat interval (slight staccato feel)
        const double noteDuration = beatsPerNote * 0.85;
        const int durSamps = static_cast<int>(std::round(noteDuration * samplesPerBeat));
        const int64_t noteOffSampleAbs = blockStart + static_cast<int64_t>(off) + static_cast<int64_t>(durSamps);

        if (noteOffSampleAbs < blockEnd)
        {
            const int noteOffOff = static_cast<int>(noteOffSampleAbs - blockStart);
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, outNote),
                          sampleOffsetBase + juce::jlimit(0, numSamples - 1, noteOffOff));
        }
        else
        {
            // Defer note-off to next block
            bassNoteOffSample = noteOffSampleAbs;
        }
    }
}

void PatternPlayer::process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition)
{
    if (library == nullptr || numSamples <= 0)
        return;

    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double beatStart = static_cast<double>(hostSamplePosition) / samplesPerBeat;
    const double beatEnd = beatStart + static_cast<double>(numSamples) / samplesPerBeat;

    // Propagate a pattern index change requested via setPatternIndex().
    const int requested = patternIndex.load(std::memory_order_relaxed);
    if (requested != activePatternIndex && pendingPatternIndex < 0)
        pendingPatternIndex = requested;

    // Transport jump detection (seek / loop / re-instantiation) — drop any
    // deferred state that was scheduled relative to a previous timeline position.
    if (hostSamplePosition != expectedHostSample)
    {
        pendingPatternIndex = -1;
        pendingGrooveCommitValid = false;
        pendingGrooveCommit = GrooveCommit{};
        bassNoteOffSample = -1;
        crashNoteOffSample = -1;
        pendingLearnedNote_ = false;
        armCrashPending = false;
    }
    expectedHostSample = hostSamplePosition + static_cast<int64_t>(numSamples);
    sampleCounter = hostSamplePosition;

    // Silence: cut all notes and clear deferred state.
    if (structureSilent)
    {
        if (!wasSilent)
            for (int ch = 1; ch <= 16; ++ch)
                midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        wasSilent = true;
        bassNoteOffSample = -1;
        crashNoteOffSample = -1;
        return;
    }

    wasSilent = false;

    // Deferred crash note-off from a previous block.
    if (crashNoteOffSample >= 0 && crashNoteOffSample < hostSamplePosition + numSamples)
    {
        const int off = juce::jlimit(0, numSamples - 1,
                                     static_cast<int>(crashNoteOffSample - hostSamplePosition));
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, kCrashNote), off);
        crashNoteOffSample = -1;
    }

    // Crash armed by PlaybackGate (phrase-breath re-entry) — hit at block start.
    if (armCrashPending)
    {
        armCrashPending = false;
        emitCrashHit(midi, numSamples, hostSamplePosition, 0);
    }

    // Resolve a pending pattern change at the first bar boundary in this block.
    constexpr double beatsPerBar = 4.0;
    double changeBeat = -1.0;
    if (pendingGrooveCommitValid || pendingPatternIndex >= 0)
    {
        const double boundary = std::ceil(beatStart / beatsPerBar - 1.0e-9) * beatsPerBar;
        if (boundary < beatEnd - 1.0e-9)
            changeBeat = boundary;
    }

    const MidiPattern& pattern = library->getPattern(activePatternIndex);

    if (changeBeat < 0.0)
    {
        // No change this block — play the active pattern across the whole block.
        if (activePatternIndex != 0)
            emitDrumEventsForRange(midi, numSamples, beatStart, beatEnd, pattern, 0);
    }
    else
    {
        const int changeOffset = static_cast<int>(std::round((changeBeat - beatStart) * samplesPerBeat));
        const int clampedOffset = juce::jlimit(0, numSamples - 1, changeOffset);

        // 1) Old pattern up to the boundary.
        if (activePatternIndex != 0)
            emitDrumEventsForRange(midi, numSamples, beatStart, changeBeat, pattern, 0);

        // 2) Apply the change.
        const int prevIndex = activePatternIndex;
        if (pendingGrooveCommitValid)
        {
            activePatternIndex = pendingGrooveCommit.patternIndex;
            emitTransitionFill(midi, numSamples, pendingGrooveCommit.fillKind, clampedOffset);
            pendingGrooveCommitValid = false;
            pendingGrooveCommit = GrooveCommit{};
        }
        else
        {
            activePatternIndex = pendingPatternIndex;
        }
        pendingPatternIndex = -1;

        // 3) Transition crash between two non-silent patterns.
        if (prevIndex != 0 && activePatternIndex != 0)
            emitCrashHit(midi, numSamples, hostSamplePosition, clampedOffset);

        // 4) New pattern from the boundary onward.
        if (activePatternIndex != 0)
            emitDrumEventsForRange(midi, numSamples, changeBeat, beatEnd,
                                   library->getPattern(activePatternIndex), 0);
    }

    // Learned bass note (PhraseLearner active).
    if (pendingLearnedNote_)
    {
        if (bassNoteOffSample >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassLastMidiNote), 0);
            bassNoteOffSample = -1;
        }

        const int off = juce::jlimit(0, numSamples - 1, pendingLearnedOffset_);
        midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, pendingLearnedMidi_, pendingLearnedVel_), off);
        bassLastMidiNote = pendingLearnedMidi_;

        bassNoteOffSample = sampleCounter + static_cast<int64_t>(off) + static_cast<int64_t>(pendingLearnedDuration_);
        pendingLearnedNote_ = false;
    }

    // Beat-aligned bass: only when PhraseLearner is NOT active.
    if (!phraseLearnerActive_)
        emitBeatAlignedBass(midi, numSamples, beatStart, beatEnd, 0);
}
