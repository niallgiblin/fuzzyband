#include "PatternPlayer.h"
#include <algorithm>
#include <cmath>
#include <limits>
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
constexpr int kRideSwitchPctSolo   = 0;   // T4.3: ride→hat is a pattern change, not an ornament
constexpr int kRideSwitchPctChorus = 0;
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

// T3.4: per-event humanisation salts (independent of the ornament salts above).
constexpr unsigned kSaltDrumTime  = 0xA1u;
constexpr unsigned kSaltDrumVel   = 0xA2u;
constexpr unsigned kSaltGhostTime = 0xA3u;
constexpr unsigned kSaltGhostVel  = 0xA4u;
constexpr unsigned kSaltBassTime  = 0xA5u;
constexpr unsigned kSaltBassVel   = 0xA6u;

double fillGrooveCutBeat(int fillIndex, double fillOrigin) noexcept
{
    if (fillIndex == 17) return fillOrigin + 3.0;
    if (fillIndex == 18) return fillOrigin + 2.0;
    return fillOrigin;
}

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
    beatGridBassEnabled_ = true;
    beatGridBassPrev_ = false;
    guitarAudible_ = false;
    bassNoteHeld_ = false;
    mirrorVoiceEndSample_ = -1;
    learnedBassNotes_ = 0;
    gridBassNotes_ = 0;
    pendingBarFillIndex_ = -1;
    barFillStartBeat_ = -1.0;
    clearDrumNoteOffTable();
    grooveGrid = {};
    for (auto& n : pendingLearned_)
        n = {};
    sampleCounter = 0;
    expectedHostSample = 0;
    lastHostSample = -1;
    lastTransportFrozen = true;
    transportJumped_ = false;

    // Musicality pivot state (Workstream A / B1)
    swing = 0.0f;
    humanizeAmount = 1.0f;
    sectionId = Groove::SongSectionId::Verse;
    guitarEnergy = 1.0f;
    setGenrePreset(0);  // Rock default
}

void PatternPlayer::setSwing(float newSwing) noexcept
{
    swing = juce::jlimit(0.0f, 1.0f, newSwing);
}

