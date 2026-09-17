#include "PhraseLearner.h"
#include <cmath>
#include <algorithm>
#include <climits>
#include <cstdint>

PhraseLearner::PhraseLearner()
{
    reset();
}

void PhraseLearner::prepare(double sampleRate) noexcept
{
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    // The attack detector derives its own time-based window from the sample
    // rate, so it means the same musical duration at every buffer size (A1).
    attackDetector.prepare(sampleRate_);
    silenceResetSamples_ = static_cast<int64_t>(std::llround(kSilenceResetSeconds * sampleRate_));
    reset();
}

void PhraseLearner::reset() noexcept
{
    state_ = State::Learning;
    attackWrite_ = 0;
    attackCount_ = 0;
    patternLen_ = 0;
    patternLenBeats_ = 4.0;
    locked_ = false;
    playbackPhase_ = 0.0;
    playbackStep_ = 0;
    lastTriggerSample_ = 0;
    following_ = false;
    justMatched_ = false;
    justMatchedRef_ = false;
    matchPattern_ = {};
    matchLen_ = 0;
    matchLenBeats_ = 16.0;
    mismatchStartSample_ = -1;
    holdActive_ = false;
    attackDetectedThisHop_ = false;
    attackDetector.reset();
    lastGoodPitchMidi_ = 36.0f;
    lastGoodPitchValid_ = false;
    silentSamples_ = 0;
    userCapturing_ = false;
    gridCapturing_ = false;
    gridListening_ = false;
    gridOccupied_ = 0;
    gridSlots_.fill({});
}

bool PhraseLearner::patternsMatch(int len, double bpm) const noexcept
{
    if (len < 2 || attackCount_ < len * 2)  // Reduced minimum from 3 to 2
        return false;

    // Compare IOIs (inter-onset intervals) between two consecutive phrases
    // Phrase A: attacks [attackCount_ - 2*len] to [attackCount_ - len - 1]
    // Phrase B: attacks [attackCount_ - len] to [attackCount_ - 1]

    // 1/4 beat tolerance at the actual tempo (not hardcoded 120 BPM).
    const double bpmSafe = (bpm > 0.0) ? bpm : 120.0;
    const double toleranceSamples = 0.25 * (60.0 / bpmSafe) * sampleRate_;

    for (int i = 0; i < len - 1; ++i)
    {
        int idxA1 = (attackWrite_ - 2 * len + i + kMaxAttacks) % kMaxAttacks;
        int idxA2 = (idxA1 + 1) % kMaxAttacks;
        int64_t ioiA = attacks_[idxA2].sample - attacks_[idxA1].sample;

        int idxB1 = (attackWrite_ - len + i + kMaxAttacks) % kMaxAttacks;
        int idxB2 = (idxB1 + 1) % kMaxAttacks;
        int64_t ioiB = attacks_[idxB2].sample - attacks_[idxB1].sample;

        if (std::abs(static_cast<double>(ioiA - ioiB)) > toleranceSamples)
            return false;
    }

    return true;
}

