#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "analysis/PhraseLearner.h"

namespace
{
constexpr double kSr       = 48000.0;
constexpr float  kBpm      = 120.0f;
constexpr int    kBlock    = 512;
// Attack spacing: 7 blocks (6 quiet + 1 loud) = 3584 samples > 2000 min interval.
constexpr int    kBlocksPerAttack = 7;

/**
 * @brief Feed synthetic chug attacks until the learner locks.
 * Each attack cycle: 6 near-silent blocks then 1 loud block with @p pitch.
 * Returns the total number of blocks fed.
 */
int feedUntilLocked(PhraseLearner& learner, float pitch, int maxAttacks = 12)
{
    int blocks = 0;
    for (int a = 0; a < maxAttacks; ++a)
    {
        for (int b = 0; b < kBlocksPerAttack; ++b)
        {
            const bool loud = (b == kBlocksPerAttack - 1);
            const int64_t sampleTime = static_cast<int64_t>(blocks) * kBlock;
            const auto note = learner.process(
                sampleTime,
                loud ? 0.03f : 0.001f,
                loud ? pitch : 40.0f,
                loud ? 0.8f : 0.0f,
                kBpm, kBlock);
            (void)note;
            ++blocks;
            if (learner.isLocked())
                return blocks;
        }
    }
    return blocks;
}

/** @brief EnergyAnalyser-style 0.1 s RMS window over raw audio. */
class RmsWindow
{
public:
    explicit RmsWindow(double sampleRate)
        : win(static_cast<size_t>(static_cast<int>(0.1 * sampleRate)), 0.0f) {}

    float push(float s)
    {
        win[w] = s * s;
        w = (w + 1) % static_cast<int>(win.size());
        if (fill < static_cast<int>(win.size()))
            ++fill;
        return getRms();
    }

    /** @brief Current analyser-scaled RMS (×4, like EnergyAnalyser). */
    float getRms() const
    {
        float acc = 0.0f;
        for (int i = 0; i < fill; ++i)
            acc += win[static_cast<size_t>(i)];
        return (fill > 0) ? std::sqrt(acc / static_cast<float>(fill)) * 4.0f : 0.0f;
    }

private:
    std::vector<float> win;
    int w = 0;
    int fill = 0;
};

/**
 * @brief Feed a realistic palm-muted 16th chug (sharp attack + decay through
 *        the analyser's RMS window) until the learner locks.
 * Pitch confidence is fed as 0 (distorted palm-mute guitar collapses YIN
 * confidence) — the rhythm mirror must lock regardless of pitch.
 * Regression: the pitch-confidence gate starved attack recording, so the bass
 * never mirrored real riffs (2–6 s lock with a skeletal pattern).
 */
bool lockOnRealisticChug(PhraseLearner& learner)
{
    learner.prepare(kSr);
    RmsWindow win(kSr);
    const double sixteenth = 0.125;  // 16th at 120 BPM
    const int total = static_cast<int>(3.0 * kSr / kBlock);
    for (int blk = 0; blk < total; ++blk)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const int n = blk * kBlock + i;
            const double t = static_cast<double>(n) / kSr;
            const int ni = static_cast<int>(t / sixteenth);
            const double env = 0.3 + 0.7 * std::exp(-(t - ni * sixteenth) / 0.045);
            win.push(static_cast<float>(0.4 * env * std::sin(2.0 * 3.14159265358979 * 65.406 * t)));
        }
        const float rms = win.getRms();
        learner.process(static_cast<int64_t>(blk) * kBlock, rms, 36.0f, 0.0f, kBpm, kBlock);
        if (learner.isLocked())
            return true;
    }
    return false;
}

/**
 * @brief Feed a realistic chug and count immediate-mirror triggers before the
 *        lock — the bass must play each detected attack right away (follows as
 *        you play), not wait for the pattern to lock.
 */
