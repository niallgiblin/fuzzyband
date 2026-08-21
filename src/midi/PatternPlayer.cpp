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
    bassNoteOffMidi = 40;
    bassNoteOffSample = -1;
    crashNoteOffSample = -1;
    clickNoteOffSample = -1;
    clickNoteOffNote = kClickStickNote;
    clickTrack_ = false;
    wasClickTrack_ = false;
    armCrashPending = false;
    phraseLearnerActive_ = false;
    pendingLearnedNote_ = false;
    sampleCounter = 0;
    expectedHostSample = 0;
    lastHostSample = -1;

    // Musicality pivot state (Workstream A / B1)
    swing = 0.0f;
    sectionId = Groove::SongSectionId::Verse;
    setGenrePreset(0);  // Rock default
}

void PatternPlayer::setSwing(float newSwing) noexcept
{
    swing = juce::jlimit(0.0f, 1.0f, newSwing);
}

void PatternPlayer::setGenrePreset(int presetId) noexcept
{
    preset = Groove::presetFor(presetId);
    grooveTemplate = Groove::templateFor(preset.templateId);
    ghostDensity = preset.ghostDensity;
    // The swing knob is user-owned; preset.defaultSwing is applied by the
    // editor when the user changes genre, so we do not override it here.
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
    // beat grid away from the DAW transport. B2: every clamp site agrees on
    // [40, 300] (matches the APVTS bpm parameter and PatternRules::adjustedBpm).
    bpm = juce::jlimit(40.0f, 300.0f, newBpm);
}

void PatternPlayer::setPatternIndex(int index)
{
    patternIndex.store(index, std::memory_order_relaxed);
}

void PatternPlayer::setStructureSilent(bool silent)
{
    structureSilent = silent;
}

int64_t PatternPlayer::previewResolvedHostSample(int64_t hostSamplePosition, int numSamples) const noexcept
{
    if (hostSamplePosition == lastHostSample)
        return sampleCounter + static_cast<int64_t>(numSamples);
    return hostSamplePosition;
}

void PatternPlayer::emitClickTrack(juce::MidiBuffer& midi,
                                   int numSamples,
                                   double beatStart,
                                   double beatEnd,
                                   int64_t hostSamplePosition) noexcept
{
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const int durSamps = juce::jmax(1, static_cast<int>(std::round(0.12 * samplesPerBeat)));

    if (clickNoteOffSample >= 0 && clickNoteOffSample < hostSamplePosition + numSamples)
    {
        const int off = juce::jlimit(0, numSamples - 1,
                                     static_cast<int>(clickNoteOffSample - hostSamplePosition));
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, clickNoteOffNote), off);
        clickNoteOffSample = -1;
    }

    for (double t = std::ceil(beatStart - 1.0e-12); t < beatEnd - 1.0e-12; t += 1.0)
    {
        int beatInBar = static_cast<int>(std::floor(t + 1.0e-9)) % 4;
        if (beatInBar < 0)
            beatInBar += 4;

        const int note = (beatInBar == 0) ? kClickKickNote : kClickStickNote;
        const juce::uint8 vel = (beatInBar == 0) ? static_cast<juce::uint8>(110)
                                                 : static_cast<juce::uint8>(85);
        const int onOff = juce::jlimit(0, numSamples - 1,
            static_cast<int>(std::round((t - beatStart) * samplesPerBeat)));

        if (clickNoteOffSample >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, clickNoteOffNote), onOff);
            clickNoteOffSample = -1;
        }

        midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, note, vel), onOff);
        clickNoteOffNote = note;
        clickNoteOffSample = hostSamplePosition + onOff + durSamps;
    }
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
    bpm = juce::jlimit(40.0f, 300.0f, newBpm);
}

float PatternPlayer::boundedGaussian(juce::Random& r, float mean, float sigma) noexcept
{
    if (sigma <= 0.0f)
        return mean;
    float u1 = r.nextFloat();
    float u2 = r.nextFloat();
    if (u1 <= 0.0f)
        u1 = 1.0e-6f;
    // Box–Muller; clamp to ±2.5 sigma so no event strays far from the grid.
    const float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * juce::MathConstants<float>::pi * u2);
    return mean + juce::jlimit(-2.5f, 2.5f, z) * sigma;
}

