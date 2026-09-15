#include "BassVoice.h"

#include <algorithm>
#include <cmath>
#include <limits>

void BassVoice::reset() noexcept
{
    bassNoteOffMidi_ = 40;
    bassNoteOffSample_ = -1;
    bassNoteHeld_ = false;
    guitarAudible_ = false;
    gridGateSample_ = -1;
    for (auto& p : pending_)
        p = {};
    recent_ = {};
    recentWrite_ = 0;
    recentCount_ = 0;
    lastProducer_ = Producer::None;
    counts_.fill(0);
    learnedRequests_ = 0;
    gridEmits_ = 0;
}

void BassVoice::resetCounters() noexcept
{
    counts_.fill(0);
    recentWrite_ = 0;
    recentCount_ = 0;
    lastProducer_ = Producer::None;
    learnedRequests_ = 0;
    gridEmits_ = 0;
}

void BassVoice::requestLearned(int midiNote, float velocity, int sampleOffset,
                               int durationSamples, bool hold, Producer source) noexcept
{
    PendingNote note;
    note.active = true;
    int n = midiNote;
    while (n < 28) n += 12;
    while (n > 55) n -= 12;
    note.midi = juce::jlimit(28, 55, n);
    note.vel = juce::jlimit(0.0f, 1.0f, velocity);
    note.offset = sampleOffset;
    note.duration = juce::jmax(100, durationSamples);
    note.hold = hold;
    note.source = source;
    for (auto& slot : pending_)
    {
        if (!slot.active)
        {
            slot = note;
            ++learnedRequests_;   // legacy diagnostic (see getLearnedCount)
            return;
        }
    }
    // The queue is full — drop the *newest* note so already-scheduled hits keep
    // their slots. Overwriting back() used to silence a queued note.
    DBG("BassVoice: pending learned queue full; dropping newest note");
}

void BassVoice::emitPickup(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart,
                           int note, int vel, int off, int durSamps) noexcept
{
    emit(midi, numSamples, blockStart, note, vel, off, durSamps, 0, /*forceRetrigger=*/true,
         /*hold=*/false, Producer::Pickup);
}

bool BassVoice::emitGrid(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart,
                         std::int64_t absSample, int note, int vel, int off, int durSamps,
                         int sampleOffsetBase, bool forceRetrigger, Producer source) noexcept
{
    // The mirror/frozen voice is the bass part while it rings; the grid only
    // fills the gaps. This is the 1.0.3 ownership contract in one place.
    if (gridGateSample_ >= 0 && absSample < gridGateSample_)
        return false;

    ++gridEmits_;   // legacy diagnostic (see getGridCount)
    emit(midi, numSamples, blockStart, note, vel, off, durSamps, sampleOffsetBase,
         forceRetrigger, /*hold=*/false, source);
    return true;
}

void BassVoice::emit(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart,
                     int outNote, int vel, int off, int durSamps, int sampleOffsetBase,
                     bool forceRetrigger, bool hold, Producer source) noexcept
{
    // Monophonic bass: close any previously scheduled note before the new one.
    // The deferred note-off carries the correct note number, so alternating or
    // interval bass lines never leave a note stuck on.
    if (bassNoteOffSample_ >= 0)
    {
        const std::int64_t newOnAbs = blockStart + static_cast<std::int64_t>(off);
        // Close at the earlier of the scheduled end and the new onset, so a
        // ringing mirror is cut at the grid hit and a note that already ended in
        // this block is not held past its gate.
        const std::int64_t closeAbs = (forceRetrigger && bassNoteOffSample_ > newOnAbs)
            ? newOnAbs
            : juce::jmin(bassNoteOffSample_, newOnAbs);
        const int closeAt = juce::jlimit(0, numSamples - 1,
                                         static_cast<int>(closeAbs - blockStart));
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi_),
                      sampleOffsetBase + closeAt);
        bassNoteOffSample_ = -1;
        bassNoteHeld_ = false;
    }

    midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, outNote,
                                            static_cast<float>(vel) / 127.0f),
                  sampleOffsetBase + off);
    record(source, outNote, blockStart + static_cast<std::int64_t>(off), hold);

    if (hold)
    {
        // Sustain: no scheduled note-off. Released when the guitarist stops (or
        // closed by the next attack / a flush).
        bassNoteOffMidi_ = outNote;
        bassNoteOffSample_ = std::numeric_limits<std::int64_t>::max();
        bassNoteHeld_ = true;
        heldNoteLevel_ = juce::jmax(inputLevel_, 1.0e-4f);
        return;
    }

    const std::int64_t noteOffAbs = blockStart + static_cast<std::int64_t>(off)
                                  + static_cast<std::int64_t>(durSamps);
    if (noteOffAbs < blockStart + numSamples)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, outNote),
                      sampleOffsetBase + juce::jlimit(0, numSamples - 1,
                          static_cast<int>(noteOffAbs - blockStart)));
    }
    else
    {
        bassNoteOffMidi_ = outNote;
        bassNoteOffSample_ = noteOffAbs;
    }
}