void PatternPlayer::setHumanize(float amount) noexcept
{
    humanizeAmount = juce::jlimit(0.0f, 1.0f, amount);
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

void PatternPlayer::triggerLearnedBassNote(int midiNote, float velocity, int sampleOffset, int durationSamples, bool hold) noexcept
{
    PendingLearnedNote note;
    note.active = true;
    int n = midiNote;
    while (n < 28) n += 12;
    while (n > 55) n -= 12;
    note.midi = juce::jlimit(28, 55, n);
    note.vel = juce::jlimit(0.0f, 1.0f, velocity);
    note.offset = sampleOffset;
    note.duration = juce::jmax(100, durationSamples);
    note.hold = hold;
    for (auto& slot : pendingLearned_)
    {
        if (!slot.active)
        {
            slot = note;
            ++learnedBassNotes_;   // diagnostics (see getLearnedBassNoteCount)
            return;
        }
    }
    // T8.2: the queue is full — drop the *newest* note so already-scheduled
    // hits keep their slots. Overwriting back() used to silence a queued note.
    DBG("PatternPlayer: pending learned queue full; dropping newest note");
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

void PatternPlayer::armBarFill(int fillPatternIndex, bool fromNextBar) noexcept
{
    if (fillPatternIndex < 17 || fillPatternIndex > 19)
        return;
    pendingBarFillIndex_ = fillPatternIndex;
    barFillStartBeat_ = fromNextBar ? -2.0 : -1.0;
}

void PatternPlayer::armBarFillAtBeat(int fillPatternIndex, double originBeat) noexcept
{
    if (fillPatternIndex < 17 || fillPatternIndex > 19)
        return;
    pendingBarFillIndex_ = fillPatternIndex;
    double snapped = std::round(originBeat / 4.0) * 4.0;
    if (snapped < 0.0)
        snapped = 0.0;
    barFillStartBeat_ = snapped;
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

int64_t PatternPlayer::previewResolvedHostSample(int64_t hostSamplePosition, int numSamples,
                                                 bool hostRolling) const noexcept
{
    const bool positionFrozen = (hostSamplePosition == lastHostSample);
    const bool transportFrozen = positionFrozen && !hostRolling;
    if (transportFrozen)
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

float PatternPlayer::boundedGaussian(float u1, float u2, float mean, float sigma) noexcept
{
    if (sigma <= 0.0f)
        return mean;
    if (u1 < 1.0e-6f)
        u1 = 1.0e-6f;
    // Box–Muller; clamp to ±2.5 sigma so no event strays far from the grid.
    const float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * juce::MathConstants<float>::pi * u2);
    return mean + juce::jlimit(-2.5f, 2.5f, z) * sigma;
}

void PatternPlayer::microtimingSlackBeats(double samplesPerBeat, double samplesPerMs,
                                          double swingDelayMs,
                                          double& earlyBeats, double& lateBeats) const noexcept
{
    float minTimingMs = 0.0f;
    float maxTimingMs = 0.0f;
    for (int c = 0; c < 16; ++c)
    {
        minTimingMs = juce::jmin(minTimingMs, grooveTemplate.timingMs[c]);
        maxTimingMs = juce::jmax(maxTimingMs, grooveTemplate.timingMs[c]);
    }
    minTimingMs = juce::jmin(minTimingMs, grooveTemplate.ghostTimingMs)
                - 2.5f * grooveTemplate.timingJitterMs;
    maxTimingMs = juce::jmax(maxTimingMs, 0.0f)
                + 2.5f * grooveTemplate.timingJitterMs
                + static_cast<float>(swingDelayMs);
    const double perBeat = (samplesPerBeat > 0.0) ? samplesPerBeat : 1.0;
    // +0.125 beats covers a Tier-1 groove-grid offset of up to half a 16th.
    earlyBeats = (static_cast<double>(juce::jmax(0.0f, -minTimingMs)) * samplesPerMs) / perBeat + 0.125;
    lateBeats  = (static_cast<double>(juce::jmax(0.0f, maxTimingMs)) * samplesPerMs) / perBeat + 0.125;
}

int PatternPlayer::placeEvent(int64_t absSample, int numSamples, int64_t slackSamples,
                              bool clampEarly) const noexcept
{
    if (clampEarly && absSample < sampleCounter && absSample >= sampleCounter - slackSamples)
        return 0;   // first block of a phase: nothing earlier emitted this event
    if (absSample < 0)
    {
        // Microtiming pushed the event before the render timeline. Only the first
        // block can present it, and only when it is within the slack window.
        if (sampleCounter != 0 || absSample < -slackSamples)
            return -1;
        absSample = 0;
    }
    const int64_t off = absSample - sampleCounter;
    if (off < 0 || off >= static_cast<int64_t>(numSamples))
        return -1;
    return static_cast<int>(off);
}

float PatternPlayer::eventGaussian(int64_t barNumber, int grid16, int voice,
                                   unsigned salt, float sigma) const noexcept
{
    if (sigma <= 0.0f)
        return 0.0f;
    const unsigned key = static_cast<unsigned>(barNumber)
                       ^ (static_cast<unsigned>(grid16) * 0x9E3779B1u)
                       ^ (static_cast<unsigned>(voice) * 0x85EBCA6Bu);
    const unsigned h1 = barHash(key ^ humanizeSeed_, salt);
    const unsigned h2 = barHash(key ^ humanizeSeed_, salt ^ 0x9E3779B9u);
    const float u1 = static_cast<float>(h1 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    const float u2 = static_cast<float>(h2 & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    return boundedGaussian(u1, u2, 0.0f, sigma);
}

int PatternPlayer::applyVelocityHeadroom(int vel) noexcept
{
    // T3.1: the product used to peak at ~1.39, pinning authored 92–125 at 127.
    // Trim lives on sectionVelMul; this soft knee catches residual accents
    // *after* ±2.5σ velocity jitter so a hot chorus is not a wall of 127.
    if (vel > 110)
        vel = 110 + static_cast<int>(std::lround(static_cast<double>(vel - 110) * 0.30));
    return juce::jlimit(1, 127, vel);
}

bool PatternPlayer::sectionAllowsGhosts() const noexcept
{
    return sectionId == Groove::SongSectionId::Verse
        || sectionId == Groove::SongSectionId::Breakdown;
}

PatternPlayer::BarOrnamentation PatternPlayer::computeOrnamentation(int64_t barNumber, int patternIndex) const noexcept
{
    BarOrnamentation o{};
    if (library == nullptr || patternIndex == 0 || humanizeAmount <= 0.0f)
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

    auto scaledPct = [this](int pct) noexcept -> int
    {
        return juce::jlimit(0, 100,
            static_cast<int>(std::lround(static_cast<double>(pct) * humanizeAmount)));
    };

    auto cellTaken = [&](int cell) noexcept -> bool
    {
        if (cell < 0) return true;
        if (o.openHat && o.openHatCell == cell) return true;
        if (o.extraGhost && o.extraGhostCell == cell) return true;
        if (o.dropKick && o.dropKickCell == cell) return true;
        return false;
    };

    // Injected ghosts (emitGhostNotes) occupy cell 9 at low density, or the
    // off-16th set at high density — extraGhost must not double them.
    bool injectedGhost[16] = {};
    if (ghostDensity >= 0.7f)
    {
        injectedGhost[1] = injectedGhost[9] = injectedGhost[11] = injectedGhost[15] = true;
    }
    else
    {
        injectedGhost[9] = true;
    }

    // 1) Open one closed-hat cell (chorus/solo loosen up; elsewhere subtle).
    if (hasClosedHat && closedHatCount > 0)
    {
        const int pct = scaledPct(chorusLike ? kOpenHatPctChorus : kOpenHatPctElse);
        if (pct > 0 && barChance(barNumber, kSaltOpenHat, pct))
        {
            const unsigned start = barHash(static_cast<unsigned>(barNumber), kSaltOpenHatCell)
                                 % static_cast<unsigned>(closedHatCount);
            for (int i = 0; i < closedHatCount; ++i)
            {
                const int cell = closedHatCells[(static_cast<int>(start) + i) % closedHatCount];
                if (!cellTaken(cell))
                {
                    o.openHat = true;
                    o.openHatCell = cell;
                    break;
                }
            }
        }
    }

    // 2) Ride switch: hats -> ride/bell for the whole bar (solo/chorus only).
    // Default probabilities are 0 (T4.3) — this is a pattern change, not an ornament.
    if (chorusLike && hasClosedHat && !hasRide)
    {
        const int pct = scaledPct((sectionId == Groove::SongSectionId::Solo)
            ? kRideSwitchPctSolo : kRideSwitchPctChorus);
        if (pct > 0 && barChance(barNumber, kSaltRide, pct))
            o.rideSwitch = true;
    }

    // 3) Extra ghost snare on an unoccupied off-16th the pattern actually uses
    // (verse/breakdown). Skip cells already claimed by another ornament or by
    // the injected-ghost path.
    if (sectionAllowsGhosts())
    {
        const int pct = scaledPct(kExtraGhostPct);
        if (pct > 0 && barChance(barNumber, kSaltGhost, pct))
        {
            const int ghostCells[4] = { 1, 9, 11, 15 };
            const int start = static_cast<int>(
                barHash(static_cast<unsigned>(barNumber), kSaltGhostCell) % 4u);
            for (int i = 0; i < 4; ++i)
            {
                const int cell = ghostCells[(start + i) % 4];
                if (!snareOccupied[cell] && !injectedGhost[cell] && !cellTaken(cell))
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
        const int pct = scaledPct(kDropKickPctBreak);
        if (pct > 0 && barChance(barNumber, kSaltKickDrop, pct))
        {
            int candidates[16]; int n = 0;
            for (int i = 0; i < kickCount; ++i)
                if (kickCells[i] != 0 && kickCells[i] != 8 && !cellTaken(kickCells[i]) && n < 16)
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
        const int pct = scaledPct(kMicroFillPct);
        if (pct > 0 && barChance(barNumber, kSaltMicroFill, pct))
            o.microFill = true;
    }

    return o;
}

void PatternPlayer::clearDrumNoteOffTable() noexcept
{
    drumNoteOffSample.fill(-1);
    crashNoteOffSample = -1;
}

void PatternPlayer::flushAllPendingNoteOffs(juce::MidiBuffer& midi, int sampleOffset) noexcept
{
    const int off = juce::jmax(0, sampleOffset);
    if (bassNoteOffSample >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi), off);
        bassNoteOffSample = -1;
    }
    // A seek/silence drops the mirror's claim on the bass voice, so the grid
    // fallback is not left muted against a stale timeline position.
    mirrorVoiceEndSample_ = -1;
    bassNoteHeld_ = false;
    if (clickNoteOffSample >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, clickNoteOffNote), off);
        clickNoteOffSample = -1;
    }
    for (int n = 0; n < kDrumVoices; ++n)
    {
        if (drumNoteOffSample[static_cast<size_t>(n)] >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, n), off);
            drumNoteOffSample[static_cast<size_t>(n)] = -1;
        }
    }
    crashNoteOffSample = -1;
}

void PatternPlayer::flushDueDrumNoteOffs(juce::MidiBuffer& midi, int numSamples, int64_t blockStart) noexcept
{
    if (numSamples <= 0)
        return;
    const int64_t blockEnd = blockStart + static_cast<int64_t>(numSamples);
    for (int n = 0; n < kDrumVoices; ++n)
    {
        const int64_t abs = drumNoteOffSample[static_cast<size_t>(n)];
        if (abs < 0 || abs >= blockEnd)
            continue;
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, n),
                      juce::jlimit(0, numSamples - 1, static_cast<int>(abs - blockStart)));
        drumNoteOffSample[static_cast<size_t>(n)] = -1;
        if (n == kCrashNote)
            crashNoteOffSample = -1;
    }
}