int countImmediateTriggers(PhraseLearner& learner)
{
    learner.prepare(kSr);
    RmsWindow win(kSr);
    const double sixteenth = 0.125;
    const int total = static_cast<int>(3.0 * kSr / kBlock);
    int triggers = 0;
    for (int blk = 0; blk < total; ++blk)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const int n = blk * kBlock + i;
            const double t = static_cast<double>(n) / kSr;
            const int ni = static_cast<int>(t / sixteenth);
            const double env = 0.3 + 0.7 * std::exp(-(t - ni * sixteenth) / 0.045);
            win.push(static_cast<float>(0.4 * env * std::sin(2.0 * 3.14159265358979 * 65.406 * t)));
        }
        const float rms = win.getRms();
        const auto note = learner.process(static_cast<int64_t>(blk) * kBlock, rms, 36.0f, 0.0f, kBpm, kBlock);
        if (note.trigger)
            ++triggers;
        if (learner.isLocked())
            break;
    }
    return triggers;
}
} // namespace

TEST_CASE("PhraseLearner: locks a repeated riff after one repeat", "[phrase][bass]")
{
    PhraseLearner learner;
    learner.prepare(kSr);

    const int blocks = feedUntilLocked(learner, 36.0f);  // C2 chug

    REQUIRE(learner.isLocked());
    REQUIRE(learner.getAttackCount() >= 4);
    REQUIRE(learner.getPatternLength() >= 2);
    // Riff cycle is ~0.15 beats — must loop at the bar-aligned minimum of 4 beats.
    REQUIRE(learner.getPatternLenBeats() == 4.0);
    REQUIRE(blocks > 0);
}

TEST_CASE("PhraseLearner: locks on a realistic palm-muted 16th chug with zero pitch confidence", "[phrase][bass]")
{
    // Regression: the pitch-confidence gate starved attack recording on
    // distorted palm-mute guitar (YIN confidence ≈ 0 at attack moments), so the
    // bass never mirrored riffs — a 2–6 s lock with a skeletal 2-note pattern.
    // The rhythm mirror must lock regardless of pitch confidence.
    PhraseLearner learner;
    REQUIRE(lockOnRealisticChug(learner));
    REQUIRE(learner.getPatternLength() >= 2);
}

TEST_CASE("PhraseLearner: immediate mirror triggers bass notes before the lock", "[phrase][bass]")
{
    // The bass must follow each detected attack right away (attack-driven
    // mirror) instead of waiting for the pattern to lock — otherwise the bass
    // sits on the sparse beat-1 fallback for seconds.
    PhraseLearner learner;
    const int triggers = countImmediateTriggers(learner);
    REQUIRE(triggers >= 4);
    REQUIRE(learner.isLocked());  // and it still locks quickly afterwards
}

TEST_CASE("PhraseLearner: locked pattern pitch matches the chugged root (C2)", "[phrase][bass]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    (void)feedUntilLocked(learner, 36.0f);

    for (int i = 0; i < learner.getPatternLength(); ++i)
        REQUIRE(learner.getPatternNote(i) == 36);  // C2
}

TEST_CASE("PhraseLearner: locked bass does NOT change note while hold is active (R1)", "[phrase][bass][lock]")
{
    // P0/R1: while the groove lock holds, the bass must play the learned riff
    // note-for-note. The live-root retune is gone, so a root change by the
    // guitarist (chugging E2=40 instead of the locked C2=36) must NOT yank the
    // bass — every triggered note stays at the learned pitch.
    PhraseLearner learner;
    learner.prepare(kSr);
    const int lockedAt = feedUntilLocked(learner, 36.0f);  // lock on C2
    REQUIRE(learner.isLocked());
    REQUIRE(learner.getPatternNote(0) == 36);
    learner.setHoldActive(true);

    // Keep chugging the same rhythm, but now at E2 (40) — a different root.
    bool sawTrigger = false;
    int block = lockedAt;
    for (int a = 0; a < 12; ++a)
    {
        for (int b = 0; b < kBlocksPerAttack; ++b)
        {
            const bool loud = (b == kBlocksPerAttack - 1);
            const int64_t st = static_cast<int64_t>(block) * kBlock;
            const auto note = learner.process(
                st,
                loud ? 0.03f : 0.001f,
                loud ? 40.0f : 36.0f,
                loud ? 0.8f : 0.0f,
                kBpm, kBlock);
            if (note.trigger)
            {
                sawTrigger = true;
                REQUIRE(note.midiNote == 36);  // frozen — never the live 40
            }
            ++block;
        }
    }
    REQUIRE(sawTrigger);
    // The learned pattern itself is unchanged by the root change.
    for (int i = 0; i < learner.getPatternLength(); ++i)
        REQUIRE(learner.getPatternNote(i) == 36);
    REQUIRE(learner.isLocked());  // held → still locked
}

