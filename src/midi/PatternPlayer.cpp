#include "PatternPlayer.h"
#include <cmath>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<PatternPlayer::GrooveCommit>);

namespace
{
// ── Drum note constants (mirror MidiPatternLibrary.cpp) ───────────────────────
constexpr int kKick      = 36;
constexpr int kSnare     = 38;
constexpr int kHatClosed = 42;
constexpr int kHatOpen   = 46;
constexpr int kRide      = 51;
constexpr int kRideBell  = 53;
constexpr int kTomHi     = 48;
constexpr int kTomMid    = 45;

// ── Ornamentation probabilities (per-bar, percent). Deterministic per bar. ──
constexpr int kOpenHatPctChorus    = 18;  // chorus/solo loosen up
constexpr int kOpenHatPctElse      = 8;
constexpr int kRideSwitchPctSolo   = 28;  // solo rides the cymbal
constexpr int kRideSwitchPctChorus = 12;
constexpr int kExtraGhostPct       = 15;  // verse/breakdown only
constexpr int kDropKickPctBreak    = 12;  // breakdown/outro leave space
constexpr int kMicroFillPct        = 12;  // phrase-end bars only

// Distinct salts so each ornament decision is an independent draw.
constexpr unsigned kSaltOpenHat      = 0x11u;
constexpr unsigned kSaltOpenHatCell  = 0x12u;
constexpr unsigned kSaltRide         = 0x21u;
constexpr unsigned kSaltGhost        = 0x31u;
constexpr unsigned kSaltGhostCell    = 0x32u;
constexpr unsigned kSaltKickDrop     = 0x41u;
constexpr unsigned kSaltKickDropCell = 0x42u;
constexpr unsigned kSaltMicroFill    = 0x51u;

// SplitMix32-style avalanche (same as PatternRules::hashMix) — kept local so
// PatternPlayer does not depend on the inference headers.
inline unsigned barHash(unsigned a, unsigned b) noexcept
{
    unsigned h = a * 0x9E3779B1u + b;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

// Deterministic per-bar probability gate: returns true `pct`% of bars.
inline bool barChance(int64_t barNumber, unsigned salt, int pct) noexcept
{
    return (barHash(static_cast<unsigned>(barNumber), salt) % 100u)
         < static_cast<unsigned>(pct);
}
} // namespace

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
    bassLeadInArmed = false;
    phraseLearnerActive_ = false;
    pendingLearnedNote_ = false;
    sampleCounter = 0;
    expectedHostSample = 0;
    lastHostSample = -1;