bool PatternPlayer::sectionAllowsGhosts() const noexcept
{
    return sectionId == Groove::SongSectionId::Verse
        || sectionId == Groove::SongSectionId::Breakdown;
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
    const double samplesPerMs = sampleRate / 1000.0;
    // Off-8th swing delay: swing=1 moves the "and" from 50% to 2/3 of the beat.
    const double swingDelayMs = static_cast<double>(swing) * (1.0 / 6.0) * 60000.0 / juce::jmax(1.0f, bpm);

    // Occupancy map for ghost-note placement (A3.2): cells with authored snares.
    bool occupied[16] = {};
    if (ghostDensity > 0.01f && sectionAllowsGhosts())
        for (const auto& ev : pattern.drumEvents)
            if (ev.note == 38)
                occupied[Groove::grid16Of(ev.beatOffset)] = true;

    for (const auto& ev : pattern.drumEvents)
    {
        // Phase of this event within the looping pattern.
        const double phase = std::fmod(static_cast<double>(ev.beatOffset), patternLenBeats);
        // First occurrence at or after beatStart, aligned to the pattern grid.
        const double startPhase = std::fmod(beatStart, patternLenBeats);
        const double first = beatStart + std::fmod(phase - startPhase + patternLenBeats, patternLenBeats);

        // 16th grid cell within the bar — drives the velocity/timing hierarchy.
        const int grid16 = Groove::grid16Of(ev.beatOffset);
        const bool isGhost = (ev.velocity <= grooveTemplate.ghostThreshold);

        for (double t = first; t < beatEnd - 1.0e-9; t += patternLenBeats)
        {
            const double rel = t - beatStart;

            // ── Structured microtiming + swing + bounded gaussian (A2.2/A2.3) ──
            float timeMs = grooveTemplate.timingMs[grid16];
            if (swing > 0.0f && (grid16 % 4) == 2)
                timeMs += static_cast<float>(swingDelayMs);
            if (isGhost)
                timeMs += grooveTemplate.ghostTimingMs;
            timeMs += boundedGaussian(rng, 0.0f, grooveTemplate.timingJitterMs);

            int off = static_cast<int>(std::round(rel * samplesPerBeat + timeMs * samplesPerMs));
            off = juce::jlimit(0, numSamples - 1, off);

            // ── Velocity hierarchy: grid accent × section contrast × genre scale (A2.1/A3.1) ──
            float mul = grooveTemplate.velocityMul[grid16] * sectionVelMul;
            int vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * mul
                                                  + boundedGaussian(rng, 0.0f, grooveTemplate.velocityJitter)));
            if (isGhost)
                vel = juce::jlimit(static_cast<int>(grooveTemplate.ghostVelocityLo),
                                   static_cast<int>(grooveTemplate.ghostVelocityHi), vel);
            vel = juce::jlimit(1, 127, vel);

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

    // ── Ghost notes (A3.2) ─────────────────────────────────────────────────
    if (ghostDensity > 0.01f && sectionAllowsGhosts())
        emitGhostNotes(midi, numSamples, beatStart, beatEnd, occupied, sampleOffsetBase);
}

void PatternPlayer::emitGhostNotes(juce::MidiBuffer& midi,
                                   int numSamples,
                                   double beatStart,
                                   double beatEnd,
                                   const bool occupied[16],
                                   int sampleOffsetBase)
{
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const int durSamps = juce::jmax(1, static_cast<int>(std::round(0.25 * samplesPerBeat)));

    // Candidate ghost cells (off-16ths). All avoid landing on the same sample
    // as an authored snare note-off, so no same-note overlap is possible.
    const int lowCells[1] = { 9 };                        // "e" of 3 — the classic
    const int highCells[4] = { 1, 9, 11, 15 };            // e of 1, e of 3, a of 3, a of 4
    const int* cells = (ghostDensity >= 0.7f) ? highCells : lowCells;
    const int numCells = (ghostDensity >= 0.7f) ? 4 : 1;

    for (double barStart = std::floor(beatStart / 4.0) * 4.0; barStart < beatEnd - 1.0e-9; barStart += 4.0)
    {
        for (int c = 0; c < numCells; ++c)
        {
            const int cell = cells[c];
            if (occupied[cell])
                continue;
            const double beat = barStart + static_cast<double>(cell) / 4.0;
            if (beat < beatStart - 1.0e-9 || beat >= beatEnd - 1.0e-9)
                continue;

            const double rel = beat - beatStart;
            float timeMs = grooveTemplate.ghostTimingMs
                         + boundedGaussian(rng, 0.0f, grooveTemplate.timingJitterMs);
            int off = static_cast<int>(std::round(rel * samplesPerBeat + timeMs * samplesPerMs));
            off = juce::jlimit(0, numSamples - 1, off);

            const float ghostLo = grooveTemplate.ghostVelocityLo;
            const float ghostHi = grooveTemplate.ghostVelocityHi;
            const int vel = juce::jlimit(1, 127,
                static_cast<int>(std::round(ghostLo + rng.nextFloat() * (ghostHi - ghostLo))));

            midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, 38, static_cast<float>(vel) / 127.0f),
                          sampleOffsetBase + off);
            midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, 38),
                          sampleOffsetBase + juce::jmin(numSamples - 1, off + durSamps));
        }
    }
}