void PhraseLearner::lockPattern(double bpm, int64_t sampleTime) noexcept
{
    if (patternLen_ < 2)
        return;

    // Use the most recent phrase (last patternLen_ attacks) as the pattern
    int startIdx = (attackWrite_ - patternLen_ + kMaxAttacks) % kMaxAttacks;

    // First note is at beat 0
    pattern_[0].beatOffset = 0.0;
    pattern_[0].midiNote = mapToBassRange(attacks_[static_cast<size_t>(startIdx)].pitch);

    // Convert IOIs in samples to beats using the actual tempo at lock time.
    // Playback later re-scales by the transport BPM via playbackPhase_.
    const double bpmSafe = (bpm > 0.0) ? bpm : 120.0;
    const double beatsPerSample = bpmSafe / 60.0 / sampleRate_;

    double totalBeats = 0.0;
    for (int i = 1; i < patternLen_; ++i)
    {
        int prevIdx = (startIdx + i - 1) % kMaxAttacks;
        int curIdx = (startIdx + i) % kMaxAttacks;

        int64_t ioiSamples = attacks_[static_cast<size_t>(curIdx)].sample - 
                            attacks_[static_cast<size_t>(prevIdx)].sample;
        double ioiBeats = static_cast<double>(ioiSamples) * beatsPerSample;

        totalBeats += ioiBeats;
        pattern_[static_cast<size_t>(i)].beatOffset = totalBeats;
        pattern_[static_cast<size_t>(i)].midiNote = mapToBassRange(attacks_[static_cast<size_t>(curIdx)].pitch);
    }

    // Bar-align the loop: a riff that fits in N bars loops at exactly 4*N beats
    // so the bass stays in phase with the host bar grid (and the drums). The
    // old "+0.5 beat gap" made a 4-beat riff loop at 4.5 beats — the bass
    // drifted against the drum bar every loop.
    const int bars = std::max(1, static_cast<int>(std::lround(totalBeats / 4.0)));
    patternLenBeats_ = static_cast<double>(bars) * 4.0;
    // The loop must never be shorter than the riff itself, or the last note is
    // skipped when the phase wraps.
    while (patternLenBeats_ <= totalBeats + 1.0e-9)
        patternLenBeats_ += 4.0;

    // ── Density fix (0.9.12 regression + P0/R1) ───────────────────────────────
    // The bar-aligned loop is usually longer than the captured riff (e.g. a
    // 4-note slice in a 4-beat loop), leaving dead space that reads as sparse
    // staccato bass. Replicate the riff across the loop so the learned playback
    // stays dense and note-for-note while the groove lock holds it. For a
    // uniform chug (equal notes, uniform IOIs) the replication is exact.
    {
        const int riffLen = patternLen_;
        if (riffLen >= 2 && patternLenBeats_ > totalBeats + 1.0e-9 && totalBeats > 1.0e-9)
        {
            const double ioiAvg = totalBeats / static_cast<double>(riffLen - 1);
            const double cycleBeats = totalBeats + ioiAvg;  // riff repeat period
            int dst = riffLen;
            for (int cycle = 1; dst < kMaxPattern; ++cycle)
            {
                const double shift = static_cast<double>(cycle) * cycleBeats;
                if (shift >= patternLenBeats_ - 1.0e-9)
                    break;
                for (int i = 0; i < riffLen && dst < kMaxPattern; ++i)
                {
                    const double beat = pattern_[static_cast<size_t>(i)].beatOffset + shift;
                    if (beat >= patternLenBeats_ - 1.0e-9)
                        break;
                    pattern_[static_cast<size_t>(dst)].beatOffset = beat;
                    pattern_[static_cast<size_t>(dst)].midiNote = pattern_[static_cast<size_t>(i)].midiNote;
                    ++dst;
                }
            }
            patternLen_ = dst;
        }
    }

    locked_ = true;
    state_ = State::Locked;

    // Align the loop phase to the riff's own cycle: current position within the
    // loop = (now − phrase start) mod loop length. note[0] (the riff's first
    // note) then fires at the next phrase start, keeping the bass in phase with
    // the guitarist's repeats — and with the drums when the riff is bar-aligned.
    const int64_t phraseStartSample = attacks_[static_cast<size_t>(startIdx)].sample;
    double phase = static_cast<double>(sampleTime - phraseStartSample) * beatsPerSample;
    phase = std::fmod(phase, patternLenBeats_);
    if (phase < 0.0)
        phase += patternLenBeats_;
    playbackPhase_ = phase;

    // Set the step to the first note at or after the current phase. The phase is
    // usually mid-loop (the phrase started a few notes ago), so naively starting
    // at step 0 would silently skip every note whose offset is already in the
    // past — the bass would stay quiet until the loop wrapped. Advancing to the
    // next upcoming note makes the learned playback fire promptly and densely.
    int step = 0;
    while (step < patternLen_ && pattern_[static_cast<size_t>(step)].beatOffset < phase)
        ++step;
    playbackStep_ = step;
}

int PhraseLearner::mapToBassRange(float midiNote) const noexcept
{
    // Pitch class only — YIN on distorted guitar octave-flips constantly.
    // Fold onto C2–B2 (MIDI 36–47), the same register StablePitchTracker uses.
    const int rounded = static_cast<int>(std::round(midiNote));
    const int pc = ((rounded % 12) + 12) % 12;
    return 36 + pc;
}

int PhraseLearner::resolveBassNote(float pitchMidi, int stablePitchClassOffset) const noexcept
{
    if (stablePitchClassOffset != INT_MIN)
    {
        int off = stablePitchClassOffset;
        if (off < 0) off = 0;
        if (off > 11) off = 11;
        return 36 + off;
    }
    return mapToBassRange(pitchMidi);
}