void PatternPlayer::scheduleDrumNoteOff(juce::MidiBuffer& midi, int numSamples,
                                        int64_t blockStart, int note, int off, int durSamps) noexcept
{
    if (numSamples <= 0 || note < 0 || note >= kDrumVoices)
        return;

    const int onOff = juce::jlimit(0, numSamples - 1, off);
    auto& slot = drumNoteOffSample[static_cast<size_t>(note)];
    const int64_t noteOnAbs = blockStart + static_cast<int64_t>(onOff);
    if (slot >= 0 && slot <= noteOnAbs)
    {
        // The ringing note already ends at or before this retrigger: release it at
        // its true sample. Which of the two wins must not depend on whether they
        // land in the same block (T9.2).
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, note),
                      juce::jlimit(0, numSamples - 1, static_cast<int>(slot - blockStart)));
        slot = -1;
    }
    if (slot >= 0)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, note), onOff);
        slot = -1;
        if (note == kCrashNote)
            crashNoteOffSample = -1;
    }

    const int64_t abs = blockStart + static_cast<int64_t>(onOff) + static_cast<int64_t>(juce::jmax(1, durSamps));
    if (abs < blockStart + numSamples)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, note),
                      juce::jlimit(onOff, numSamples - 1, onOff + juce::jmax(1, durSamps)));
    }
    else
    {
        slot = abs;
        if (note == kCrashNote)
            crashNoteOffSample = abs;
    }
}

bool PatternPlayer::patternCrashesNear(const MidiPattern& pattern, double targetBeat) const noexcept
{
    if (pattern.lengthInBars <= 0.0f)
        return false;
    const double patternLen = static_cast<double>(pattern.lengthInBars) * 4.0;
    const double windowBeats = (0.020 * static_cast<double>(juce::jmax(1.0f, bpm))) / 60.0;
    double wrappedTarget = std::fmod(targetBeat, patternLen);
    if (wrappedTarget < 0.0)
        wrappedTarget += patternLen;
    for (const auto& ev : pattern.drumEvents)
    {
        if (ev.note != kCrashNote)
            continue;
        double phase = std::fmod(static_cast<double>(ev.beatOffset), patternLen);
        if (phase < 0.0)
            phase += patternLen;
        double d = std::abs(phase - wrappedTarget);
        d = std::min(d, patternLen - d);
        if (d <= windowBeats)
            return true;
    }
    return false;
}

void PatternPlayer::emitCrashHit(juce::MidiBuffer& midi,
                                 int numSamples,
                                 int64_t hostSamplePosition,
                                 int sampleOffset) noexcept
{
    if (numSamples <= 0)
        return;

    const int off = juce::jlimit(0, numSamples - 1, sampleOffset);
    // Match authored crashes (2.5 beats). scheduleDrumNoteOff before the note-on
    // so a same-sample retrigger close sorts first (T1.1 / T1.6).
    const int dur = juce::jmax(1, static_cast<int>(std::lround(
        2.5 * (60.0 / static_cast<double>(juce::jmax(1.0f, bpm))) * sampleRate)));
    scheduleDrumNoteOff(midi, numSamples, hostSamplePosition, kCrashNote, off, dur);
    midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, kCrashNote, 0.9f), off);
}