    // Musicality pivot state (Workstream A / B1)
    swing = 0.0f;
    sectionId = Groove::SongSectionId::Verse;
    guitarEnergy = 1.0f;
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

PatternPlayer::BarOrnamentation PatternPlayer::computeOrnamentation(int64_t barNumber, int patternIndex) const noexcept
{
    BarOrnamentation o{};
    if (library == nullptr || patternIndex == 0)
        return o;

    const MidiPattern& p = library->getPattern(patternIndex);

    // Voice usage + grid cells the pattern actually uses (drives which ornaments
    // are even possible — an ornament never fires on a voice the pattern lacks).
    bool hasClosedHat = false;
    bool hasRide = false;
    int closedHatCells[16]; int closedHatCount = 0;
    int kickCells[16];      int kickCount = 0;
    bool snareOccupied[16] = {};
    for (const auto& ev : p.drumEvents)
    {
        const int cell = Groove::grid16Of(ev.beatOffset);
        if (ev.note == kHatClosed)
        {
            hasClosedHat = true;
            if (closedHatCount < 16) closedHatCells[closedHatCount++] = cell;
        }
        else if (ev.note == kRide || ev.note == kRideBell)
        {
            hasRide = true;
        }
        else if (ev.note == kKick)
        {
            if (kickCount < 16) kickCells[kickCount++] = cell;
        }
        else if (ev.note == kSnare)
        {
            snareOccupied[cell] = true;
        }
    }

    const bool chorusLike = (sectionId == Groove::SongSectionId::Chorus
                          || sectionId == Groove::SongSectionId::Solo);

    // 1) Open one closed-hat cell (chorus/solo loosen up; elsewhere subtle).
    if (hasClosedHat && closedHatCount > 0)
    {
        const int pct = chorusLike ? kOpenHatPctChorus : kOpenHatPctElse;
        if (barChance(barNumber, kSaltOpenHat, pct))
        {
            o.openHat = true;
            o.openHatCell = closedHatCells[static_cast<int>(
                barHash(static_cast<unsigned>(barNumber), kSaltOpenHatCell) % static_cast<unsigned>(closedHatCount))];
        }
    }

    // 2) Ride switch: hats -> ride/bell for the whole bar (solo/chorus only).
    if (chorusLike && hasClosedHat && !hasRide)
    {
        const int pct = (sectionId == Groove::SongSectionId::Solo)
            ? kRideSwitchPctSolo : kRideSwitchPctChorus;
        if (barChance(barNumber, kSaltRide, pct))
            o.rideSwitch = true;
    }

    // 3) Extra ghost snare on an unoccupied off-16th (verse/breakdown).
    if (sectionAllowsGhosts())
    {
        if (barChance(barNumber, kSaltGhost, kExtraGhostPct))
        {
            const int ghostCells[4] = { 1, 9, 11, 15 };
            const int start = static_cast<int>(
                barHash(static_cast<unsigned>(barNumber), kSaltGhostCell) % 4u);
            for (int i = 0; i < 4; ++i)
            {
                const int cell = ghostCells[(start + i) % 4];
                if (!snareOccupied[cell])
                {
                    o.extraGhost = true;
                    o.extraGhostCell = cell;
                    break;
                }
            }
        }
    }

    // 4) Drop a non-essential kick (breakdown/outro leave space).
    if ((sectionId == Groove::SongSectionId::Breakdown || sectionId == Groove::SongSectionId::Outro)
        && kickCount > 0)
    {
        if (barChance(barNumber, kSaltKickDrop, kDropKickPctBreak))
        {
            int candidates[16]; int n = 0;
            for (int i = 0; i < kickCount; ++i)
                if (kickCells[i] != 0 && kickCells[i] != 8 && n < 16)  // never downbeat/beat-3
                    candidates[n++] = kickCells[i];
            if (n > 0)
            {
                o.dropKick = true;
                o.dropKickCell = candidates[static_cast<int>(
                    barHash(static_cast<unsigned>(barNumber), kSaltKickDropCell) % static_cast<unsigned>(n))];
            }
        }
    }

    // 5) Micro-fill at the end of a 4-bar phrase (not on a fill pattern).
    if ((barNumber % 4) == 3 && patternIndex != 17 && patternIndex != 18 && patternIndex != 19)
    {
        if (barChance(barNumber, kSaltMicroFill, kMicroFillPct))
            o.microFill = true;
    }

    return o;
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

void PatternPlayer::emitMicroFill(juce::MidiBuffer& midi,
                                  int numSamples,
                                  double beatStart,
                                  double beatEnd,
                                  int sampleOffsetBase) noexcept
{
    // Tier-0 micro-fill: a two-note tom pickup on the last 16ths of a phrase-end
    // bar, leading into the next bar's downbeat (whose kick/crash is the
    // pattern's own). Bounded, allocation-free.
    if (numSamples <= 0)
        return;

    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double barStart = std::floor(beatStart / 4.0) * 4.0;

    const auto addTom = [&](int note, int vel, double beat) noexcept {
        if (beat < beatStart - 1.0e-9 || beat >= beatEnd - 1.0e-9)
            return;
        const int off = juce::jlimit(0, numSamples - 1,
            static_cast<int>(std::round((beat - beatStart) * samplesPerBeat)));
        const int durSamps = juce::jmax(1, static_cast<int>(std::round(0.125 * samplesPerBeat)));
        midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, note, static_cast<float>(vel) / 127.0f),
                      sampleOffsetBase + off);
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, note),
                      sampleOffsetBase + juce::jmin(numSamples - 1, off + durSamps));
    };

    addTom(kTomHi, 105, barStart + 3.75);    // "a" of beat 4
    addTom(kTomMid, 108, barStart + 3.875);  // the very last 16th before the downbeat
}