float PhraseLearner::bassVelocityForRms(float rms) noexcept
{
    // Learned bass used to fire at 0.9 (~MIDI 114) with 90% beat gates — a
    // wall of sound sitting on top of the drums. Sit under the kit: MIDI ~61–86.
    // Wider range than 0.48..0.68: `rms * 4` saturated almost immediately, so a
    // 48 s real take produced only 3 distinct mirror velocities (measured) —
    // audible as a flat, robotic line. Still sits under the kit.
    float vel = 0.48f + rms * 2.6f;
    if (vel < 0.48f) vel = 0.48f;
    if (vel > 0.90f) vel = 0.90f;
    return vel;
}

void PhraseLearner::beginUserCapture() noexcept
{
    reset();
    userCapturing_ = true;
}

void PhraseLearner::cancelUserCapture() noexcept
{
    userCapturing_ = false;
    reset();
}

bool PhraseLearner::commitUserCapture(double bpm, int64_t sampleTime) noexcept
{
    if (!userCapturing_ || attackCount_ < 2)
        return false;

    userCapturing_ = false;
    patternLen_ = std::min(attackCount_, kMaxPattern);
    lockPattern(bpm, sampleTime);
    return locked_;
}

void PhraseLearner::beginGridCapture() noexcept
{
    reset();
    userCapturing_ = true;
    gridCapturing_ = true;
}

void PhraseLearner::beginLiveGridListen() noexcept
{
    reset();
    userCapturing_ = false;
    // Passive listen: stamp the grid as a side observation but keep the learner
    // in normal Learning mode so auto-lock and the fallback bass still work.
    gridCapturing_ = false;
    gridListening_ = true;
}

void PhraseLearner::cancelLiveGridListen() noexcept
{
    gridListening_ = false;
    gridOccupied_ = 0;
    gridSlots_.fill({});
}

void PhraseLearner::beginSectionGridListen() noexcept
{
    // Passive per-section capture: stamp the grid in parallel with the live
    // mirror. Deliberately does NOT call reset() — clearing the attack detector
    // would delay the first mirrored note after a section change.
    userCapturing_ = false;
    gridCapturing_ = false;
    gridListening_ = true;
    gridOccupied_ = 0;
    gridSlots_.fill({});
}

void PhraseLearner::endSectionGridListen() noexcept
{
    gridListening_ = false;
    gridOccupied_ = 0;
    gridSlots_.fill({});
}

void PhraseLearner::stampGridRange(double beat0, double beat1, float peak, int bassMidi,
                                   bool onset, uint8_t velocity) noexcept
{
    if (!gridCapturing_ && !gridListening_)
        return;
    if (peak < 0.025f)
        return;
    if (beat1 <= beat0)
        return;

    int midi = bassMidi;
    if (midi < 28) midi = 36;
    if (midi > 55) midi = 36 + (((midi % 12) + 12) % 12);

    constexpr double kLoop = static_cast<double>(kGridBars) * 4.0;
    const double a = std::max(0.0, beat0);
    const double b = std::min(kLoop, beat1);
    if (b <= a)
        return;

    // Stamp only 16ths that this range actually overlaps. A ringing palm-mute
    // in one slot must not paint neighbors: each caller's peak is for THIS
    // range only (processor splits the block per slot).
    const int slot0 = std::max(0, static_cast<int>(std::floor(a * 4.0)));
    const int slot1 = std::min(kGridSlots - 1,
                               static_cast<int>(std::floor((b - 1.0e-9) * 4.0)));
    for (int s = slot0; s <= slot1; ++s)
    {
        const double slotA = static_cast<double>(s) * 0.25;
        const double slotB = slotA + 0.25;
        const double overlap = std::min(b, slotB) - std::max(a, slotA);
        if (overlap <= 1.0e-9)
            continue;
        auto& slot = gridSlots_[static_cast<size_t>(s)];
        if (!slot.occupied)
        {
            slot.occupied = true;
            slot.onset = onset;
            slot.gate16 = 1;
            ++gridOccupied_;
        }
        else if (onset)
        {
            slot.onset = true;
        }
        slot.midiNote = midi;
        if (velocity > 0)
            slot.velocity = velocity;
    }
}