void PatternPlayer::emitBarFill(juce::MidiBuffer& midi,
                                int numSamples,
                                double beatStart,
                                double beatEnd,
                                int fillPatternIndex,
                                double fillBarStart) noexcept
{
    if (library == nullptr || numSamples <= 0 || beatEnd <= beatStart + 1.0e-9)
        return;
    if (fillPatternIndex < 17 || fillPatternIndex > 19)
        return;

    const MidiPattern& fill = library->getPattern(fillPatternIndex);
    const MidiPattern& groove = library->getPattern(activePatternIndex);
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const double swingDelayMs = static_cast<double>(swing) * (1.0 / 6.0)
                              * 60000.0 / juce::jmax(1.0f, bpm);
    const double barStart = fillBarStart;
    const double windowStart = fillGrooveCutBeat(fillPatternIndex, barStart);
    const double blockStartBeat = static_cast<double>(sampleCounter) / samplesPerBeat;
    double earlyBeats = 0.0, lateBeats = 0.0;
    microtimingSlackBeats(samplesPerBeat, samplesPerMs, swingDelayMs, earlyBeats, lateBeats);
    const int64_t slackSamples = static_cast<int64_t>(std::llround(lateBeats * samplesPerBeat)) + 1;
    (void) earlyBeats;

    for (const auto& ev : fill.drumEvents)
    {
        const double t = barStart + static_cast<double>(ev.beatOffset);
        if (t < windowStart - 1.0e-9)
            continue;

        // T7.3: skip the fill's terminal crash when the incoming bar already
        // crashes on its downbeat (same ±20 ms window as T1.6).
        if (ev.note == kCrashNote && ev.beatOffset >= 3.5f
            && patternCrashesNear(groove, barStart + 4.0))
            continue;

        const int grid16 = Groove::grid16Of(ev.beatOffset);
        const bool isGhost = ev.isGhost || (ev.velocity <= grooveTemplate.ghostThreshold);
        const int64_t eventBar = static_cast<int64_t>(std::floor(t / 4.0));

        float timeMs = grooveTemplate.timingMs[grid16];
        if (swing > 0.0f && (grid16 % 4) == 2)
            timeMs += static_cast<float>(swingDelayMs);
        if (isGhost)
            timeMs += grooveTemplate.ghostTimingMs;
        timeMs += eventGaussian(eventBar, grid16, ev.note, kSaltDrumTime,
                                grooveTemplate.timingJitterMs);

        const int64_t absSample = sampleCounter + static_cast<int64_t>(std::llround(
            (t - blockStartBeat) * samplesPerBeat
            + static_cast<double>(timeMs) * samplesPerMs));
        const int absOff = placeEvent(absSample, numSamples, slackSamples);
        if (absOff < 0)
            continue;

        const float mul = grooveTemplate.velocityMul[grid16] * sectionVelMul;
        int vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * mul
            + eventGaussian(eventBar, grid16, ev.note, kSaltDrumVel,
                            grooveTemplate.velocityJitter)));
        if (isGhost)
            vel = juce::jlimit(static_cast<int>(grooveTemplate.ghostVelocityLo),
                               static_cast<int>(grooveTemplate.ghostVelocityHi), vel);
        else
            vel = applyVelocityHeadroom(vel);

        int outNote = juce::jlimit(0, 127, static_cast<int>(ev.note));
        float durBeats = ev.durationBeats;
        if (outNote == kHatOpen && durBeats < 1.0f)
            durBeats = 1.0f;
        const int durSamps = juce::jmax(
            1, static_cast<int>(std::round(static_cast<double>(durBeats) * samplesPerBeat)));
        scheduleDrumNoteOff(midi, numSamples, sampleCounter, outNote, absOff, durSamps);
        midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, outNote,
                                                static_cast<float>(vel) / 127.0f),
                      absOff);
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
    //
    // T9.2: the phrase-end decision is made per BAR over the block's beat window
    // (not from the block-start bar), and the toms are placed by absolute sample,
    // so the result cannot depend on the host buffer size.
    (void) sampleOffsetBase;
    if (numSamples <= 0)
        return;

    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const double blockStartBeat = static_cast<double>(sampleCounter) / samplesPerBeat;
    double earlyBeats = 0.0, lateBeats = 0.0;
    microtimingSlackBeats(samplesPerBeat, samplesPerMs, 0.0, earlyBeats, lateBeats);
    const int64_t slackSamples = static_cast<int64_t>(std::llround(lateBeats * samplesPerBeat)) + 1;

    const auto addTom = [&](int note, int vel, double beat) noexcept {
        const int64_t absSample = sampleCounter + static_cast<int64_t>(std::llround(
            (beat - blockStartBeat) * samplesPerBeat));
        const int absOff = placeEvent(absSample, numSamples, slackSamples);
        if (absOff < 0)
            return;
        const int durSamps = juce::jmax(1, static_cast<int>(std::round(0.125 * samplesPerBeat)));
        scheduleDrumNoteOff(midi, numSamples, sampleCounter, note, absOff, durSamps);
        midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, note, static_cast<float>(vel) / 127.0f),
                      absOff);
    };

    const double barLo = std::floor((beatStart - lateBeats) / 4.0) * 4.0;
    const double barHi = beatEnd + earlyBeats;
    for (double barStart = barLo; barStart < barHi - 1.0e-9; barStart += 4.0)
    {
        const auto barOrn = computeOrnamentation(
            static_cast<int64_t>(std::floor(barStart / 4.0)), activePatternIndex);
        if (!barOrn.microFill)
            continue;
        addTom(kTomHi, 105, barStart + 3.75);    // "a" of beat 4
        addTom(kTomMid, 108, barStart + 3.875);  // the very last 16th before the downbeat
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

    // ── Buffer-invariant placement (review T9.2) ────────────────────────────
    // An event's sample position must not depend on the host block size. Enumerate
    // pattern occurrences by index (t = phase + n*patternLenBeats — an exact value
    // for a 16th grid) and place each at its ABSOLUTE sample, skipping those that
    // belong to a neighbouring block. The previous form rebuilt `t` against the
    // per-block beat origin and clamped the offset into [0, numSamples-1], which
    // quantised microtiming to the block start and made the render buffer-dependent.
    const double blockBeats = static_cast<double>(numSamples) / samplesPerBeat;
    const double blockStartBeat = static_cast<double>(sampleCounter) / samplesPerBeat;

    double earlyBeats = 0.0, lateBeats = 0.0;
    microtimingSlackBeats(samplesPerBeat, samplesPerMs, swingDelayMs, earlyBeats, lateBeats);

    // The caller may split the block at a bar line. Apply the microtiming slack only
    // on the outer edges of the block so the two halves stay disjoint and no event
    // is emitted twice.
    const bool isFirstSubRange = (sampleOffsetBase == 0);
    const bool isLastSubRange  = (beatEnd >= blockStartBeat + blockBeats - 1.0e-9);
    const double windowLo = beatStart - (isFirstSubRange ? lateBeats : 0.0);
    const double windowHi = beatEnd  + (isLastSubRange  ? earlyBeats : 0.0);
    const int64_t slackSamples = static_cast<int64_t>(std::llround(lateBeats * samplesPerBeat)) + 1;

    // Tier-0 ornaments are a pure function of (bar, pattern). Resolve them per
    // EVENT bar, not per block-start bar, or a block that straddles a bar line
    // would pick different ornaments depending on the host buffer size (T9.2).
    int64_t ornBar = std::numeric_limits<int64_t>::min();
    BarOrnamentation ornLocal{};

    for (const auto& ev : pattern.drumEvents)
    {
        // Phase of this event within the looping pattern.
        const double phase = std::fmod(static_cast<double>(ev.beatOffset), patternLenBeats);

        // 16th grid cell within the bar — drives the velocity/timing hierarchy.
        const int grid16 = Groove::grid16Of(ev.beatOffset);
        const bool isGhost = ev.isGhost || (ev.velocity <= grooveTemplate.ghostThreshold);

        // Tier-1 groove grid: when a rendered grid matches the active pattern,
        // use its learned per-step velocity/offset instead of the fixed template.
        const int voice = GrooveGridUtil::voiceForNote(ev.note);
        const bool useGrid = grooveGrid.valid && grooveGrid.patternIndex == activePatternIndex && voice >= 0;
        const float gridVel = useGrid ? grooveGrid.velocity[static_cast<size_t>(voice)][static_cast<size_t>(grid16)] : 0.0f;
        const float gridOff = useGrid ? grooveGrid.offset[static_cast<size_t>(voice)][static_cast<size_t>(grid16)] : 0.0f;

        const double nLo = std::floor((windowLo - phase) / patternLenBeats);
        const double nHi = std::ceil((windowHi - phase) / patternLenBeats);
        for (double n = nLo; n <= nHi; n += 1.0)
        {
            const double t = phase + n * patternLenBeats;   // absolute beat
            const int64_t eventBar = static_cast<int64_t>(std::floor(t / 4.0));

            if (eventBar != ornBar)
            {
                ornLocal = computeOrnamentation(eventBar, activePatternIndex);
                ornBar = eventBar;
            }

            // Tier-0: omit a non-essential kick to leave space (breakdown/outro).
            if (ornLocal.dropKick && ev.note == kKick && grid16 == ornLocal.dropKickCell)
                continue;

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
                timeMs += eventGaussian(eventBar, grid16, ev.note, kSaltDrumTime,
                                        grooveTemplate.timingJitterMs);
            }

            const int64_t absSample = sampleCounter + static_cast<int64_t>(std::llround(
                (t - blockStartBeat) * samplesPerBeat
                + static_cast<double>(timeMs) * samplesPerMs));
            const int absOff = placeEvent(absSample, numSamples, slackSamples);
            if (absOff < 0)
                continue;   // belongs to a neighbouring block; that block emits it

            // ── Velocity: learned grid (Tier-1) or template hierarchy + jitter (A2.1/A3.1) ──
            int vel;
            if (useGrid)
            {
                // Tier-1 v2: grid velocity is a *multiplier* on the authored
                // velocity (~1.0), not an absolute value — the authored pattern's
                // dynamics are preserved and the model humanises around them.
                vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * gridVel * sectionVelMul));
                vel = applyVelocityHeadroom(vel);
            }
            else
            {
                const float mul = grooveTemplate.velocityMul[grid16] * sectionVelMul;
                vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * mul
                                                  + eventGaussian(eventBar, grid16, ev.note, kSaltDrumVel,
                                                                  grooveTemplate.velocityJitter)));
                if (isGhost)
                    vel = juce::jlimit(static_cast<int>(grooveTemplate.ghostVelocityLo),
                                       static_cast<int>(grooveTemplate.ghostVelocityHi), vel);
                else
                    vel = applyVelocityHeadroom(vel);
            }

            int outNote = juce::jlimit(0, 127, static_cast<int>(ev.note));
            // Tier-0: open one closed-hat cell, or switch hats -> ride for the bar.
            if (ornLocal.openHat && ev.note == kHatClosed && grid16 == ornLocal.openHatCell)
                outNote = kHatOpen;
            else if (ornLocal.rideSwitch && ev.note == kHatClosed)
                outNote = (grid16 == 0) ? kRideBell : kRide;

            float durBeats = ev.durationBeats;
            if (outNote == kHatOpen && durBeats < 1.0f)
                durBeats = 1.0f;
            const int durSamps = juce::jmax(
                1,
                static_cast<int>(std::round(static_cast<double>(durBeats) * samplesPerBeat)));
            scheduleDrumNoteOff(midi, numSamples, sampleCounter, outNote, absOff, durSamps);
            midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, outNote, static_cast<float>(vel) / 127.0f),
                          absOff);
        }
    }

    // ── Ghost notes (A3.2) ─────────────────────────────────────────────────
    if (ghostDensity > 0.01f && sectionAllowsGhosts())
        emitGhostNotes(midi, numSamples, beatStart, beatEnd, occupied);
}