void PatternPlayer::emitBassRange(juce::MidiBuffer& midi,
                                  int numSamples,
                                  double beatStart,
                                  double beatEnd,
                                  const MidiPattern& pattern,
                                  int sampleOffsetBase)
{
    if (beatEnd <= beatStart + 1.0e-9 || numSamples <= 0)
        return;

    // A1.1: authored bass lines (dead code until now) play when present;
    // otherwise the harmonic engine (A1.2) builds one from the guitarist's root.
    if (!pattern.bassEvents.empty())
        emitPatternBass(midi, numSamples, beatStart, beatEnd, pattern, sampleOffsetBase);
    else
        emitHarmonicBass(midi, numSamples, beatStart, beatEnd, sampleOffsetBase);
}

void PatternPlayer::emitBassNote(juce::MidiBuffer& midi,
                                 int numSamples,
                                 int64_t blockStart,
                                 int outNote,
                                 int vel,
                                 int off,
                                 int durSamps,
                                 int sampleOffsetBase)
{
    // Monophonic bass: close any previously scheduled note before the new one.
    // The deferred note-off carries the correct note number (bassNoteOffMidi),
    // so alternating/interval bass lines never leave a note stuck on.
    if (bassNoteOffSample >= 0)
    {
        const int prevOff = (bassNoteOffSample < blockStart + numSamples)
            ? juce::jlimit(0, numSamples - 1, static_cast<int>(bassNoteOffSample - blockStart))
            : 0;
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi), sampleOffsetBase + prevOff);
        bassNoteOffSample = -1;
    }

    midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, outNote, static_cast<float>(vel) / 127.0f),
                  sampleOffsetBase + off);
    bassLastMidiNote = outNote;

    const int64_t noteOffAbs = blockStart + static_cast<int64_t>(off) + static_cast<int64_t>(durSamps);
    if (noteOffAbs < blockStart + numSamples)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, outNote),
                      sampleOffsetBase + juce::jlimit(0, numSamples - 1,
                          static_cast<int>(noteOffAbs - blockStart)));
    }
    else
    {
        bassNoteOffMidi = outNote;
        bassNoteOffSample = noteOffAbs;
    }
}

void PatternPlayer::emitPatternBass(juce::MidiBuffer& midi,
                                    int numSamples,
                                    double beatStart,
                                    double beatEnd,
                                    const MidiPattern& pattern,
                                    int sampleOffsetBase)
{
    const double patternLenBeats = static_cast<double>(pattern.lengthInBars) * 4.0;
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const int64_t blockStart = sampleCounter;

    for (const auto& ev : pattern.bassEvents)
    {
        const double phase = std::fmod(static_cast<double>(ev.beatOffset), patternLenBeats);
        const double startPhase = std::fmod(beatStart, patternLenBeats);
        const double first = beatStart + std::fmod(phase - startPhase + patternLenBeats, patternLenBeats);

        const int grid16 = Groove::grid16Of(ev.beatOffset);

        for (double t = first; t < beatEnd - 1.0e-9; t += patternLenBeats)
        {
            const double rel = t - beatStart;

            // Bass sits slightly behind the kick for pocket (A1.2), with small jitter.
            float timeMs = grooveTemplate.bassPocketMs
                         + boundedGaussian(rng, 0.0f, grooveTemplate.timingJitterMs);
            int off = static_cast<int>(std::round(rel * samplesPerBeat + timeMs * samplesPerMs));
            off = juce::jlimit(0, numSamples - 1, off);

            // Transpose the authored interval pattern to the live root, folding
            // back into the playable bass register.
            const int interval = static_cast<int>(ev.note) - kPatternBassRoot;
            int outNote = bassRootMidi + bassSemitoneOffset + interval;
            while (outNote < 28) outNote += 12;
            while (outNote > 55) outNote -= 12;
            outNote = juce::jlimit(0, 127, outNote);

            // Accent beat 1, soften beat 3, passing notes quieter; humanise ±5.
            float mul = sectionVelMul;
            if (grid16 == 0)      mul *= 1.08f;   // downbeat
            else if (grid16 == 8) mul *= 0.96f;   // beat 3
            int vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * mul
                                                  + boundedGaussian(rng, 0.0f, 4.0f)));
            vel = juce::jlimit(1, 127, vel);

            const int durSamps = juce::jmax(1, static_cast<int>(std::round(
                static_cast<double>(ev.durationBeats) * kBassGate * samplesPerBeat)));

            emitBassNote(midi, numSamples, blockStart, outNote, vel, off, durSamps, sampleOffsetBase);
        }
    }
}