TEST_CASE("PhraseLearner: locked chug playback is dense (≥6 notes/s, 0.9.12 regression)", "[phrase][bass]")
{
    // Regression: the lock used to capture a 2-note slice, so the learned
    // playback was sparse staccato (1–2 notes/s). With full-riff capture +
    // loop-fill, a locked chug must sustain a dense pulse on its own.
    PhraseLearner learner;
    learner.prepare(kSr);
    const int lockedAt = feedUntilLocked(learner, 36.0f);
    REQUIRE(learner.isLocked());
    learner.setHoldActive(true);

    // Feed ~2 s of the same chug and count locked-playback triggers.
    const int blocks = static_cast<int>(2.0 * kSr / kBlock);
    int triggers = 0;
    int block = lockedAt;
    for (int i = 0; i < blocks; ++i)
    {
        const int pos = i % kBlocksPerAttack;
        const bool loud = (pos == kBlocksPerAttack - 1);
        const int64_t st = static_cast<int64_t>(block) * kBlock;
        const auto note = learner.process(
            st, loud ? 0.03f : 0.001f, 36.0f, loud ? 0.8f : 0.0f, kBpm, kBlock);
        if (note.trigger)
            ++triggers;
        ++block;
    }

    const double notesPerSec = static_cast<double>(triggers) / 2.0;
    REQUIRE(notesPerSec >= 6.0);
}

TEST_CASE("PhraseLearner: lock phase is aligned inside the loop (bar-aligned loop)", "[phrase][bass]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    (void)feedUntilLocked(learner, 36.0f);

    // Loop length is a whole number of bars.
    const double len = learner.getPatternLenBeats();
    REQUIRE(std::fmod(len, 4.0) == 0.0);
    REQUIRE(len >= 4.0);
    // Phase is a valid position inside the loop.
    const double phase = learner.getPlaybackPhase();
    REQUIRE(phase >= 0.0);
    REQUIRE(phase < len);
}

TEST_CASE("PhraseLearner: sustained tone does NOT lock (attack detector ignores slow ramps)", "[phrase][bass]")
{
    // Regression: the old delta trigger (any positive drift while loud) fired on
    // the RMS warm-up ramp of a sustained tone, so the learner locked onto a
    // held chord's transient and mirrored junk on bass.
    PhraseLearner learner;
    learner.prepare(kSr);

    // Constant tone: steady RMS, zero attacks.
    for (int i = 0; i < 300; ++i)
    {
        learner.process(static_cast<int64_t>(i) * kBlock, 0.2f, 36.0f, 0.9f, kBpm, kBlock);
        REQUIRE_FALSE(learner.isLocked());
    }

    // Slow swell (+2% per block): a relative rise far below a real attack's
    // sharpness (a chug jumps ×3–30) — must never be treated as an attack.
    PhraseLearner swell;
    swell.prepare(kSr);
    float rms = 0.02f;
    for (int i = 0; i < 400; ++i)
    {
        swell.process(static_cast<int64_t>(i) * kBlock, rms, 36.0f, 0.9f, kBpm, kBlock);
        rms = std::min(0.4f, rms * 1.02f);
        REQUIRE_FALSE(swell.isLocked());
    }
}

TEST_CASE("PhraseLearner: silence resets learning state", "[phrase][bass]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    (void)feedUntilLocked(learner, 36.0f);
    REQUIRE(learner.isLocked());

    // 200+ blocks of silence (kSilenceResetBlocks = 200) resets the learner.
    for (int i = 0; i < 210; ++i)
    {
        learner.process(static_cast<int64_t>(i) * kBlock, 0.0001f, 40.0f, 0.0f, kBpm, kBlock);
    }
    REQUIRE_FALSE(learner.isLocked());
    REQUIRE(learner.getPatternLength() == 0);
}