void PatternPlayer::emitGhostNotes(juce::MidiBuffer& midi,
                                   int numSamples,
                                   double beatStart,
                                   double beatEnd,
                                   const bool occupied[16])
{
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const int durSamps = juce::jmax(1, static_cast<int>(std::round(0.25 * samplesPerBeat)));
    const double blockStartBeat = static_cast<double>(sampleCounter) / samplesPerBeat;
    double earlyBeats = 0.0, lateBeats = 0.0;
    microtimingSlackBeats(samplesPerBeat, samplesPerMs, 0.0, earlyBeats, lateBeats);
    const int64_t slackSamples = static_cast<int64_t>(std::llround(lateBeats * samplesPerBeat)) + 1;

    const auto emitOneGhost = [&](double barStart, int cell) noexcept {
        if (occupied[cell])
            return;
        const double beat = barStart + static_cast<double>(cell) / 4.0;
        const int64_t eventBar = static_cast<int64_t>(std::floor(barStart / 4.0));
        const float timeMs = grooveTemplate.ghostTimingMs
                           + eventGaussian(eventBar, cell, 38, kSaltGhostTime,
                                           grooveTemplate.timingJitterMs);
        // Absolute placement (T9.2) — see emitDrumEventsForRange.
        const int64_t absSample = sampleCounter + static_cast<int64_t>(std::llround(
            (beat - blockStartBeat) * samplesPerBeat
            + static_cast<double>(timeMs) * samplesPerMs));
        const int absOff = placeEvent(absSample, numSamples, slackSamples);
        if (absOff < 0)
            return;

        const float ghostLo = grooveTemplate.ghostVelocityLo;
        const float ghostHi = grooveTemplate.ghostVelocityHi;
        const unsigned h = barHash(static_cast<unsigned>(eventBar) ^ humanizeSeed_,
                                   kSaltGhostVel ^ static_cast<unsigned>(cell));
        const float u = static_cast<float>(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
        const int vel = juce::jlimit(1, 127,
            static_cast<int>(std::round(ghostLo + u * (ghostHi - ghostLo))));

        scheduleDrumNoteOff(midi, numSamples, sampleCounter, 38, absOff, durSamps);
        midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, 38, static_cast<float>(vel) / 127.0f),
                      absOff);
    };

    // Candidate ghost cells (off-16ths). All avoid landing on the same sample
    // as an authored snare note-off, so no same-note overlap is possible.
    const int lowCells[1] = { 9 };                        // "e" of 3 — the classic
    const int highCells[4] = { 1, 9, 11, 15 };            // e of 1, e of 3, a of 3, a of 4
    const int* cells = (ghostDensity >= 0.7f) ? highCells : lowCells;
    const int numCells = (ghostDensity >= 0.7f) ? 4 : 1;

    const double barLo = std::floor((beatStart - lateBeats) / 4.0) * 4.0;
    const double barHi = beatEnd + earlyBeats;
    for (double barStart = barLo; barStart < barHi - 1.0e-9; barStart += 4.0)
    {
        for (int c = 0; c < numCells; ++c)
            emitOneGhost(barStart, cells[c]);

        // Tier-0: an extra ornamented ghost snare on an unoccupied off-16th.
        // Resolved per bar (not per block) so the choice cannot depend on the
        // host buffer size (T9.2).
        const auto barOrn = computeOrnamentation(
            static_cast<int64_t>(std::floor(barStart / 4.0)), activePatternIndex);
        if (barOrn.extraGhost && barOrn.extraGhostCell >= 0)
            emitOneGhost(barStart, barOrn.extraGhostCell);
    }
}

void PatternPlayer::emitBassRange(juce::MidiBuffer& midi,
                                  int numSamples,
                                  double beatStart,
                                  double beatEnd,
                                  const MidiPattern& pattern,
                                  int sampleOffsetBase,
                                  bool clampEarly,
                                  int64_t suppressBeforeAbs)
{
    if (beatEnd <= beatStart + 1.0e-9 || numSamples <= 0)
        return;

    // T5.1: authored bass lines play when present; otherwise the harmonic
    // engine (A1.2) builds one from the guitarist's root.
    if (!pattern.bassEvents.empty())
        emitPatternBass(midi, numSamples, beatStart, beatEnd, pattern,
                        sampleOffsetBase, clampEarly, suppressBeforeAbs);
    else
        emitHarmonicBass(midi, numSamples, beatStart, beatEnd,
                         sampleOffsetBase, clampEarly, suppressBeforeAbs);
}