void PatternPlayer::emitHarmonicBass(juce::MidiBuffer& midi,
                                     int numSamples,
                                     double beatStart,
                                     double beatEnd,
                                     int sampleOffsetBase)
{
    if (beatEnd <= beatStart + 1.0e-9 || bassNotesPerBar <= 0)
        return;

    const int64_t blockStart = sampleCounter;
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const double beatsPerNote = 4.0 / static_cast<double>(bassNotesPerBar);

    // Find the first beat in this window that should trigger a bass note.
    double firstBeat = std::ceil(beatStart / beatsPerNote) * beatsPerNote;
    const int bar = static_cast<int>(std::floor(beatStart / 4.0));

    for (double beat = firstBeat; beat < beatEnd - 1.0e-9; beat += beatsPerNote)
    {
        const double rel = beat - beatStart;
        const int beatInBar = static_cast<int>(std::floor(std::fmod(beat, 4.0)));
        const int degree = harmonyDegree(beatInBar, bar);

        int outNote = bassRootMidi + bassSemitoneOffset + degree;
        while (outNote < 28) outNote += 12;
        while (outNote > 55) outNote -= 12;
        outNote = juce::jlimit(0, 127, outNote);

        // Accent beat 1, soften beat 3; humanise ±5.
        float mul = sectionVelMul;
        if (beatInBar == 0)      mul *= 1.08f;
        else if (beatInBar == 2) mul *= 0.96f;
        int vel = static_cast<int>(std::round(95.0f * mul + boundedGaussian(rng, 0.0f, 4.0f)));
        vel = juce::jlimit(1, 127, vel);

        // Slightly behind the kick for pocket.
        float timeMs = grooveTemplate.bassPocketMs
                     + boundedGaussian(rng, 0.0f, grooveTemplate.timingJitterMs);
        int off = static_cast<int>(std::round(rel * samplesPerBeat + timeMs * samplesPerMs));
        off = juce::jlimit(0, numSamples - 1, off);

        // 85% gate (kept from the old engine, now a named parameter).
        const double noteDuration = beatsPerNote * kBassGate;
        const int durSamps = juce::jmax(1, static_cast<int>(std::round(noteDuration * samplesPerBeat)));

        emitBassNote(midi, numSamples, blockStart, outNote, vel, off, durSamps, sampleOffsetBase);
    }
}

int PatternPlayer::harmonyDegree(int beatInBar, int bar) const noexcept
{
    switch (sectionId)
    {
        case Groove::SongSectionId::Chorus:
        case Groove::SongSectionId::Solo:
            // Root → fifth → octave → fourth walk (A1.2: chorus walks).
            { constexpr int kDeg[4] = { 0, 7, 12, 5 }; return kDeg[beatInBar & 3]; }
        case Groove::SongSectionId::Verse:
            // Hold root; occasional fourth on beat 3 of every 4th bar.
            if (beatInBar == 2 && (bar & 3) == 3)
                return 5;
            return 0;
        case Groove::SongSectionId::Breakdown:
        case Groove::SongSectionId::Intro:
        case Groove::SongSectionId::Outro:
        case Groove::SongSectionId::Unknown:
            return 0;
        default:
            return (beatInBar == 2) ? 7 : 0;  // Unknown: root/fifth alternating
    }
}