TEST_CASE("PhraseLearner: hold keeps the locked riff through silence (no reset)", "[phrase][bass][lock]")
{
    // Regression: the silence reset used to wipe a LOCKED riff during a breath,
    // so the bass fell back to live-mirroring the solo — "LOCKED RIFF DOESN'T
    // PERSIST". With the groove lock holding, ~2 s of quiet must NOT reset the
    // learner; the frozen riff keeps looping.
    PhraseLearner learner;
    learner.prepare(kSr);
    (void)feedUntilLocked(learner, 36.0f);
    REQUIRE(learner.isLocked());
    learner.setHoldActive(true);

    // 210 blocks of silence (> kSilenceResetBlocks = 200) — held, so no reset.
    for (int i = 0; i < 210; ++i)
        learner.process(static_cast<int64_t>(i) * kBlock, 0.0001f, 40.0f, 0.0f, kBpm, kBlock);

    REQUIRE(learner.isLocked());               // held → survives the silence
    REQUIRE(learner.getPatternLength() >= 2);  // pattern intact
}

TEST_CASE("PhraseLearner: hold suppresses drift-unlock (bass keeps the riff)", "[phrase][bass][lock]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    (void)feedUntilLocked(learner, 36.0f);
    REQUIRE(learner.isLocked());

    // Helper: one deviant attack = one quiet block then one loud block
    // (rising RMS so the attack detector fires), spaced ~1 beat apart.
    auto feedDeviantAttacks = [&](int64_t startSample, int count) {
        for (int i = 0; i < count; ++i)
        {
            const int64_t base = startSample + static_cast<int64_t>(i) * 47 * kBlock;
            learner.process(base, 0.04f, 40.0f, 0.8f, kBpm, kBlock);
            learner.process(base + kBlock, 0.09f, 40.0f, 0.8f, kBpm, kBlock);
        }
    };

    // Hold engaged (groove lock): a deviant rhythm (~1-beat spacing vs the
    // learned ~0.15-beat chug) must NOT unlock the mirror.
    learner.setHoldActive(true);
    feedDeviantAttacks(static_cast<int64_t>(1000) * kBlock, 4);
    REQUIRE(learner.isLocked());
    REQUIRE_FALSE(learner.isFollowingRiff());
    REQUIRE(learner.getPatternNote(0) == 36);  // still the learned C2 riff

    // Release the hold: the next deviant attack now unlocks the old riff
    // (the learner then re-learns the new rhythm — that is the listening mode).
    learner.setHoldActive(false);
    const int64_t releaseBase = static_cast<int64_t>(4000) * kBlock;
    learner.process(releaseBase, 0.04f, 40.0f, 0.8f, kBpm, kBlock);
    learner.process(releaseBase + kBlock, 0.09f, 40.0f, 0.8f, kBpm, kBlock);
    REQUIRE_FALSE(learner.isLocked());
}

TEST_CASE("PhraseLearner: isFollowingRiff fires while the riff is being played", "[phrase][bass][lock]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    const int lockedAt = feedUntilLocked(learner, 36.0f);
    REQUIRE(learner.isLocked());
    learner.setHoldActive(true);

    // Keep playing the same riff (same 7-block attack cycle) → the learner must
    // report matching attacks (justMatchedRiff) and follow.
    bool sawFollowing = false;
    int block = lockedAt;
    for (int a = 0; a < 6; ++a)
    {
        for (int b = 0; b < kBlocksPerAttack; ++b)
        {
            const bool loud = (b == kBlocksPerAttack - 1);
            const int64_t st = static_cast<int64_t>(block) * kBlock;
            learner.process(st, loud ? 0.03f : 0.001f,
                            loud ? 36.0f : 40.0f, loud ? 0.8f : 0.0f, kBpm, kBlock);
            ++block;
            if (learner.justMatchedRiff())
                sawFollowing = true;
        }
    }
    REQUIRE(sawFollowing);
    REQUIRE(learner.isFollowingRiff());
    REQUIRE(learner.isLocked());  // held → still locked
}