void PatternPlayer::emitBassNote(juce::MidiBuffer& midi,
                                 int numSamples,
                                 int64_t blockStart,
                                 int outNote,
                                 int vel,
                                 int off,
                                 int durSamps,
                                 int sampleOffsetBase,
                                 bool forceRetrigger,
                                 bool hold)
{
    // Monophonic bass: close any previously scheduled note before the new one.
    // The deferred note-off carries the correct note number (bassNoteOffMidi),
    // so alternating/interval bass lines never leave a note stuck on.
    if (bassNoteOffSample >= 0)
    {
        const int64_t newOnAbs = blockStart + static_cast<int64_t>(off);
        // Close at the earlier of the scheduled end and the new onset so a
        // ringing mirror is cut at the grid hit (T5.3) and a note that already
        // ended in this block is not held past its gate.
        const int64_t closeAbs = (forceRetrigger && bassNoteOffSample > newOnAbs)
            ? newOnAbs
            : juce::jmin(bassNoteOffSample, newOnAbs);
        const int closeAt = juce::jlimit(0, numSamples - 1,
                                         static_cast<int>(closeAbs - blockStart));
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi),
                      sampleOffsetBase + closeAt);
        bassNoteOffSample = -1;
        bassNoteHeld_ = false;
    }

    midi.addEvent(juce::MidiMessage::noteOn(kBassChannel, outNote, static_cast<float>(vel) / 127.0f),
                  sampleOffsetBase + off);
    bassLastMidiNote = outNote;

    if (hold)
    {
        // Sustain: no scheduled note-off. Released when the guitarist stops
        // (or closed by the next attack / a flush).
        bassNoteOffMidi = outNote;
        bassNoteOffSample = std::numeric_limits<int64_t>::max();
        bassNoteHeld_ = true;
        return;
    }

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
                                    int sampleOffsetBase,
                                    bool clampEarly,
                                    int64_t suppressBeforeAbs)
{
    const double patternLenBeats = static_cast<double>(pattern.lengthInBars) * 4.0;
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const int64_t blockStart = sampleCounter;
    const double blockStartBeat = static_cast<double>(blockStart) / samplesPerBeat;
    double earlyBeats = 0.0, lateBeats = 0.0;
    microtimingSlackBeats(samplesPerBeat, samplesPerMs, 0.0, earlyBeats, lateBeats);
    const int64_t slackSamples = static_cast<int64_t>(std::llround(lateBeats * samplesPerBeat)) + 1;

    // Absolute placement (T9.2) — see emitDrumEventsForRange.
    const double blockBeats = static_cast<double>(numSamples) / samplesPerBeat;
    const bool isFirstSubRange = (sampleOffsetBase == 0);
    const bool isLastSubRange  = (beatEnd >= blockStartBeat + blockBeats - 1.0e-9);
    const double windowLo = beatStart - (isFirstSubRange ? lateBeats : 0.0);
    const double windowHi = beatEnd  + (isLastSubRange  ? earlyBeats : 0.0);

    for (const auto& ev : pattern.bassEvents)
    {
        const double phase = std::fmod(static_cast<double>(ev.beatOffset), patternLenBeats);
        const int grid16 = Groove::grid16Of(ev.beatOffset);

        const double nLo = std::floor((windowLo - phase) / patternLenBeats);
        const double nHi = std::ceil((windowHi - phase) / patternLenBeats);
        for (double n = nLo; n <= nHi; n += 1.0)
        {
            const double t = phase + n * patternLenBeats;   // absolute beat
            const int64_t eventBar = static_cast<int64_t>(std::floor(t / 4.0));
            // Bass sits slightly behind the kick for pocket (A1.2), with small jitter.
            const float timeMs = grooveTemplate.bassPocketMs
                               + eventGaussian(eventBar, grid16, ev.note, kSaltBassTime,
                                               grooveTemplate.timingJitterMs);
            const int64_t nominalAbs = blockStart + static_cast<int64_t>(std::llround(
                (t - blockStartBeat) * samplesPerBeat));
            int64_t absSample = nominalAbs + static_cast<int64_t>(std::llround(
                static_cast<double>(timeMs) * samplesPerMs));
            if (absSample < nominalAbs)
                absSample = nominalAbs;   // pocket delays, never anticipates
            const int off = placeEvent(absSample, numSamples, slackSamples, clampEarly);
            if (off < 0)
                continue;   // belongs to a neighbouring block
            if (suppressBeforeAbs >= 0 && absSample < suppressBeforeAbs)
                continue;   // live mirror owns the voice here — this is a gap note

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
                                                  + eventGaussian(eventBar, grid16, outNote, kSaltBassVel, 4.0f)));
            vel = applyVelocityHeadroom(vel);

            const int durSamps = juce::jmax(1, static_cast<int>(std::round(
                static_cast<double>(ev.durationBeats) * sectionBassGate() * samplesPerBeat)));

            // T5.3: retrigger — close a ringing mirror/previous grid note at this
            // hit rather than dropping the authored event.
            ++gridBassNotes_;   // diagnostics (see getGridBassNoteCount)
            emitBassNote(midi, numSamples, blockStart, outNote, vel, off, durSamps, 0, true);
        }
    }
}