void BassVoice::flushLearned(juce::MidiBuffer& midi, int numSamples, std::int64_t blockStart) noexcept
{
    PendingNote notes[kMaxPending];
    int n = 0;
    for (auto& p : pending_)
    {
        if (!p.active)
            continue;
        notes[n++] = p;
        p.active = false;
    }
    std::sort(notes, notes + n, [](const PendingNote& a, const PendingNote& b) {
        return a.offset < b.offset;
    });
    for (int i = 0; i < n; ++i)
    {
        const int off = juce::jlimit(0, numSamples - 1, notes[i].offset);
        const int vel = juce::jlimit(1, 127, static_cast<int>(std::lround(notes[i].vel * 127.0f)));
        const int durSamps = juce::jmax(1, notes[i].duration);
        const bool hold = notes[i].hold && guitarAudible_;
        emit(midi, numSamples, blockStart, notes[i].midi, vel, off, durSamps, 0,
             /*forceRetrigger=*/true, hold, notes[i].source);
        // The learned/mirrored note owns the monophonic bass voice until it
        // ends; a held note owns it until the guitar stops.
        gridGateSample_ = hold
            ? std::numeric_limits<std::int64_t>::max()
            : juce::jmax(gridGateSample_,
                         blockStart + static_cast<std::int64_t>(off)
                           + static_cast<std::int64_t>(durSamps));
    }
}

void BassVoice::releaseHeldIfStopped(juce::MidiBuffer& midi, std::int64_t blockStart) noexcept
{
    if (!bassNoteHeld_)
        return;

    // Release when the guitarist actually stops, OR when the note's level has
    // decayed to the trough (the note ended). Without the decay test a held
    // mirror note rings through the rest and consecutive sustains blur into one
    // another - the 'muddy on long sustains' report.
    const bool stopped = !guitarAudible_;
    const bool decayed = (heldNoteLevel_ > 0.0f)
                      && (inputLevel_ < heldNoteLevel_ * kDecayRelease);
    if (!stopped && !decayed)
        return;

    if (bassNoteOffSample_ >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi_), 0);
        bassNoteOffSample_ = -1;
    }
    bassNoteHeld_ = false;
    heldNoteLevel_ = 0.0f;
    gridGateSample_ = blockStart;
}

void BassVoice::closeNaturalNoteOff(juce::MidiBuffer& midi, int numSamples,
                                    std::int64_t blockStart) noexcept
{
    if (bassNoteOffSample_ >= 0 && bassNoteOffSample_ < blockStart + numSamples)
    {
        const int off = juce::jlimit(0, numSamples - 1,
                                     static_cast<int>(bassNoteOffSample_ - blockStart));
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi_), off);
        bassNoteOffSample_ = -1;
    }
}

void BassVoice::clearPending() noexcept
{
    for (auto& p : pending_)
        p = {};
}

void BassVoice::flushAll(juce::MidiBuffer& midi, int sampleOffset) noexcept
{
    const int off = juce::jmax(0, sampleOffset);
    if (bassNoteOffSample_ >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi_), off);
        bassNoteOffSample_ = -1;
    }
    // A seek/silence drops the mirror's claim on the bass voice, so the grid
    // fallback is not left muted against a stale timeline position.
    gridGateSample_ = -1;
    bassNoteHeld_ = false;
}

int BassVoice::getRecentNoteOns(NoteOn* out, int maxCount) const noexcept
{
    if (out == nullptr || maxCount <= 0)
        return 0;
    const int n = juce::jmin(recentCount_, maxCount);
    for (int i = 0; i < n; ++i)
    {
        const int idx = (recentWrite_ - 1 - i + kMaxProvenance) % kMaxProvenance;
        out[i] = recent_[static_cast<std::size_t>(idx)];
    }
    return n;
}

int BassVoice::getProducerCount(Producer source) const noexcept
{
    const int idx = static_cast<int>(source);
    return (idx >= 0 && idx < kProducerCount) ? counts_[static_cast<std::size_t>(idx)] : 0;
}

void BassVoice::record(Producer source, int midiNote, std::int64_t sample, bool held) noexcept
{
    NoteOn n;
    n.producer = source;
    n.midi = midiNote;
    n.sample = sample;
    n.held = held;
    recent_[static_cast<std::size_t>(recentWrite_)] = n;
    recentWrite_ = (recentWrite_ + 1) % kMaxProvenance;
    if (recentCount_ < kMaxProvenance)
        ++recentCount_;
    lastProducer_ = source;
    const int idx = static_cast<int>(source);
    if (idx >= 0 && idx < kProducerCount)
        ++counts_[static_cast<std::size_t>(idx)];
}