TEST_CASE("PhraseLearner: octave-flipped YIN still maps to C2 pitch class", "[phrase][bass]")
{
    // Distorted guitar YIN often reports C3 (48) for a C2 chug. The bass must
    // fold to pitch class on C2–B2, not follow the raw octave.
    PhraseLearner learner;
    learner.prepare(kSr);
    (void)feedUntilLocked(learner, 48.0f);  // C3
    REQUIRE(learner.isLocked());
    for (int i = 0; i < learner.getPatternLength(); ++i)
        REQUIRE(learner.getPatternNote(i) == 36);  // C2
}

TEST_CASE("PhraseLearner: learned bass velocity sits under the drums", "[phrase][bass]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    int blocks = 0;
    bool saw = false;
    for (int a = 0; a < 4 && !saw; ++a)
    {
        for (int b = 0; b < kBlocksPerAttack; ++b)
        {
            const bool loud = (b == kBlocksPerAttack - 1);
            const auto note = learner.process(
                static_cast<int64_t>(blocks) * kBlock,
                loud ? 0.03f : 0.001f,
                36.0f, loud ? 0.8f : 0.0f, kBpm, kBlock);
            ++blocks;
            if (note.trigger)
            {
                saw = true;
                REQUIRE(note.velocity <= 0.68f);
                REQUIRE(note.velocity >= 0.48f);
            }
        }
    }
    REQUIRE(saw);
}

TEST_CASE("PhraseLearner: user capture locks the whole recorded riff", "[phrase][bass][capture]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginUserCapture();
    REQUIRE(learner.isUserCapturing());
    REQUIRE_FALSE(learner.isLocked());

    // 10 attack cycles — the first rise may not count until a fall has armed
    // the detector, so we over-feed and require a full-riff lock (>= 8 notes).
    int blocks = 0;
    for (int a = 0; a < 10; ++a)
    {
        for (int b = 0; b < kBlocksPerAttack; ++b)
        {
            const bool loud = (b == kBlocksPerAttack - 1);
            const auto note = learner.process(
                static_cast<int64_t>(blocks) * kBlock,
                loud ? 0.03f : 0.001f,
                36.0f, loud ? 0.8f : 0.0f, kBpm, kBlock);
            REQUIRE_FALSE(note.trigger);  // silent accompaniment while recording
            REQUIRE_FALSE(learner.isLocked());
            ++blocks;
        }
    }
    REQUIRE(learner.getAttackCount() >= 8);
    REQUIRE(learner.commitUserCapture(kBpm, static_cast<int64_t>(blocks) * kBlock));
    REQUIRE(learner.isLocked());
    REQUIRE_FALSE(learner.isUserCapturing());
    REQUIRE(learner.getPatternLength() >= 8);
}

TEST_CASE("PhraseLearner: user capture with too few notes fails closed", "[phrase][bass][capture]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginUserCapture();
    learner.process(0, 0.03f, 36.0f, 0.8f, kBpm, kBlock);
    REQUIRE_FALSE(learner.commitUserCapture(kBpm, kBlock));
    REQUIRE_FALSE(learner.isLocked());
}

TEST_CASE("PhraseLearner: grid capture locks a full 4-bar 16th riff", "[phrase][bass][capture][grid]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginGridCapture();
    REQUIRE(learner.isUserCapturing());
    REQUIRE_FALSE(learner.isLocked());

    for (int slot = 0; slot < PhraseLearner::kGridSlots; ++slot)
    {
        const double t0 = static_cast<double>(slot) * 0.25;
        learner.stampGridRange(t0, t0 + 0.24, 0.2f, 36);
    }

    REQUIRE(learner.getGridOccupiedCount() == PhraseLearner::kGridSlots);
    REQUIRE(learner.commitGridCapture());
    REQUIRE(learner.isLocked());
    REQUIRE_FALSE(learner.isUserCapturing());
    REQUIRE(learner.getPatternLength() == PhraseLearner::kGridSlots);
    REQUIRE(learner.getPatternLenBeats() == 16.0);
    REQUIRE(learner.getPatternNote(0) == 36);
    REQUIRE(learner.getPatternNote(PhraseLearner::kGridSlots - 1) == 36);
}