bool PhraseLearner::commitGridCapture() noexcept
{
    if ((!gridCapturing_ && !gridListening_) || gridOccupied_ < 2)
        return false;

    patternLen_ = 0;
    for (int s = 0; s < kGridSlots && patternLen_ < kMaxPattern; ++s)
    {
        const auto& slot = gridSlots_[static_cast<size_t>(s)];
        if (!slot.occupied)
            continue;
        pattern_[static_cast<size_t>(patternLen_)].beatOffset =
            static_cast<double>(s) * 0.25;
        pattern_[static_cast<size_t>(patternLen_)].midiNote = slot.midiNote;
        ++patternLen_;
    }

    if (patternLen_ < 2)
        return false;

    coalesceSlotGates();
    patternLenBeats_ = static_cast<double>(kGridBars) * 4.0;
    userCapturing_ = false;
    gridCapturing_ = false;
    gridListening_ = false;
    locked_ = true;
    state_ = State::Locked;
    playbackPhase_ = 0.0;
    playbackStep_ = 0;
    return true;
}

void PhraseLearner::exportPattern(LearnedRiff& dest) const noexcept
{
    dest.valid = false;
    dest.lenBeats = static_cast<double>(kGridBars) * 4.0;
    dest.occupied.fill(false);
    dest.midi.fill(36);
    dest.gate16.fill(0);
    dest.velocity.fill(0);
    int n = 0;
    for (int s = 0; s < kGridSlots; ++s)
    {
        const auto& slot = gridSlots_[static_cast<size_t>(s)];
        dest.occupied[static_cast<size_t>(s)] = slot.occupied;
        dest.midi[static_cast<size_t>(s)] = slot.occupied ? slot.midiNote : 36;
        dest.velocity[static_cast<size_t>(s)] = slot.velocity;
        if (slot.occupied)
            ++n;
    }
    dest.valid = n >= 2;
    if (dest.valid)
    {
        for (int s = 0; s < kGridSlots; ++s)
        {
            const auto& slot = gridSlots_[static_cast<size_t>(s)];
            if (!slot.occupied || !slot.onset)
                continue;
            uint8_t g = 1;
            for (int t = s + 1; t < kGridSlots; ++t)
            {
                const auto& next = gridSlots_[static_cast<size_t>(t)];
                if (!next.occupied || next.onset)
                    break;
                ++g;
            }
            dest.gate16[static_cast<size_t>(s)] = g;
        }
        return;
    }
    if (!locked_ || patternLen_ < 2)
        return;

    dest.occupied.fill(false);
    dest.midi.fill(36);
    dest.gate16.fill(0);
    dest.velocity.fill(0);
    dest.lenBeats = static_cast<double>(kGridBars) * 4.0;
    n = 0;
    for (int i = 0; i < patternLen_; ++i)
    {
        double beat = pattern_[static_cast<size_t>(i)].beatOffset;
        beat = std::fmod(beat, dest.lenBeats);
        if (beat < 0.0)
            beat += dest.lenBeats;
        int slot = static_cast<int>(std::floor(beat * 4.0 + 1.0e-9));
        if (slot < 0)
            slot = 0;
        if (slot >= kGridSlots)
            slot = kGridSlots - 1;
        if (dest.occupied[static_cast<size_t>(slot)])
            continue;
        dest.occupied[static_cast<size_t>(slot)] = true;
        dest.midi[static_cast<size_t>(slot)] = pattern_[static_cast<size_t>(i)].midiNote;
        dest.gate16[static_cast<size_t>(slot)] = 1;
        dest.velocity[static_cast<size_t>(slot)] = 96;
        ++n;
    }
    dest.valid = n >= 2;
}