void PatternPlayer::emitHarmonicBass(juce::MidiBuffer& midi,
                                     int numSamples,
                                     double beatStart,
                                     double beatEnd,
                                     int sampleOffsetBase,
                                     bool clampEarly,
                                     int64_t suppressBeforeAbs)
{
    if (beatEnd <= beatStart + 1.0e-9 || bassNotesPerBar <= 0)
        return;

    const int64_t blockStart = sampleCounter;
    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double samplesPerMs = sampleRate / 1000.0;
    const double beatsPerNote = 4.0 / static_cast<double>(bassNotesPerBar);
    const double blockStartBeat = static_cast<double>(blockStart) / samplesPerBeat;
    double earlyBeats = 0.0, lateBeats = 0.0;
    microtimingSlackBeats(samplesPerBeat, samplesPerMs, 0.0, earlyBeats, lateBeats);
    const int64_t slackSamples = static_cast<int64_t>(std::llround(lateBeats * samplesPerBeat)) + 1;

    // Absolute placement (T9.2). The caller may split the block at a bar line;
    // keep the two halves disjoint by applying slack only on the block's outer edges.
    const double blockBeats = static_cast<double>(numSamples) / samplesPerBeat;
    const bool isFirstSubRange = (sampleOffsetBase == 0);
    const bool isLastSubRange  = (beatEnd >= blockStartBeat + blockBeats - 1.0e-9);
    const double windowLo = beatStart - (isFirstSubRange ? lateBeats : 0.0);
    const double windowHi = beatEnd  + (isLastSubRange  ? earlyBeats : 0.0);

    // Find the first beat in this window that should trigger a bass note.
    const double firstBeat = std::ceil((windowLo - 1.0e-9) / beatsPerNote) * beatsPerNote;

    for (double beat = firstBeat; beat < windowHi - 1.0e-9; beat += beatsPerNote)
    {
        const int beatInBar = static_cast<int>(std::floor(std::fmod(beat, 4.0)));
        const int64_t eventBar = static_cast<int64_t>(std::floor(beat / 4.0));
        const int degree = harmonyDegree(beatInBar, static_cast<int>(eventBar));

        int outNote = bassRootMidi + bassSemitoneOffset + degree;
        while (outNote < 28) outNote += 12;
        while (outNote > 55) outNote -= 12;
        outNote = juce::jlimit(0, 127, outNote);

        // Accent beat 1, soften beat 3; humanise ±5.
        float mul = sectionVelMul;
        if (beatInBar == 0)      mul *= 1.08f;
        else if (beatInBar == 2) mul *= 0.96f;
        int vel = static_cast<int>(std::round(95.0f * mul + eventGaussian(eventBar, beatInBar * 4, outNote,
                                                                          kSaltBassVel, 4.0f)));
        vel = applyVelocityHeadroom(vel);

        // Slightly behind the kick for pocket.
        float timeMs = grooveTemplate.bassPocketMs
                     + eventGaussian(eventBar, beatInBar * 4, outNote, kSaltBassTime,
                                     grooveTemplate.timingJitterMs);
        const int64_t nominalAbs = blockStart + static_cast<int64_t>(std::llround(
            (beat - blockStartBeat) * samplesPerBeat));
        int64_t absSample = nominalAbs + static_cast<int64_t>(std::llround(
            static_cast<double>(timeMs) * samplesPerMs));
        // The pocket offset is a *delay* (bassPocketMs > 0); jitter must not pull a
        // root ahead of its beat, or the note would be scored against the previous
        // bar. Clamping to the nominal beat sample is absolute, so it stays
        // buffer-invariant.
        if (absSample < nominalAbs)
            absSample = nominalAbs;
        const int off = placeEvent(absSample, numSamples, slackSamples, clampEarly);
        if (off < 0)
            continue;   // belongs to a neighbouring block
        if (suppressBeforeAbs >= 0 && absSample < suppressBeforeAbs)
            continue;   // live mirror owns the voice here — this is a gap note

        // 85% gate (kept from the old engine, now a named parameter scaled by section).
        const double noteDuration = beatsPerNote * sectionBassGate();
        const int durSamps = juce::jmax(1, static_cast<int>(std::round(noteDuration * samplesPerBeat)));

        // T5.3: retrigger a ringing mirror at the grid hit so a pickup on the
        // "and of 4" cannot swallow the next downbeat root.
        ++gridBassNotes_;   // diagnostics (see getGridBassNoteCount)
        emitBassNote(midi, numSamples, blockStart, outNote, vel, off, durSamps, 0, true);
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

void PatternPlayer::process(juce::MidiBuffer& midi, int numSamples, int64_t hostSamplePosition,
                            bool hostRolling)
{
    if (library == nullptr || numSamples <= 0)
        return;

    // ── Transport frozen? (DAW stopped / no moving playhead) ────────────────
    // A stopped transport keeps getTimeInSamples() constant. Treating that as a
    // jump every block wiped pending pattern changes (drums stuck on Silent)
    // and anchored the beat clock to a fixed phase (bass/drums machine-gunning
    // at block rate — the "harsh constant" sound). Instead, run the plugin's
    // own beat clock when the host position is frozen, so jamming works with
    // the transport stopped.
    //
    // DAW Record/Play unfreezes the playhead. The free-run sampleCounter is
    // unrelated to that timeline, so the first moving sample must snap onto
    // the host grid without a seek dump — otherwise count-in/click restart.
    // While the host reports rolling, a stalled duplicate callback must not
    // flip back onto the internal clock (that desyncs metronome vs count-in).
    const int64_t rawHost = hostSamplePosition;
    const bool positionFrozen = (rawHost == lastHostSample);
    const bool transportFrozen = positionFrozen && !hostRolling;
    const bool transportJustStarted = lastTransportFrozen && !transportFrozen;
    lastHostSample = rawHost;
    lastTransportFrozen = transportFrozen;
    if (transportFrozen)
        hostSamplePosition = sampleCounter + static_cast<int64_t>(numSamples);
    else if (transportJustStarted)
        expectedHostSample = rawHost;

    const double samplesPerBeat = (60.0 / juce::jmax(1.0f, bpm)) * sampleRate;
    const double beatStart = static_cast<double>(hostSamplePosition) / samplesPerBeat;
    const double beatEnd = beatStart + static_cast<double>(numSamples) / samplesPerBeat;

    // Section velocity multiplier for this block (A3.1), scaled by the
    // guitarist-energy dynamic so the kit/bass swell with the guitarist's picking.
    // T3.1: kVelocityTrim keeps the product from pinning every accent at 127.
    sectionVelMul = preset.sectionVelocityMultiplier(sectionId) * preset.velocityScale
                  * guitarEnergy * kVelocityTrim;

    // Tier-0 ornamentation is resolved per EVENT bar inside the emitters
    // (computeOrnamentation), so it cannot depend on where the block boundaries
    // fall — see T9.2.

    // Latest request wins. A pending change queued during click/silence used
    // to block Play rotation (T4.1) because pendingPatternIndex stayed >= 0.
    const int requested = patternIndex.load(std::memory_order_relaxed);
    if (requested == activePatternIndex)
        pendingPatternIndex = -1;
    else
        pendingPatternIndex = requested;

    // Transport jump detection (seek / loop / re-instantiation) — drop any
    // deferred state that was scheduled relative to a previous timeline position.
    // Stopped → Record/Play is not a seek (handled above). Small blips (±2
    // blocks) are PDC / duplicate callbacks, not timeline jumps.
    if (!transportFrozen && !transportJustStarted)
    {
        const int64_t slack = static_cast<int64_t>(numSamples) * 2;
        const int64_t delta = hostSamplePosition - expectedHostSample;
        if (delta < -slack || delta > slack)
        {
            transportJumped_ = true;
            flushAllPendingNoteOffs(midi, 0);
            pendingPatternIndex = -1;
            pendingGrooveCommitValid = false;
            pendingGrooveCommit = GrooveCommit{};
            for (auto& p : pendingLearned_)
                p = {};
            armCrashPending = false;
        }
    }
    expectedHostSample = hostSamplePosition + static_cast<int64_t>(numSamples);
    sampleCounter = hostSamplePosition;

    // Silence: emit pending offs, then all-notes-off, and drop armed crash/fill/lead-in
    // so a crash armed while silent cannot fire after the gate re-opens (T1.2 / T1.6).
    if (structureSilent && !clickTrack_)
    {
        flushAllPendingNoteOffs(midi, 0);
        if (!wasSilent)
            for (int ch = 1; ch <= 16; ++ch)
                midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        wasSilent = true;
        armCrashPending = false;
        pendingBarFillIndex_ = -1;
        barFillStartBeat_ = -1.0;
        bassLeadInArmed = false;
        for (auto& p : pendingLearned_)
            p = {};
        wasClickTrack_ = false;
        return;
    }

    const bool enteringClick = clickTrack_ && !wasClickTrack_;
    if (wasClickTrack_ && !clickTrack_)
    {
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, kClickKickNote), 0);
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, kClickStickNote), 0);
        clickNoteOffSample = -1;
    }
    if (enteringClick)
        flushAllPendingNoteOffs(midi, 0);
    wasClickTrack_ = clickTrack_;

    wasSilent = false;

    if (clickTrack_)
    {
        emitClickTrack(midi, numSamples, beatStart, beatEnd, hostSamplePosition);
        return;
    }

    // Resolve a pending pattern change at the first bar boundary in this block
    // (or the first beat when the commit is a T6.1 gesture).
    constexpr double beatsPerBar = 4.0;
    double changeBeat = -1.0;
    if (pendingGrooveCommitValid || pendingPatternIndex >= 0)
    {
        const double quant = (pendingGrooveCommitValid && pendingGrooveCommit.alignToBeat)
            ? 1.0 : beatsPerBar;
        const double boundary = std::ceil(beatStart / quant - 1.0e-9) * quant;
        if (boundary < beatEnd - 1.0e-9)
            changeBeat = boundary;
    }

    if (pendingBarFillIndex_ >= 17 && barFillStartBeat_ < -1.5)
        barFillStartBeat_ = (std::floor(beatStart / 4.0) + 1.0) * 4.0;

    const double fillOrigin = (pendingBarFillIndex_ >= 17)
        ? ((barFillStartBeat_ >= 0.0) ? barFillStartBeat_ : std::floor(beatStart / 4.0) * 4.0)
        : beatEnd;
    const bool fillEmitting = pendingBarFillIndex_ >= 17 && beatEnd > fillOrigin + 1.0e-12;
    const double fillCut = fillEmitting
        ? fillGrooveCutBeat(pendingBarFillIndex_, fillOrigin)
        : beatEnd;

    auto emitGroove = [&](double from, double to, int patIdx) noexcept
    {
        if (patIdx == 0 || to <= from + 1.0e-12)
            return;
        if (fillEmitting)
        {
            if (from >= fillCut - 1.0e-12)
                return;
            to = std::min(to, fillCut);
            if (to <= from + 1.0e-12)
                return;
        }
        const int base = juce::jmax(0, static_cast<int>(std::llround((from - beatStart) * samplesPerBeat)));
        emitDrumEventsForRange(midi, numSamples, from, to, library->getPattern(patIdx), base);
    };

    if (changeBeat < 0.0)
    {
        emitGroove(beatStart, beatEnd, activePatternIndex);
    }
    else
    {
        emitGroove(beatStart, changeBeat, activePatternIndex);

        if (pendingGrooveCommitValid)
        {
            activePatternIndex = pendingGrooveCommit.patternIndex;
            pendingGrooveCommitValid = false;
            pendingGrooveCommit = GrooveCommit{};
        }
        else
        {
            activePatternIndex = pendingPatternIndex;
        }
        pendingPatternIndex = -1;

        emitGroove(changeBeat, beatEnd, activePatternIndex);
    }

    // Armed crash: next beat, or the section bar line when a commit landed this block.
    // Skip if the sounding pattern already crashes there (T1.6). Stay pending until
    // a block actually contains the target beat.
    if (armCrashPending)
    {
        const double boundary = (changeBeat >= 0.0)
            ? changeBeat
            : std::ceil(beatStart - 1.0e-9);
        if (boundary >= beatStart - 1.0e-9 && boundary < beatEnd - 1.0e-9)
        {
            const bool dup = (library != nullptr)
                && patternCrashesNear(library->getPattern(activePatternIndex), boundary);
            if (!dup)
            {
                const int off = juce::jlimit(0, numSamples - 1,
                    static_cast<int>(std::llround((boundary - beatStart) * samplesPerBeat)));
                emitCrashHit(midi, numSamples, hostSamplePosition, off);
            }
            armCrashPending = false;
        }
    }

    if (fillEmitting)
    {
        emitBarFill(midi, numSamples, beatStart, beatEnd, pendingBarFillIndex_, fillOrigin);
        const double fillBarEnd = fillOrigin + 4.0;
        if (beatEnd >= fillBarEnd - 1.0e-9)
        {
            pendingBarFillIndex_ = -1;
            barFillStartBeat_ = -1.0;
        }
    }

    // Tier-0 micro-fill: a two-note tom pickup into the next downbeat at the end
    // of a 4-bar phrase (the downbeat's own kick/crash is the pattern's).
    if (!fillEmitting)
        emitMicroFill(midi, numSamples, beatStart, beatEnd, 0);

    // Bass note-offs that expire without a retrigger are flushed after all
    // bass emission (below). emitBassNote closes a ringing note at the new
    // onset when a grid/mirror hit arrives first (T5.3), so an early flush
    // here would leave a later off that kills the retriggered root.

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
            emitBassNote(midi, numSamples, sampleCounter, pickupNote, 96, off, durSamps, 0, true);
            bassLeadInArmed = false;
        }
    }

    // Learned bass note-ons (RiffA / RiffBLocked snapshots, or live listen).
    // Duration is already gated by the caller (T5.2: onset × gate16 × 90%,
    // kBassGate for live mirror). Do not multiply sectionBassGate() again.
    {
        PendingLearnedNote notes[kMaxPendingLearned];
        int n = 0;
        for (auto& p : pendingLearned_)
        {
            if (!p.active)
                continue;
            notes[n++] = p;
            p.active = false;
        }
        std::sort(notes, notes + n, [](const PendingLearnedNote& a, const PendingLearnedNote& b) {
            return a.offset < b.offset;
        });
        for (int i = 0; i < n; ++i)
        {
            const int off = juce::jlimit(0, numSamples - 1, notes[i].offset);
            const int vel = juce::jlimit(1, 127, static_cast<int>(std::lround(notes[i].vel * 127.0f)));
            const int durSamps = juce::jmax(1, notes[i].duration);
            const bool hold = notes[i].hold && guitarAudible_;
            emitBassNote(midi, numSamples, sampleCounter, notes[i].midi, vel, off, durSamps, 0,
                         true, hold);
            // The learned/mirrored note owns the monophonic bass voice until it
            // ends; a held note owns it until the guitar stops.
            mirrorVoiceEndSample_ = hold
                ? std::numeric_limits<int64_t>::max()
                : juce::jmax(mirrorVoiceEndSample_,
                             sampleCounter + static_cast<int64_t>(off) + static_cast<int64_t>(durSamps));
        }
    }

    // Guitar-stop release. A held mirror note sustains through the player's
    // sustain; when the guitarist actually stops, the note ends so the harmony
    // fallback below can take over. This is the whole point of the
    // "harmony is a total fallback" contract: a *sustain* is not a gap.
    if (bassNoteHeld_ && !guitarAudible_)
    {
        if (bassNoteOffSample >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi), 0);
            bassNoteOffSample = -1;
        }
        bassNoteHeld_ = false;
        mirrorVoiceEndSample_ = sampleCounter;
    }

    // Listen mixer (Play / RiffBListen): authored pattern bass when present,
    // harmonic fallback otherwise (T5.1). Frozen riffs leave this off and play
    // only the snapshot via triggerLearnedBassNote. Pattern 0 must not mute
    // this path — phase owns the grid, not the kit index.
    //
    // The live mirror is the primary bass part: while its note is sounding the
    // grid is muted (`suppressBeforeAbs`), so the harmony engine is heard only
    // in the gaps between mirrored notes — the fallback the guitarist wants
    // before a riff is learned, not a layer under it.
    if (beatGridBassEnabled_)
    {
        // A phase that switches the grid bass on has no earlier block to own an
        // event microtiming pulled before this one, so allow a one-block clamp.
        const bool gridBassOnset = !beatGridBassPrev_;
        const MidiPattern& bassPat = library->getPattern(activePatternIndex);
        const int64_t suppressBefore = mirrorVoiceEndSample_;
        if (changeBeat < 0.0)
            emitBassRange(midi, numSamples, beatStart, beatEnd, bassPat, 0, gridBassOnset,
                          suppressBefore);
        else
        {
            emitBassRange(midi, numSamples, beatStart, changeBeat, bassPat, 0, gridBassOnset,
                          suppressBefore);
            const int bassBase = juce::jmax(0,
                static_cast<int>(std::llround((changeBeat - beatStart) * samplesPerBeat)));
            emitBassRange(midi, numSamples, changeBeat, beatEnd, bassPat, bassBase, false,
                          suppressBefore);
        }
    }
    beatGridBassPrev_ = beatGridBassEnabled_;

    // Close a ringing bass note whose natural end falls in this block and was
    // not cut short by a later retrigger.
    if (bassNoteOffSample >= 0 && bassNoteOffSample < sampleCounter + numSamples)
    {
        const int off = juce::jlimit(0, numSamples - 1,
                                     static_cast<int>(bassNoteOffSample - sampleCounter));
        midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi), off);
        bassNoteOffSample = -1;
    }

    // Release drum notes whose gate expires in this block, at their exact sample.
    flushDueDrumNoteOffs(midi, numSamples, sampleCounter);
}