TEST_CASE("PhraseLearner: grid capture with rests still loops at 4 bars", "[phrase][bass][capture][grid]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginGridCapture();

    // Downbeats only — 16 notes across 4 bars, lots of empty 16ths.
    for (int beat = 0; beat < 16; ++beat)
        learner.stampGridRange(static_cast<double>(beat),
                               static_cast<double>(beat) + 0.05, 0.2f, 40);

    REQUIRE(learner.getGridOccupiedCount() == 16);
    REQUIRE(learner.commitGridCapture());
    REQUIRE(learner.getPatternLength() == 16);
    REQUIRE(learner.getPatternLenBeats() == 16.0);
    REQUIRE(learner.getPatternNote(0) == 40);
}

TEST_CASE("PhraseLearner: rewindRiffToDownbeat resets a held riff to bar 1", "[phrase][bass][lock]")
{
    // The riff-loop determinism change keeps a recorded riff held (looping
    // silently) through a transition, so its internal phase keeps advancing. On
    // re-engage we must rewind it to bar 1 beat 1 so the bass comes back in on
    // the downbeat with the re-locked drums, not mid-riff.
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginGridCapture();
    for (int slot = 0; slot < PhraseLearner::kGridSlots; ++slot)
    {
        const double t0 = static_cast<double>(slot) * 0.25;
        learner.stampGridRange(t0, t0 + 0.24, 0.2f, 36);
    }
    REQUIRE(learner.commitGridCapture());
    REQUIRE(learner.isLocked());
    REQUIRE(learner.getPlaybackPhase() == 0.0);

    // Advance the loop clock ~2 beats (1s at 120 BPM) so the phase is mid-riff.
    (void)learner.process(48000, 0.0f, 36.0f, 0.0f, 120.0f, 48000, 0);
    REQUIRE(learner.getPlaybackPhase() > 1.0);

    // Rewind to the downbeat.
    learner.rewindRiffToDownbeat();
    REQUIRE(learner.getPlaybackPhase() == 0.0);

    // No-op when not locked — no crash, phase stays valid.
    PhraseLearner idle;
    idle.prepare(kSr);
    idle.rewindRiffToDownbeat();
    REQUIRE_FALSE(idle.isLocked());
}

TEST_CASE("PhraseLearner: empty grid capture fails closed", "[phrase][bass][capture][grid]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginGridCapture();
    REQUIRE_FALSE(learner.commitGridCapture());
    REQUIRE_FALSE(learner.isLocked());
}

TEST_CASE("PhraseLearner: live grid listen uses the same 16th occupancy lock", "[phrase][bass][capture][grid]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginLiveGridListen();
    // Passive listen is NOT active capture: gridCapturing_ stays false (so the
    // processor keeps auto-lock + fallback bass running); listening_ is set.
    REQUIRE_FALSE(learner.isGridCapturing());
    REQUIRE(learner.isGridListening());
    REQUIRE_FALSE(learner.isUserCapturing());

    for (int slot = 0; slot < PhraseLearner::kGridSlots; ++slot)
    {
        const double t0 = static_cast<double>(slot) * 0.25;
        learner.stampGridRange(t0, t0 + 0.24, 0.2f, 38);
        const auto note = learner.process(
            static_cast<int64_t>(slot) * kBlock, 0.03f, 38.0f, 0.8f, kBpm, kBlock);
        // Passive listen leaves the live mirror running (real picking still
        // plays bass) — but a low-RMS block below the attack floor triggers
        // nothing, so the mirror stays quiet here.
        REQUIRE_FALSE(learner.isLocked());
    }

    REQUIRE(learner.getGridOccupiedCount() == PhraseLearner::kGridSlots);
    REQUIRE(learner.commitGridCapture());
    REQUIRE(learner.isLocked());
    REQUIRE(learner.getPatternLenBeats() == 16.0);
    REQUIRE(learner.getPatternNote(0) == 38);
}