void PatternPlayer::emitDrumEventsForRange(juce::MidiBuffer& midi,
                                           int numSamples,
                                           double beatStart,
                                           double beatEnd,
                                           const MidiPattern& pattern,
                                           const BarOrnamentation& orn,
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

        // Tier-1 groove grid: when a rendered grid matches the active pattern,
        // use its learned per-step velocity/offset instead of the fixed template.
        const int voice = GrooveGridUtil::voiceForNote(ev.note);
        const bool useGrid = grooveGrid.valid && grooveGrid.patternIndex == activePatternIndex && voice >= 0;
        const float gridVel = useGrid ? grooveGrid.velocity[static_cast<size_t>(voice)][static_cast<size_t>(grid16)] : 0.0f;
        const float gridOff = useGrid ? grooveGrid.offset[static_cast<size_t>(voice)][static_cast<size_t>(grid16)] : 0.0f;

        // Tier-0: omit a non-essential kick to leave space (breakdown/outro).
        if (orn.dropKick && ev.note == kKick && grid16 == orn.dropKickCell)
            continue;

        for (double t = first; t < beatEnd - 1.0e-9; t += patternLenBeats)
        {
            const double rel = t - beatStart;

            // ── Microtiming: learned grid (Tier-1) or template + swing + jitter (A2.2/A2.3) ──
            float timeMs;
            if (useGrid)
            {
                // grid offset is a fraction of a 16th note -> scale to ms at the live BPM.
                const double msPer16th = (60.0 / juce::jmax(1.0f, bpm)) * 1000.0 / 4.0;
                timeMs = gridOff * static_cast<float>(msPer16th);
            }
            else
            {
                timeMs = grooveTemplate.timingMs[grid16];
                if (swing > 0.0f && (grid16 % 4) == 2)
                    timeMs += static_cast<float>(swingDelayMs);
                if (isGhost)
                    timeMs += grooveTemplate.ghostTimingMs;
                timeMs += boundedGaussian(rng, 0.0f, grooveTemplate.timingJitterMs);
            }

            int off = static_cast<int>(std::round(rel * samplesPerBeat + timeMs * samplesPerMs));
            off = juce::jlimit(0, numSamples - 1, off);

            // ── Velocity: learned grid (Tier-1) or template hierarchy + jitter (A2.1/A3.1) ──
            int vel;
            if (useGrid)
            {
                // Tier-1 v2: grid velocity is a *multiplier* on the authored
                // velocity (~1.0), not an absolute value — the authored pattern's
                // dynamics are preserved and the model humanises around them.
                vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * gridVel * sectionVelMul));
                vel = juce::jlimit(1, 127, vel);
            }
            else
            {
                const float mul = grooveTemplate.velocityMul[grid16] * sectionVelMul;
                vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * mul
                                                  + boundedGaussian(rng, 0.0f, grooveTemplate.velocityJitter)));
                if (isGhost)
                    vel = juce::jlimit(static_cast<int>(grooveTemplate.ghostVelocityLo),
                                       static_cast<int>(grooveTemplate.ghostVelocityHi), vel);
                vel = juce::jlimit(1, 127, vel);
            }

            int outNote = juce::jlimit(0, 127, static_cast<int>(ev.note));
            // Tier-0: open one closed-hat cell, or switch hats -> ride for the bar.
            if (orn.openHat && ev.note == kHatClosed && grid16 == orn.openHatCell)
                outNote = kHatOpen;
            else if (orn.rideSwitch && ev.note == kHatClosed)
                outNote = (grid16 == 0) ? kRideBell : kRide;

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
        emitGhostNotes(midi, numSamples, beatStart, beatEnd, occupied, orn, sampleOffsetBase);
}