void PatternPlayer::process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition)
{
    if (library == nullptr || numSamples <= 0)
        return;

    // ── Transport frozen? (DAW stopped / no moving playhead) ────────────────
    // A stopped transport keeps getTimeInSamples() constant. Treating that as a
    // jump every block wiped pending pattern changes (drums stuck on Silent)
    // and anchored the beat clock to a fixed phase (bass/drums machine-gunning
    // at block rate — the "harsh constant" sound). Instead, run the plugin's
    // own beat clock when the host position is frozen, so jamming works with
    // the transport stopped. Real seeks/loops still register as jumps.
    const bool transportFrozen = (hostSamplePosition == lastHostSample);
    lastHostSample = hostSamplePosition;
    if (transportFrozen)
        hostSamplePosition = sampleCounter + static_cast<int64_t>(numSamples);

    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double beatStart = static_cast<double>(hostSamplePosition) / samplesPerBeat;
    const double beatEnd = beatStart + static_cast<double>(numSamples) / samplesPerBeat;

    // Section velocity multiplier for this block (A3.1).
    sectionVelMul = preset.sectionVelocityMultiplier(sectionId) * preset.velocityScale;

    // Propagate a pattern index change requested via setPatternIndex().
    const int requested = patternIndex.load(std::memory_order_relaxed);
    if (requested != activePatternIndex && pendingPatternIndex < 0)
        pendingPatternIndex = requested;

    // Transport jump detection (seek / loop / re-instantiation) — drop any
    // deferred state that was scheduled relative to a previous timeline position.
    if (!transportFrozen && hostSamplePosition != expectedHostSample)
    {
        pendingPatternIndex = -1;
        pendingGrooveCommitValid = false;
        pendingGrooveCommit = GrooveCommit{};
        bassNoteOffSample = -1;
        crashNoteOffSample = -1;
        pendingLearnedNote_ = false;
        armCrashPending = false;
        clickNoteOffSample = -1;
    }
    expectedHostSample = hostSamplePosition + static_cast<int64_t>(numSamples);
    sampleCounter = hostSamplePosition;

    // Silence: cut all notes and clear deferred state.
    if (structureSilent && !clickTrack_)
    {
        if (!wasSilent)
            for (int ch = 1; ch <= 16; ++ch)
                midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        wasSilent = true;
        bassNoteOffSample = -1;
        crashNoteOffSample = -1;
        clickNoteOffSample = -1;
        wasClickTrack_ = false;
        return;
    }

    if (wasClickTrack_ && !clickTrack_)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, kClickKickNote), 0);
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, kClickStickNote), 0);
        clickNoteOffSample = -1;
    }
    wasClickTrack_ = clickTrack_;

    wasSilent = false;

    if (clickTrack_)
    {
        emitClickTrack(midi, numSamples, beatStart, beatEnd, hostSamplePosition);
        return;
    }

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
    const int prevPatternIndex = activePatternIndex;  // pre-change index for the split bass

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

    // ── Bass note-off bookkeeping (A1) ──────────────────────────────────────
    // Emit any deferred note-off that has come due this block. Runs regardless
    // of the phrase-learner state so a note scheduled before a mode switch is
    // still closed; carries the correct note number (monophonic bass).
    if (bassNoteOffSample >= 0 && bassNoteOffSample < sampleCounter + numSamples)
    {
        const int off = juce::jlimit(0, numSamples - 1,
                                     static_cast<int>(bassNoteOffSample - sampleCounter));
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi), off);
        bassNoteOffSample = -1;
    }

    // Learned bass note (PhraseLearner active).
    if (pendingLearnedNote_)
    {
        if (bassNoteOffSample >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi), 0);
            bassNoteOffSample = -1;
        }

        const int off = juce::jlimit(0, numSamples - 1, pendingLearnedOffset_);
        midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, pendingLearnedMidi_, pendingLearnedVel_), off);
        bassLastMidiNote = pendingLearnedMidi_;
        bassNoteOffMidi = pendingLearnedMidi_;

        bassNoteOffSample = sampleCounter + static_cast<int64_t>(off) + static_cast<int64_t>(pendingLearnedDuration_);
        pendingLearnedNote_ = false;
    }

    // Bass engine (A1): only when PhraseLearner is NOT mirroring the riff, and
    // never during the Silent pattern (index 0) — silence means silence for the
    // bass too. Previously the harmonic fallback kept droning the last root
    // note whenever the state dropped to SILENT (hum-level audio keeps the
    // plugin "active" so structureSilent never engages).
    if (!phraseLearnerActive_)
    {
        const MidiPattern& bassPattern = library->getPattern(activePatternIndex);
        if (changeBeat < 0.0)
        {
            if (activePatternIndex != 0)
                emitBassRange(midi, numSamples, beatStart, beatEnd, bassPattern, 0);
        }
        else
        {
            // Old pattern up to the boundary, new pattern after it.
            if (prevPatternIndex != 0)
                emitBassRange(midi, numSamples, beatStart, changeBeat, pattern, 0);
            if (activePatternIndex != 0)
                emitBassRange(midi, numSamples, changeBeat, beatEnd, bassPattern, 0);
        }
    }
}