TEST_CASE("PhraseLearner: 2 silent bars + 2 occupied keep rests in slots 0-31", "[phrase][bass][capture][grid][rests]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginGridCapture();

    // Bars 3-4 occupied (slots 32-63); bars 1-2 silent (below stamp floor).
    learner.stampGridRange(0.0, 8.0, 0.004f, 36);
    for (int slot = 32; slot < PhraseLearner::kGridSlots; ++slot)
        learner.stampGridRange(static_cast<double>(slot) * 0.25,
                               static_cast<double>(slot) * 0.25 + 0.24, 0.2f, 40);

    REQUIRE(learner.getGridOccupiedCount() >= 32);
    REQUIRE(learner.getGridOccupiedCount() < 64);
    for (int s = 0; s < 32; ++s)
        REQUIRE_FALSE(learner.getGridSlotOccupied(s));

    REQUIRE(learner.commitGridCapture());
    REQUIRE(learner.getPatternLenBeats() == 16.0);

    PhraseLearner::LearnedRiff snap;
    learner.exportPattern(snap);
    REQUIRE(snap.valid);
    PhraseLearner restored;
    restored.prepare(kSr);
    REQUIRE(restored.loadPattern(snap));
    for (int s = 0; s < 32; ++s)
        REQUIRE_FALSE(restored.getGridSlotOccupied(s));
    REQUIRE(restored.getGridSlotOccupied(32));
}

TEST_CASE("T5.2: export/load preserves coalesced gates", "[phrase][bass][capture][t5.2]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginGridCapture();

    // Two-bar held chord: onset on slot 0, sustain through slot 7, then a rest,
    // then a second onset at slot 16 lasting 4 sixteenths.
    learner.stampGridRange(0.0, 0.24, 0.2f, 36, true);
    for (int s = 1; s < 8; ++s)
        learner.stampGridRange(static_cast<double>(s) * 0.25,
                               static_cast<double>(s) * 0.25 + 0.24, 0.18f, 36, false);
    learner.stampGridRange(4.0, 4.24, 0.2f, 40, true);
    for (int s = 17; s < 20; ++s)
        learner.stampGridRange(static_cast<double>(s) * 0.25,
                               static_cast<double>(s) * 0.25 + 0.24, 0.18f, 40, false);

    REQUIRE(learner.commitGridCapture());
    PhraseLearner::LearnedRiff snap;
    learner.exportPattern(snap);
    REQUIRE(snap.valid);
    REQUIRE(snap.gate16[0] == 8);
    for (int s = 1; s < 8; ++s)
        REQUIRE(snap.gate16[static_cast<size_t>(s)] == 0);
    REQUIRE(snap.gate16[16] == 4);
    for (int s = 17; s < 20; ++s)
        REQUIRE(snap.gate16[static_cast<size_t>(s)] == 0);

    PhraseLearner restored;
    restored.prepare(kSr);
    REQUIRE(restored.loadPattern(snap));
    PhraseLearner::LearnedRiff roundTrip;
    restored.exportPattern(roundTrip);
    REQUIRE(roundTrip.gate16[0] == 8);
    REQUIRE(roundTrip.gate16[16] == 4);
    REQUIRE(roundTrip.midi[0] == 36);
    REQUIRE(roundTrip.midi[16] == 40);
}

TEST_CASE("T5.2: 16th-note chug stamps one onset per slot", "[phrase][bass][capture][t5.2]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.beginGridCapture();
    for (int s = 0; s < 16; ++s)
        learner.stampGridRange(static_cast<double>(s) * 0.25,
                               static_cast<double>(s) * 0.25 + 0.24, 0.2f, 36, true);
    REQUIRE(learner.commitGridCapture());
    PhraseLearner::LearnedRiff snap;
    learner.exportPattern(snap);
    for (int s = 0; s < 16; ++s)
        REQUIRE(snap.gate16[static_cast<size_t>(s)] == 1);
}

TEST_CASE("PhraseLearner: setAutoLockEnabled(false) never auto-locks", "[phrase][bass][autolock]")
{
    PhraseLearner learner;
    learner.prepare(kSr);
    learner.setAutoLockEnabled(false);
    REQUIRE_FALSE(learner.isAutoLockEnabled());
    for (int i = 0; i < 64; ++i)
    {
        const bool loud = (i % 8) < 2;
        learner.process(static_cast<int64_t>(i) * kBlock,
                        loud ? 0.2f : 0.02f, 36.0f, 0.8f, kBpm, kBlock);
    }
    REQUIRE_FALSE(learner.isLocked());
}