void PatternPlayer::emitGhostNotes(juce::MidiBuffer& midi,
                                   int numSamples,
                                   double beatStart,
                                   double beatEnd,
                                   const bool occupied[16],
                                   const BarOrnamentation& orn,
                                   int sampleOffsetBase)
{
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const int durSamps = juce::jmax(1, static_cast<int>(std::round(0.25 * samplesPerBeat)));

    const auto emitOneGhost = [&](double barStart, int cell) noexcept {
        if (occupied[cell])
            return;
        const double beat = barStart + static_cast<double>(cell) / 4.0;
        if (beat < beatStart - 1.0e-9 || beat >= beatEnd - 1.0e-9)
            return;

        const double rel = beat - beatStart;
        const float timeMs = grooveTemplate.ghostTimingMs
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
    };

    // Candidate ghost cells (off-16ths). All avoid landing on the same sample
    // as an authored snare note-off, so no same-note overlap is possible.
    const int lowCells[1] = { 9 };                        // "e" of 3 — the classic
    const int highCells[4] = { 1, 9, 11, 15 };            // e of 1, e of 3, a of 3, a of 4
    const int* cells = (ghostDensity >= 0.7f) ? highCells : lowCells;
    const int numCells = (ghostDensity >= 0.7f) ? 4 : 1;

    for (double barStart = std::floor(beatStart / 4.0) * 4.0; barStart < beatEnd - 1.0e-9; barStart += 4.0)
    {
        for (int c = 0; c < numCells; ++c)
            emitOneGhost(barStart, cells[c]);

        // Tier-0: an extra ornamented ghost snare on an unoccupied off-16th.
        if (orn.extraGhost && orn.extraGhostCell >= 0)
            emitOneGhost(barStart, orn.extraGhostCell);
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
                static_cast<double>(ev.durationBeats) * sectionBassGate() * samplesPerBeat)));

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

        // 85% gate (kept from the old engine, now a named parameter scaled by section).
        const double noteDuration = beatsPerNote * sectionBassGate();
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

float PatternPlayer::sectionBassGate() const noexcept
{
    // Bass note-length follows the section (A1.2): chorus/solo are legato
    // (present, sustained), verse/breakdown are chunkier, intro/outro airy.
    switch (sectionId)
    {
        case Groove::SongSectionId::Chorus:
        case Groove::SongSectionId::Solo:      return 0.95f;
        case Groove::SongSectionId::Breakdown: return 0.80f;
        case Groove::SongSectionId::Intro:
        case Groove::SongSectionId::Outro:     return 0.80f;
        case Groove::SongSectionId::Verse:
        default:                               return kBassGate;    // 0.85
    }
}