bool PhraseLearner::loadPattern(const LearnedRiff& src) noexcept
{
    if (!src.valid)
        return false;

    bool anyGate = false;
    for (int s = 0; s < kGridSlots; ++s)
    {
        if (src.gate16[static_cast<size_t>(s)] > 0)
        {
            anyGate = true;
            break;
        }
    }

    gridSlots_.fill({});
    gridOccupied_ = 0;
    patternLen_ = 0;
    const int nSlots = std::min(kGridSlots, kMaxPattern);
    for (int s = 0; s < nSlots; ++s)
    {
        if (!src.occupied[static_cast<size_t>(s)])
            continue;
        auto& slot = gridSlots_[static_cast<size_t>(s)];
        slot.occupied = true;
        slot.midiNote = src.midi[static_cast<size_t>(s)];
        slot.velocity = src.velocity[static_cast<size_t>(s)] > 0
                            ? src.velocity[static_cast<size_t>(s)] : uint8_t{ 96 };
        slot.gate16 = anyGate ? src.gate16[static_cast<size_t>(s)] : uint8_t{ 1 };
        slot.onset = anyGate ? (src.gate16[static_cast<size_t>(s)] > 0) : true;
        ++gridOccupied_;
        pattern_[static_cast<size_t>(patternLen_)].beatOffset =
            static_cast<double>(s) * 0.25;
        pattern_[static_cast<size_t>(patternLen_)].midiNote = slot.midiNote;
        ++patternLen_;
    }
    if (patternLen_ < 2)
        return false;

    patternLenBeats_ = src.lenBeats > 0.0 ? src.lenBeats
                                          : static_cast<double>(kGridBars) * 4.0;
    userCapturing_ = false;
    gridCapturing_ = false;
    gridListening_ = false;
    locked_ = true;
    state_ = State::Locked;
    playbackPhase_ = 0.0;
    playbackStep_ = 0;
    return true;
}

void PhraseLearner::setMatchReference(const LearnedRiff& src) noexcept
{
    matchPattern_ = {};
    matchLen_ = 0;
    matchLenBeats_ = src.lenBeats > 0.0 ? src.lenBeats
                                        : static_cast<double>(kGridBars) * 4.0;
    if (!src.valid)
        return;
    bool anyOnset = false;
    for (int s = 0; s < kGridSlots; ++s)
        if (src.gate16[static_cast<size_t>(s)] > 0)
            anyOnset = true;
    const int nSlots = std::min(kGridSlots, kMaxPattern);
    for (int s = 0; s < nSlots && matchLen_ < kMaxPattern; ++s)
    {
        if (!src.occupied[static_cast<size_t>(s)])
            continue;
        // Sustain slots are 16ths apart; matching those IOIs lets a held note's
        // block-RMS wobble look like "the same riff" (T6.2 false cut-short).
        if (anyOnset && src.gate16[static_cast<size_t>(s)] == 0)
            continue;
        matchPattern_[static_cast<size_t>(matchLen_)].beatOffset =
            static_cast<double>(s) * 0.25;
        matchPattern_[static_cast<size_t>(matchLen_)].midiNote =
            src.midi[static_cast<size_t>(s)];
        ++matchLen_;
    }
}

void PhraseLearner::coalesceSlotGates() noexcept
{
    for (int s = 0; s < kGridSlots; ++s)
    {
        auto& slot = gridSlots_[static_cast<size_t>(s)];
        if (!slot.occupied || !slot.onset)
        {
            slot.gate16 = 0;
            continue;
        }
        uint8_t g = 1;
        for (int t = s + 1; t < kGridSlots; ++t)
        {
            const auto& next = gridSlots_[static_cast<size_t>(t)];
            if (!next.occupied || next.onset)
                break;
            ++g;
        }
        slot.gate16 = g;
    }
}