int PatternPlayer::snapBassToSectionHarmony(int rawNote) const noexcept
{
    // The guitarist's riff drives the rhythm, but every note resolves to a chord
    // tone of the current section around the tonic (A1.2), so the listening bass
    // sounds like it is playing the song's section rather than free-mirroring.
    // Chorus/solo use the full root-fifth-octave-fourth palette; other sections
    // stay root-centric (with the occasional fourth). The raw note is the tonic
    // pitch-class register (36–47); the live root + transpose are folded in here.
    const int tonicPc = ((bassRootMidi % 12) + 12) % 12;
    const int pc = ((rawNote % 12) + 12) % 12;

    int bestDeg = 0;
    int bestDist = 99;
    for (int deg : { 0, 5, 7 })
    {
        // Per-section chord palette (A1.2): verse = root/fourth, chorus/solo =
        // root/fourth/fifth, breakdown/intro/outro = root only, unknown = root/fifth.
        if (deg == 5 && sectionId != Groove::SongSectionId::Verse
            && sectionId != Groove::SongSectionId::Chorus
            && sectionId != Groove::SongSectionId::Solo)
            continue;
        if (deg == 7 && sectionId != Groove::SongSectionId::Chorus
            && sectionId != Groove::SongSectionId::Solo
            && sectionId != Groove::SongSectionId::Unknown)
            continue;
        const int degPc = (tonicPc + deg) % 12;
        int dist = std::abs(pc - degPc);
        if (dist > 6) dist = 12 - dist;
        if (dist < bestDist) { bestDist = dist; bestDeg = deg; }
    }

    int out = bassRootMidi + bassSemitoneOffset + bestDeg;
    while (out < 28) out += 12;
    while (out > 55) out -= 12;
    return juce::jlimit(0, 127, out);
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

    // Section velocity multiplier for this block (A3.1), scaled by the
    // guitarist-energy dynamic so the kit/bass swell with the guitarist's picking.
    sectionVelMul = preset.sectionVelocityMultiplier(sectionId) * preset.velocityScale * guitarEnergy;

    // Tier-0 ornamentation: deterministic per-bar score-level mutations for the
    // block (subtle + reproducible — see computeOrnamentation). A block that
    // straddles a bar boundary applies the block-start bar's ornaments
    // throughout, which is inaudible for these subtle changes.
    const double samplesPerBar = 4.0 * samplesPerBeat;
    const int64_t barNumber = (samplesPerBar > 0.0)
        ? static_cast<int64_t>(std::floor(static_cast<double>(hostSamplePosition) / samplesPerBar))
        : 0;
    const BarOrnamentation orn = computeOrnamentation(barNumber, activePatternIndex);

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
            emitDrumEventsForRange(midi, numSamples, beatStart, beatEnd, pattern, orn, 0);
    }
    else
    {
        const int changeOffset = static_cast<int>(std::round((changeBeat - beatStart) * samplesPerBeat));
        const int clampedOffset = juce::jlimit(0, numSamples - 1, changeOffset);

        // 1) Old pattern up to the boundary.
        if (activePatternIndex != 0)
            emitDrumEventsForRange(midi, numSamples, beatStart, changeBeat, pattern, orn, 0);

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
                                   library->getPattern(activePatternIndex), orn, 0);
    }

    // Tier-0 micro-fill: a two-note tom pickup into the next downbeat at the end
    // of a 4-bar phrase (the downbeat's own kick/crash is the pattern's).
    if (orn.microFill && activePatternIndex != 0)
        emitMicroFill(midi, numSamples, beatStart, beatEnd, 0);

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

    // Section hand-off bass pickup (A1.2 lead-in): when armed (set by the
    // processor on a section's last bar), play a short approach note on the "and"
    // of the bar's last beat — a pickup into the new section — then disarm. Only
    // the block that actually crosses the pickup beat fires (the beat is a point,
    // so a re-arm later in the same bar cannot fire again).
    if (bassLeadInArmed)
    {
        const double barStart = std::floor(beatStart / 4.0) * 4.0;
        const double pickupBeat = barStart + 3.5;
        if (pickupBeat >= beatStart - 1.0e-9 && pickupBeat < beatEnd - 1.0e-9)
        {
            const int off = juce::jlimit(0, numSamples - 1,
                static_cast<int>(std::round((pickupBeat - beatStart) * samplesPerBeat)));
            const int durSamps = juce::jmax(
                1, static_cast<int>(std::round(0.5 * samplesPerBeat * sectionBassGate())));
            // Approach the tonic from a fourth below, then fold into the register.
            int pickupNote = bassRootMidi + bassSemitoneOffset - 5;
            while (pickupNote < 28) pickupNote += 12;
            while (pickupNote > 55) pickupNote -= 12;
            emitBassNote(midi, numSamples, sampleCounter, pickupNote, 96, off, durSamps, 0);
            bassLeadInArmed = false;
        }
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

        const int64_t durSamps = static_cast<int64_t>(
            static_cast<double>(pendingLearnedDuration_) * sectionBassGate());
        bassNoteOffSample = sampleCounter + static_cast<int64_t>(off) + durSamps;
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