PhraseLearner::BassNote PhraseLearner::process(int64_t sampleTime, float rms, float pitchMidi,
                                                float pitchConf, float bpm, int numSamples,
                                                int stablePitchClassOffset, float hfFlux) noexcept
{
    BassNote result;
    result.trigger = false;
    result.midiNote = 36;
    result.velocity = 0.58f;

    // Silence detection — disabled during user capture so gaps between riff
    // notes do not wipe the take, and disabled while the groove lock is holding
    // the learned riff so a breath/pause mid-lock never drops the frozen riff
    // (the "locked riff doesn't persist" bug). Outside a hold, ~2 s of quiet
    // still resets the learner back to follow mode.
    if (rms < 0.003f && !userCapturing_ && !holdActive_)
    {
        silentSamples_ += numSamples;
        if (silentSamples_ > silenceResetSamples_)
        {
            reset();
            return result;
        }
    }
    else
    {
        silentSamples_ = 0;
    }

    // Minimum interval between attacks. The envelope is advanced on EVERY block
    // (otherwise the frozen prevRms_/rmsSmooth_ made the first block after the
    // gate read as a rise and the mirror machine-gunned); the gate only decides
    // *when* a latched edge is accepted, so a fast attack whose whole rise fits
    // inside the gate is not lost.
    const AttackVerdict verdict = attackDetector.classify(rms, sampleTime, hfFlux);
    const bool attack = verdict.accepted;
    attackDetectedThisHop_ = attack;

    // Hold the last confidently-estimated pitch: YIN confidence collapses at
    // loud/quiet transitions, so attacks there use the held pitch instead of
    // being dropped (the note itself is the same — only the estimate wavers).
    if (pitchConf > 0.3f)
    {
        lastGoodPitchMidi_ = pitchMidi;
        lastGoodPitchValid_ = true;
    }

    // Attacks are gated on the RMS transient ONLY — never on pitch confidence.
    // On distorted palm-mute guitar YIN's confidence is bimodal (≈0 at most
    // attack moments), so a pitch gate starved the learner to a few attacks per
    // minute and the mirror played a skeletal riff. The riff mirror is a rhythm
    // mirror; the note value comes from the held/confident pitch when available
    // and is corrected later by the live-root retune.
    const float attackPitch = (pitchConf > 0.05f) ? pitchMidi : lastGoodPitchMidi_;
    const int attackBassNote = resolveBassNote(attackPitch, stablePitchClassOffset);

    // Edge-triggered "following the riff": recomputed on every block.
    justMatched_ = false;
    justMatchedRef_ = false;

    if (attack)
    {
        // The detector has already consumed the decay→rise edge and the trough
        // (see AttackDetector::classify).

        // Record attack
        attacks_[attackWrite_].sample = sampleTime;
        attacks_[attackWrite_].pitch = static_cast<float>(attackBassNote);
        attackWrite_ = (attackWrite_ + 1) % kMaxAttacks;
        if (attackCount_ < kMaxAttacks)
            ++attackCount_;

        // "Following the riff": the attack landed on the learned riff's grid —
        // within a quarter beat of ANY note-to-note interval (cycled). Matching
        // any interval is robust for uniform chugs, whose bar-aligned pattern
        // under-samples the bar. Computed here (before the state machine) so the
        // live mirror below can distinguish a riff note from a solo lick.
        bool matched = false;
        bool matchedRef = false;
        if (attackCount_ >= 2)
        {
            const int prevIdx = (attackWrite_ - 2 + kMaxAttacks) % kMaxAttacks;
            const int curIdx = (attackWrite_ - 1 + kMaxAttacks) % kMaxAttacks;
            const int64_t recentIoi = attacks_[curIdx].sample - attacks_[prevIdx].sample;
            const double bpmSafe = (bpm > 0.0) ? static_cast<double>(bpm) : 120.0;
            const double quarterBeatSamples = 0.25 * (60.0 / bpmSafe) * sampleRate_;
            auto matchIoi = [&](const PatternNote* notes, int len, double lenBeats) noexcept -> bool
            {
                if (notes == nullptr || len < 2)
                    return false;
                for (int i = 0; i < len; ++i)
                {
                    const int next = (i + 1) % len;
                    double ioiBeats = notes[next].beatOffset - notes[i].beatOffset;
                    if (ioiBeats <= 0.0)
                        ioiBeats += lenBeats;
                    const double ioiSamples = ioiBeats * (60.0 / bpmSafe) * sampleRate_;
                    if (std::abs(static_cast<double>(recentIoi) - ioiSamples) <= quarterBeatSamples)
                        return true;
                }
                return false;
            };
            matched = matchIoi(pattern_.data(), patternLen_, patternLenBeats_);
            // T6.2: same-riff cut-short. Ignore sub-16th IOIs — a held note's
            // block-RMS wobble retriggers at kMinAttackInterval (~40 ms) and
            // would otherwise match sustain-adjacent 16ths of the same pitch.
            const double minRiffIoiSamples = 0.2 * (60.0 / bpmSafe) * sampleRate_;
            if (matchLen_ >= 2 && static_cast<double>(recentIoi) >= minRiffIoiSamples
                && matchIoi(matchPattern_.data(), matchLen_, matchLenBeats_))
            {
                const int pc = ((attackBassNote % 12) + 12) % 12;
                for (int i = 0; i < matchLen_ && !matchedRef; ++i)
                {
                    const int rpc = ((matchPattern_[static_cast<size_t>(i)].midiNote % 12) + 12) % 12;
                    if (rpc == pc)
                        matchedRef = true;
                }
            }
        }
        following_ = matched;
        justMatched_ = matched;
        justMatchedRef_ = matchedRef;

        if (locked_ && !following_)
        {
            if (mismatchStartSample_ < 0)
                mismatchStartSample_ = sampleTime;
        }
        else if (following_)
        {
            mismatchStartSample_ = -1;
        }

        // Live riff mirror: follow each attack while Learning (pre-lock).
        // Locked playback of a snapshot is the processor's job (RiffA / RiffBLocked).
        // Active capture is silent accompaniment — no bass until commit.
        if ((state_ != State::Locked || liveMirrorWhenLocked_) && !userCapturing_ && !gridCapturing_)
        {
            result.trigger = true;
            result.midiNote = attackBassNote;
            result.velocity = bassVelocityForRms(rms);
        }
    }

    // State machine
    switch (state_)
    {
        case State::Learning:
        {
            // Active grid capture records occupancy, not attack slices. Passive
            // listen leaves auto-lock running (it stamps the grid in parallel).
            // Play disables auto-lock so a leftover riff cannot freeze bass.
            if (userCapturing_ || gridCapturing_ || !autoLockEnabled_)
                break;

            // T6.3: lock on the first 2-bar repeat (`patternsMatch` already
            // requires attackCount_ >= len * 2). Four attacks is two notes
            // repeated twice — enough to pin a figure without waiting for 8.
            if (attackCount_ >= 4)
            {
                const int maxLen = std::min({ kMaxPattern, 16, attackCount_ / 2 });
                for (int len = maxLen; len >= 2; --len)
                {
                    if (patternsMatch(len, bpm))
                    {
                        patternLen_ = len;
                        lockPattern(bpm, sampleTime);
                        break;
                    }
                }
            }
            break;
        }

        case State::Locked:
        {
            // Advance playback position
            const double beatsPerSample = bpm / 60.0 / sampleRate_;
            const double blockBeats = static_cast<double>(numSamples) * beatsPerSample;
            const double prevPhase = playbackPhase_;
            playbackPhase_ += blockBeats;

            // Wrap around pattern
            if (playbackPhase_ >= patternLenBeats_)
            {
                playbackPhase_ = std::fmod(playbackPhase_, patternLenBeats_);
                playbackStep_ = 0;  // Reset to first note
            }

            // Check if we should trigger the next note from the frozen snapshot.
            if (playbackStep_ < patternLen_)
            {
                const double noteOffset = pattern_[playbackStep_].beatOffset;

                // Did we cross this note's trigger point?
                bool crossed = false;
                if (prevPhase <= noteOffset && playbackPhase_ > noteOffset)
                    crossed = true;
                // Handle wrap-around case
                if (playbackPhase_ < prevPhase && noteOffset < playbackPhase_)
                    crossed = true;

                if (crossed)
                {
                    result.trigger = true;
                    result.midiNote = pattern_[playbackStep_].midiNote;
                    result.velocity = bassVelocityForRms(rms);
                    ++playbackStep_;
                    lastTriggerSample_ = sampleTime;
                }
            }

            // Drift handling: rhythm no longer fits → unlock and re-learn.
            // Hold still blocks an immediate unlock, but T6.3 lets a stuck lock
            // escape after kDriftUnlockBars of non-matching attacks (no Forget).
            const double bpmSafe = (bpm > 0.0) ? static_cast<double>(bpm) : 120.0;
            const int64_t mismatchLimit = static_cast<int64_t>(
                static_cast<double>(kDriftUnlockBars) * 4.0 * (60.0 / bpmSafe) * sampleRate_);
            const bool mismatchLongEnough = mismatchStartSample_ >= 0
                && (sampleTime - mismatchStartSample_) >= mismatchLimit;
            if (attack && locked_ && attackCount_ >= 2 && patternLen_ >= 2
                && !following_
                && (!holdActive_ || mismatchLongEnough))
            {
                state_ = State::Learning;
                locked_ = false;
                patternLen_ = 0;
                mismatchStartSample_ = -1;
            }
            break;
        }
    }

    return result;
}
