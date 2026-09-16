/**
 * @file
 * @brief End-to-end bass-mirror diagnosis on REAL recorded guitar, in Play mode,
 *        through the full AccompanimentProcessor.
 *
 * Why: the mirror-vs-harmony arbitration is a ratio question, and every prior
 * attempt to answer it used synthetic sine input or the learner's internal
 * counters. This drives the real plugin over the real recordings and measures
 * how many bass note-ons came from each BassVoice producer, plus the attack
 * detector's rise-candidate outcome histogram. See `docs/BASS_MIRRORING.md` §6
 * and `RULES.md` §7.
 *
 * Offline agent harness (env-gated):
 *   MA_DI_WAV  + MA_DI_BPM   → Play-mode DI audit   ([di] tag)
 *   MA_RIFF_WAV + MA_RIFF_BPM → Record-riff dump    ([riff] tag)
 *
 * Harness contracts (do not weaken):
 * - Input must be **mono clean DI** (stem `01-*`). Stereo 02/03 stems fail hard.
 * - BPM env var is **required** (host tempo; never assume 120).
 * - Processor sample rate = WAV sample rate (no silent 44.1 vs 48 skew).
 * - Note dump includes BassVoice producer (Mirror/Frozen/Grid/…).
 * - Attack stats use rise-candidate outcomes — never compare to external
 *   spectral-flux onset rates; those are a different detector.
 *
 * The 10 s `tests/fixtures` excerpts are the *densest* windows of each raw take,
 * so they are the best case for the detector. The `data/raw` windows below are
 * the representative case: minutes of ordinary playing, sampled in 30 s blocks.
 */

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <JuceHeader.h>

#include "AccompanimentProcessor.h"
#include "fixtures/WavReader.h"

namespace
{
constexpr double kSr  = 44100.0;
constexpr int    kBlock = 512;
constexpr double kBpm = 120.0;

#if !defined(MA_REPO_ROOT)
#define MA_REPO_ROOT ""
#endif

std::string fixturePath(const char* name)
{
    return std::string(MA_REPO_ROOT) + "/tests/fixtures/" + name;
}

std::string rawPath(const char* name)
{
    return std::string(MA_REPO_ROOT) + "/data/raw/" + name;
}

struct PlayMirrorStats
{
    bool loaded = false;
    int  bassOns = 0;
    int  learned = 0;   // mirror / frozen snapshot
    int  grid = 0;      // authored / harmonic fallback
    int  drumOns = 0;
    double seconds = 0.0;
    PhraseLearner::AttackDebug attack{};
    std::vector<int> bassNotes;   // note numbers, in time order
    int blocks = 0;
    int audibleBlocks = 0;        // structure state != SILENT
    int silentBlocks = 0;
};

struct MovingPlayHead final : public juce::AudioPlayHead
{
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(bpm);
        info.setIsPlaying(true);
        info.setTimeInSamples(samples);
        return info;
    }
    int64_t samples = 0;
    double bpm = kBpm;
};

/** Run the real processor in Play mode over [startSample, startSample+numSamples). */
PlayMirrorStats runPlay(const WavReader::PcmMono& pcm, int64_t startSample, int64_t numSamples,
                        int blockSize = kBlock)
{
    PlayMirrorStats s;
    s.loaded = true;
    const double sr = (pcm.sampleRate > 0) ? static_cast<double>(pcm.sampleRate) : kSr;
    s.seconds = static_cast<double>(numSamples) / sr;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, blockSize);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));   // Rock
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(kBpm)));
    proc.setCustomSongForm("VERSE:64,CHORUS:32");
    proc.playActive.store(true, std::memory_order_release);

    MovingPlayHead ph;
    proc.setPlayHead(&ph);

    const int64_t end = startSample + numSamples;
    for (int64_t start = startSample; start + blockSize <= end; start += blockSize)
    {
        ph.samples = start;
        juce::AudioBuffer<float> buf(2, blockSize);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(buf.getWritePointer(ch),
                                              pcm.samples.data() + start, blockSize);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getVelocity() <= 0)
                continue;
            if (msg.getChannel() == 2) { ++s.bassOns; s.bassNotes.push_back(msg.getNoteNumber()); }
            else if (msg.getChannel() == 10) ++s.drumOns;
        }
        ++s.blocks;
        if (proc.getDisplayStateIndex() == 0) ++s.silentBlocks; else ++s.audibleBlocks;
    }

    s.learned = proc.getLearnedBassNoteCount();
    s.grid = proc.getGridBassNoteCount();
    s.attack = proc.getAttackDebug();

    proc.playActive.store(false, std::memory_order_release);
    proc.setPlayHead(nullptr);
    proc.releaseResources();
    return s;
}

void reportFixture(const char* name)
{
    WavReader::PcmMono pcm;
    if (!WavReader::readMonoWav(fixturePath(name), pcm))
    {
        std::printf("[PLAY-MIRROR] %-22s : MISSING fixture\n", name);
        return;
    }
    const PlayMirrorStats s = runPlay(pcm, 0, static_cast<int64_t>(pcm.samples.size()));
    const double ratio = (s.grid > 0) ? static_cast<double>(s.learned) / static_cast<double>(s.grid)
                                      : (s.learned > 0 ? 1.0e9 : 0.0);
    std::printf("[PLAY-MIRROR] %-22s %.1fs bassOns=%3d learned=%3d grid=%3d L/G=%.2f  rise=%lld blockedFloor=%lld acc=%lld\n",
                name, s.seconds, s.bassOns, s.learned, s.grid, ratio,
                static_cast<long long>(s.attack.riseEdges),
                static_cast<long long>(s.attack.blockedByFloor),
                static_cast<long long>(s.attack.accepted));
}

void reportRawWindows(const char* relPath, const char* label)
{
    WavReader::PcmMono pcm;
    if (!WavReader::readMonoWav(rawPath(relPath), pcm))
    {
        std::printf("[PLAY-MIRROR] raw %-16s : MISSING (%s)\n", label, relPath);
        return;
    }
    const int64_t window = static_cast<int64_t>(30.0 * pcm.sampleRate);
    const int64_t total = static_cast<int64_t>(pcm.samples.size());
    std::printf("[PLAY-MIRROR] raw %s  (%.0fs total, 30s windows)\n", label,
                static_cast<double>(total) / pcm.sampleRate);
    for (int64_t start = 0; start + window <= total; start += window)
    {
        const PlayMirrorStats s = runPlay(pcm, start, window);
        const double ratio = (s.grid > 0) ? static_cast<double>(s.learned) / static_cast<double>(s.grid)
                                          : (s.learned > 0 ? 1.0e9 : 0.0);
        std::printf("[PLAY-MIRROR]   @%4llds learned=%3d grid=%3d L/G=%.2f audible=%3d%%  rise=%4lld blockedFloor=%4lld acc=%3lld\n",
                    static_cast<long long>(start / pcm.sampleRate),
                    s.learned, s.grid, ratio,
                    (s.blocks > 0) ? (100 * s.audibleBlocks / s.blocks) : 0,
                    static_cast<long long>(s.attack.riseEdges),
                    static_cast<long long>(s.attack.blockedByFloor),
                    static_cast<long long>(s.attack.accepted));
    }

    // Pitch histogram over the first window: if the mirror is following the
    // playing, a single-note line and an open-chord passage must show movement.
    const PlayMirrorStats first = runPlay(pcm, 0, window);
    int lo = 127, hi = 0;
    std::vector<int> hist(128, 0);
    for (int n : first.bassNotes) { hist[static_cast<size_t>(n)]++; lo = std::min(lo, n); hi = std::max(hi, n); }
    int distinct = 0;
    for (int c : hist) if (c > 0) ++distinct;
    std::printf("[PLAY-MIRROR]   %s pitch: distinct=%d range=%d..%d hist=", label, distinct,
                first.bassNotes.empty() ? -1 : lo, hi);
    for (int n = 0; n < 128; ++n)
        if (hist[static_cast<size_t>(n)] > 0)
            std::printf("%d:%d ", n, hist[static_cast<size_t>(n)]);
    std::printf("\n");
}
} // namespace

// Diagnostic table over the 10 s fixtures — never fails, it measures.
TEST_CASE("bass mirror: real-audio Play-mode producer split (fixtures)",
          "[integration][bass][mirror][realaudio][diagnostic]")
{
    reportFixture("palm_mute_chug.wav");
    reportFixture("thrash_chug.wav");
    reportFixture("open_chord_passage.wav");
    reportFixture("single_note_run.wav");
    SUCCEED("fixture measurement ran");
}

// Diagnostic table over the full raw takes — the representative case.
TEST_CASE("bass mirror: real-audio Play-mode producer split (raw takes)",
          "[integration][bass][mirror][realaudio][diagnostic]")
{
    reportRawWindows("palm_mute/palm_mute.wav", "palm_mute");
    reportRawWindows("palm_mute/palm_mute2.wav", "palm_mute2");
    reportRawWindows("open_chord/open_chord.wav", "open_chord");
    reportRawWindows("single_note/single_note.wav", "single_note");
    reportRawWindows("sustain/sustain.wav", "sustain");
    reportRawWindows("open_chord/open_chord2.wav", "open_chord2");
    reportRawWindows("single_note/single_notes2.wav", "single_note2");
    SUCCEED("raw measurement ran");
}

// Guard: the emitted mirror must not depend on the host block size. The attack
// detector is driven at a fixed hop (independently of the host block), so the
// number of mirrored notes must be the same at 64 and 4096 samples per block.
// This was the root cause of the recurring "bass doesn't mirror" failures: the
// detector was called once per block with a single value from the last 20 ms.
TEST_CASE("bass mirror: the emitted mirror is host-buffer-size invariant",
          "[integration][bass][mirror][realaudio][bufferinvariance]")
{
    WavReader::PcmMono pcm;
    if (!WavReader::readMonoWav(rawPath("palm_mute/palm_mute.wav"), pcm))
    {
        SUCCEED("skipped");
        return;
    }
    const int64_t window = static_cast<int64_t>(30.0 * pcm.sampleRate);
    std::printf("[BS-SWEEP] block | learned grid L/G | rise blockedFloor acc\n");
    int ref = -1;
    int minLearned = -1;
    int maxLearned = -1;
    for (int bs : { 64, 128, 256, 512, 1024, 2048, 4096 })
    {
        const PlayMirrorStats s = runPlay(pcm, 0, window, bs);
        const double ratio = (s.grid > 0) ? static_cast<double>(s.learned) / static_cast<double>(s.grid)
                                          : (s.learned > 0 ? 1.0e9 : 0.0);
        std::printf("[BS-SWEEP] %5d | %7d %4d %5.2f | %5lld %12lld %4lld\n",
                    bs, s.learned, s.grid, ratio,
                    static_cast<long long>(s.attack.riseEdges),
                    static_cast<long long>(s.attack.blockedByFloor),
                    static_cast<long long>(s.attack.accepted));
        if (bs == 512)
            ref = s.learned;
        minLearned = (minLearned < 0) ? s.learned : std::min(minLearned, s.learned);
        maxLearned = std::max(maxLearned, s.learned);
    }

    REQUIRE(ref > 0);
    // 10% (plus a small absolute slack for edge hops) is generous next to the
    // ~5x collapse the once-per-block call produced.
    REQUIRE(maxLearned <= ref + ref / 10 + 4);
    REQUIRE(minLearned >= ref - ref / 10 - 4);
}

// Diagnostic: Record-riff mode over the same real take, 2 s buckets, so the
// producer split is visible through capture → locked riff → post-lock transition.
TEST_CASE("bass mirror: real-audio Record-riff producer timeline (diagnostic)",
          "[integration][bass][mirror][realaudio][diagnostic]")
{
    WavReader::PcmMono pcm;
    if (!WavReader::readMonoWav(rawPath("palm_mute/palm_mute.wav"), pcm))
    {
        std::printf("[REC-MIRROR] raw palm_mute : MISSING\n");
        SUCCEED("skipped");
        return;
    }

    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, kBlock);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(kBpm)));
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);   // 4-bar hold, testable
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(4.0f));

    MovingPlayHead ph;
    proc.setPlayHead(&ph);
    proc.requestRiffCaptureStart();

    const int64_t total = static_cast<int64_t>(pcm.samples.size());
    const int64_t bucket = static_cast<int64_t>(2.0 * pcm.sampleRate);
    int64_t nextBucket = bucket;
    int lastLearned = 0, lastGrid = 0;
    std::printf("[REC-MIRROR] t(s) | dLearned dGrid | capturing locked transition learnerLocked\n");
    for (int64_t start = 0; start + kBlock <= total; start += kBlock)
    {
        ph.samples = start;
        juce::AudioBuffer<float> buf(2, kBlock);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(buf.getWritePointer(ch),
                                              pcm.samples.data() + start, kBlock);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();

        if (start + kBlock >= nextBucket)
        {
            const int learned = proc.getLearnedBassNoteCount();
            const int grid = proc.getGridBassNoteCount();
            std::printf("[REC-MIRROR] %4lld | %8d %5d | %9s %6s %10s %13s\n",
                        static_cast<long long>(nextBucket / pcm.sampleRate),
                        learned - lastLearned, grid - lastGrid,
                        proc.isRiffCapturing() ? "yes" : "no",
                        proc.isGrooveLocked() ? "yes" : "no",
                        proc.isTransitionSectionActive() ? "yes" : "no",
                        proc.hasLearnedRiff() ? "yes" : "no");
            lastLearned = learned;
            lastGrid = grid;
            nextBucket += bucket;
        }
    }
    SUCCEED("record timeline ran");
}

// The user's exact scenario: record a riff, let the lock expire into the
// transition section, then play a DIFFERENT live riff. The bass must mirror
// what is being played now, not freeze onto a snapshot and go silent.
TEST_CASE("bass mirror: the transition mirrors a different live riff",
          "[integration][bass][mirror][realaudio][transition]")
{
    WavReader::PcmMono riffA, riffB;
    if (!WavReader::readMonoWav(rawPath("palm_mute/palm_mute.wav"), riffA)
        || !WavReader::readMonoWav(rawPath("single_note/single_notes2.wav"), riffB))
    {
        SUCCEED("skipped");
        return;
    }

    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, kBlock);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(kBpm)));
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);   // 4-bar hold
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(16.0f));

    MovingPlayHead ph;
    proc.setPlayHead(&ph);
    proc.requestRiffCaptureStart();

    int64_t host = 0;
    int64_t srcPos = 0;
    auto feed = [&](const WavReader::PcmMono* pcm, int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            ph.samples = host;
            juce::AudioBuffer<float> buf(2, kBlock);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                for (int i = 0; i < kBlock; ++i)
                {
                    if (pcm == nullptr || pcm->samples.empty())
                        p[i] = 0.0f;
                    else
                        p[i] = pcm->samples[static_cast<size_t>((srcPos + i) % pcm->samples.size())];
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            srcPos += kBlock;
            host += kBlock;
        }
    };

    // 1) Record riff A, then let it lock.
    {
        int guard = 0;
        while (!proc.isGrooveLocked() && guard < static_cast<int>(25.0 * kSr / kBlock))
        {
            feed(&riffA, 1);
            ++guard;
        }
    }
    REQUIRE(proc.isGrooveLocked());

    // 2) Feed silence until the lock expires into the transition section.
    int guard = 0;
    while (!proc.isTransitionSectionActive()
           && guard < static_cast<int>(60.0 * kSr / kBlock))
    {
        feed(nullptr, 1);
        ++guard;
    }
    REQUIRE(proc.isTransitionSectionActive());

    // 3) Now play a different live riff through the transition, per second.
    srcPos = static_cast<int64_t>(5.0 * kSr);   // skip the take's quiet lead-in
    const int learnedBefore = proc.getLearnedBassNoteCount();
    const int grid0 = proc.getGridBassNoteCount();
    const int blocks = static_cast<int>(6.0 * kSr / kBlock);
    for (int s = 0; s < 6; ++s)
    {
        const int l0 = proc.getLearnedBassNoteCount();
        const int g0 = proc.getGridBassNoteCount();
        const auto a0 = proc.getAttackDebug();
        feed(&riffB, static_cast<int>(kSr / kBlock));
        const auto a1 = proc.getAttackDebug();
        std::printf("[TRANS] s=%d state=%d learned=%d grid=%d attacks=%lld locked=%d\n",
                    s, proc.getDisplayStateIndex(),
                    proc.getLearnedBassNoteCount() - l0,
                    proc.getGridBassNoteCount() - g0,
                    static_cast<long long>(a1.accepted - a0.accepted),
                    proc.hasLearnedRiff() ? 1 : 0);
    }
    const int learned = proc.getLearnedBassNoteCount() - learnedBefore;
    const int grid = proc.getGridBassNoteCount() - grid0;
    const double secs = static_cast<double>(blocks) * kBlock / kSr;
    std::printf("[TRANS-MIRROR] during transition: learned=%d (%.1f/s) grid=%d\n",
                learned, static_cast<double>(learned) / secs, grid);

    // The transition section must MIRROR the live player: the drums may commit
    // to a contrast section, but the bass follows the player. Was 3 notes (0.5/s)
    // when the contrast riff froze the bass; now ~110 (18/s).
    REQUIRE(learned >= 40);

    proc.setPlayHead(nullptr);
    proc.releaseResources();
}

// Contract: on real playing in Play mode, the bass is the mirror. The harmony
// fallback may fill gaps, but the mirror must be the majority of what is played.
TEST_CASE("bass mirror: real playing in Play is mirror-primary",
          "[integration][bass][mirror][realaudio]")
{
    WavReader::PcmMono pcm;
    if (!WavReader::readMonoWav(fixturePath("palm_mute_chug.wav"), pcm))
    {
        WARN("palm_mute_chug.wav missing; skipping");
        return;
    }
    const PlayMirrorStats s = runPlay(pcm, 0, static_cast<int64_t>(pcm.samples.size()));

    INFO("learned=" << s.learned << " grid=" << s.grid << " bassOns=" << s.bassOns
                    << " over " << s.seconds << "s"
                    << " riseEdges=" << s.attack.riseEdges
                    << " blockedByFloor=" << s.attack.blockedByFloor
                    << " accepted=" << s.attack.accepted);
    REQUIRE(s.learned > 0);
    REQUIRE(s.learned > s.grid);
}

// Offline audit of a user-supplied clean DI.
//
// Required env:
//   MA_DI_WAV  = path to mono clean DI (stem 01-*). Never 02/03 output stems.
//   MA_DI_BPM  = host tempo used when the take was recorded (required; no 120 default).
//
// Run:
//   MA_DI_WAV=/path/to/01-fairo_di-….wav MA_DI_BPM=85 \
//     ./build/MetalAccompanimentIntegrationTests "[di]"
//
// Do NOT compare accepted-attack rate to an external spectral-flux onset
// detector — that mismatch previously caused circular AttackDetector retunes.
TEST_CASE("bass mirror: offline audit of a supplied DI",
          "[integration][bass][mirror][di]")
{
    const char* path = std::getenv("MA_DI_WAV");
    if (path == nullptr || *path == '\0')
    {
        SUCCEED("MA_DI_WAV unset — offline DI audit skipped");
        return;
    }
    const char* bpmEnv = std::getenv("MA_DI_BPM");
    REQUIRE(bpmEnv != nullptr);
    REQUIRE(*bpmEnv != '\0');
    const double bpm = std::atof(bpmEnv);
    REQUIRE(bpm >= 40.0);
    REQUIRE(bpm <= 300.0);

    WavReader::PcmMono pcm;
    std::string wavErr;
    const bool wavOk = WavReader::readMonoWav(path, pcm, &wavErr);
    INFO("WavReader: " << wavErr);
    REQUIRE(wavOk);
    REQUIRE_FALSE(pcm.samples.empty());
    REQUIRE(pcm.sampleRate > 0);

    const double sr = static_cast<double>(pcm.sampleRate);
    const int block = kBlock;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(bpm)));
    // MA_DI_FORM lets the audit reproduce the take's real song form (e.g.
    // "VERSE:8,CHORUS:8,VERSE:8,CHORUS:8") so Step-2 section recall is
    // actually exercised. Default keeps the old single-section behaviour.
    const char* formEnv = std::getenv("MA_DI_FORM");
    proc.setCustomSongForm((formEnv != nullptr && *formEnv != '\0') ? formEnv : "VERSE:128");
    proc.playActive.store(true, std::memory_order_release);

    MovingPlayHead ph;
    ph.bpm = bpm;
    proc.setPlayHead(&ph);

    struct Note
    {
        int64_t sample = 0;
        int midi = 0;
        float vel = 0.0f;
        BassVoice::Producer producer = BassVoice::Producer::None;
        bool held = false;
    };
    std::vector<Note> notes;
    auto producerName = [](BassVoice::Producer p) -> const char*
    {
        switch (p)
        {
            case BassVoice::Producer::Mirror:       return "Mirror";
            case BassVoice::Producer::Frozen:       return "Frozen";
            case BassVoice::Producer::GridAuthored: return "GridAuthored";
            case BassVoice::Producer::GridHarmonic: return "GridHarmonic";
            case BassVoice::Producer::Pickup:       return "Pickup";
            default:                                return "None";
        }
    };
    auto blockedName = [](int idx) -> const char*
    {
        switch (idx)
        {
            case 0: return "None(accepted)";
            case 1: return "NoRecentFall";
            case 2: return "NoSharpRise";
            case 3: return "TroughTooShallow";
            case 4: return "BelowAmplitudeFloor";
            case 5: return "NotTransient";
            case 6: return "MinIntervalGate";
            default: return "?";
        }
    };

    const int64_t total = static_cast<int64_t>(pcm.samples.size());
    const double durSec = static_cast<double>(total) / sr;
    int64_t nextSec = static_cast<int64_t>(sr);
    int64_t playSectionStart = -1;
    int lastMirror = 0, lastFrozen = 0, lastGridA = 0, lastGridH = 0;
    int drumOnsThisSecond = 0;
    auto lastAtt = PhraseLearner::AttackDebug{};

    std::printf("[DI-AUDIT] === clean-DI Play audit ===\n");
    std::printf("[DI-AUDIT] contract: mono clean DI only (01-*). Plugin sits before FX.\n");
    std::printf("[DI-AUDIT] do-not: compare rates to external spectral-flux onset tools.\n");
    std::printf("[DI-AUDIT] file=%s dur=%.1fs sr=%d bpm=%.1f block=%d\n",
                path, durSec, pcm.sampleRate, bpm, block);
    std::printf("[DI-AUDIT] t(s) | state Mir Fro GrA GrH attacks | drums pat phase\n");

    for (int64_t start = 0; start + block <= total; start += block)
    {
        ph.samples = start;
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
                p[i] = pcm.samples[static_cast<size_t>(start + i)];
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();

        if (playSectionStart < 0 && proc.getSectionPhase() == 1)
            playSectionStart = start;

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn() && m.getChannel() == 10 && m.getVelocity() > 0)
                ++drumOnsThisSecond;
            else if (m.isNoteOn() && m.getChannel() == 2 && m.getVelocity() > 0)
            {
                const int64_t abs = start + meta.samplePosition;
                BassVoice::Producer prod = BassVoice::Producer::None;
                bool held = false;
                BassVoice::NoteOn recent[32];
                const int nRecent = proc.getRecentBassNoteOns(recent, 32);
                for (int i = 0; i < nRecent; ++i)
                {
                    if (recent[static_cast<size_t>(i)].sample == abs
                        && recent[static_cast<size_t>(i)].midi == m.getNoteNumber())
                    {
                        prod = recent[static_cast<size_t>(i)].producer;
                        held = recent[static_cast<size_t>(i)].held;
                        break;
                    }
                }
                if (prod == BassVoice::Producer::None)
                    prod = proc.getLastBassProducer();
                notes.push_back({ abs, m.getNoteNumber(), m.getFloatVelocity(), prod, held });
            }
        }

        if (start + block >= nextSec)
        {
            const int mir = proc.getBassProducerCount(BassVoice::Producer::Mirror);
            const int fro = proc.getBassProducerCount(BassVoice::Producer::Frozen);
            const int gra = proc.getBassProducerCount(BassVoice::Producer::GridAuthored);
            const int grh = proc.getBassProducerCount(BassVoice::Producer::GridHarmonic);
            const auto att = proc.getAttackDebug();
            std::printf("[DI-AUDIT] %4lld | %5d %3d %3d %3d %3d %7lld | drums=%3d pat=%3d phase=%d sec=%s%s\n",
                        static_cast<long long>(nextSec / static_cast<int64_t>(sr)),
                        proc.getDisplayStateIndex(),
                        mir - lastMirror, fro - lastFrozen, gra - lastGridA, grh - lastGridH,
                        static_cast<long long>(att.accepted - lastAtt.accepted),
                        drumOnsThisSecond, proc.getDisplayPatternIndex(),
                        proc.getSectionPhase(),
                        proc.getCurrentSectionName().toRawUTF8(),
                        (playSectionStart >= 0
                         && nextSec / static_cast<int64_t>(sr)
                            == (playSectionStart / static_cast<int64_t>(sr)) + 1)
                            ? " (post-count-in)" : "");
            drumOnsThisSecond = 0;
            lastMirror = mir; lastFrozen = fro; lastGridA = gra; lastGridH = grh;
            lastAtt = att;
            nextSec += static_cast<int64_t>(sr);
        }
    }

    const auto dbg = proc.getAttackDebug();
    const int mir = proc.getBassProducerCount(BassVoice::Producer::Mirror);
    const int fro = proc.getBassProducerCount(BassVoice::Producer::Frozen);
    const int gra = proc.getBassProducerCount(BassVoice::Producer::GridAuthored);
    const int grh = proc.getBassProducerCount(BassVoice::Producer::GridHarmonic);
    const int pickup = proc.getBassProducerCount(BassVoice::Producer::Pickup);

    const double countInSec = (playSectionStart > 0)
        ? static_cast<double>(playSectionStart) / sr : 0.0;
    const double mirrorWindowSec = std::max(0.001, durSec - countInSec);
    int mirrorNotes = 0, frozenNotes = 0, gridNotes = 0, otherNotes = 0;
    for (const auto& n : notes)
    {
        if (n.producer == BassVoice::Producer::Mirror) ++mirrorNotes;
        else if (n.producer == BassVoice::Producer::Frozen) ++frozenNotes;
        else if (n.producer == BassVoice::Producer::GridAuthored
              || n.producer == BassVoice::Producer::GridHarmonic) ++gridNotes;
        else ++otherNotes;
    }

    std::printf("[DI-AUDIT] count-in ended at t=%.2fs (PlaySection); rates below exclude it where noted\n",
                countInSec);
    std::printf("[DI-AUDIT] producer totals: Mirror=%d Frozen=%d GridAuthored=%d GridHarmonic=%d Pickup=%d\n",
                mir, fro, gra, grh, pickup);
    std::printf("[DI-AUDIT] note-ons by producer (from provenance ring): Mirror=%d Frozen=%d Grid=%d other=%d\n",
                mirrorNotes, frozenNotes, gridNotes, otherNotes);
    std::printf("[DI-AUDIT] bass note-ons: %d over %.1fs full-file (%.2f/s); "
                "Mirror-only post-count-in ≈ %.2f/s over %.1fs\n",
                static_cast<int>(notes.size()), durSec,
                static_cast<double>(notes.size()) / durSec,
                static_cast<double>(mirrorNotes) / mirrorWindowSec, mirrorWindowSec);

    std::printf("[DI-AUDIT] detector legacy: rise=%lld clearedFloor=%lld blockedByFloor=%lld accepted=%lld\n",
                static_cast<long long>(dbg.riseEdges),
                static_cast<long long>(dbg.clearedFloor),
                static_cast<long long>(dbg.blockedByFloor),
                static_cast<long long>(dbg.accepted));
    std::printf("[DI-AUDIT] rise-candidate outcomes (USE THIS for pick diagnosis):\n");
    for (int i = 0; i < AttackDetector::AttackDebug::kBlockedCount; ++i)
        if (dbg.riseCandidateOutcome[i] > 0)
            std::printf("[DI-AUDIT]   %-20s %lld\n", blockedName(i),
                        static_cast<long long>(dbg.riseCandidateOutcome[i]));
    std::printf("[DI-AUDIT] every-call blockedBy (noisy; mostly quiet hops — do not treat as missed picks):\n");
    for (int i = 0; i < AttackDetector::AttackDebug::kBlockedCount; ++i)
        if (dbg.everyCallBlockedBy[i] > 0)
            std::printf("[DI-AUDIT]   %-20s %lld\n", blockedName(i),
                        static_cast<long long>(dbg.everyCallBlockedBy[i]));

    std::printf("[DI-AUDIT] stored section riffs: VERSE=%d CHORUS=%d INTRO=%d BREAKDOWN=%d SOLO=%d OUTRO=%d\n",
                proc.getStoredSectionRiffOccupiedCount("VERSE"),
                proc.getStoredSectionRiffOccupiedCount("CHORUS"),
                proc.getStoredSectionRiffOccupiedCount("INTRO"),
                proc.getStoredSectionRiffOccupiedCount("BREAKDOWN"),
                proc.getStoredSectionRiffOccupiedCount("SOLO"),
                proc.getStoredSectionRiffOccupiedCount("OUTRO"));
    std::printf("[DI-AUDIT] notes (t:s pitch vel producer held):\n");
    for (const auto& n : notes)
        std::printf("[DI-AUDIT]   %8.3f  %3d  %.2f  %-12s %s\n",
                    static_cast<double>(n.sample) / sr, n.midi,
                    static_cast<double>(n.vel), producerName(n.producer),
                    n.held ? "hold" : "gate");

    int hist[128] = {};
    for (const auto& n : notes)
        if (n.midi >= 0 && n.midi < 128) ++hist[n.midi];
    std::printf("[DI-AUDIT] pitch histogram:");
    for (int i = 0; i < 128; ++i)
        if (hist[i] > 0) std::printf(" %d:%d", i, hist[i]);
    std::printf("\n");

    proc.playActive.store(false, std::memory_order_release);
    proc.setPlayHead(nullptr);
    proc.releaseResources();
    SUCCEED("di audit ran");
}

namespace {

// Drive a short Play form on real DI audio, one fixture per section name, and
// report per-section BassVoice provenance. This is the Step 2 end-to-end guard:
// the first pass mirrors live (and captures), a return replays the stored riff.
struct RealSectionStat
{
    std::string name;
    int frozen = 0;
    int mirror = 0;
    std::set<int> frozenNotes;
};

std::vector<RealSectionStat> runPlayRealSections(const WavReader::PcmMono& verse,
                                                 const WavReader::PcmMono& chorus,
                                                 int block)
{
    std::vector<RealSectionStat> stats;
    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(kBpm)));
    proc.setCustomSongForm("VERSE:1,CHORUS:1,VERSE:1");
    {
        juce::AudioBuffer<float> warm(2, block);
        warm.clear();
        juce::MidiBuffer midi;
        proc.processBlock(warm, midi);
    }

    MovingPlayHead ph;
    proc.setPlayHead(&ph);
    proc.playActive.store(true, std::memory_order_release);

    size_t versePos = 0, chorusPos = 0;
    std::string cur;
    int prevFrozen = 0, prevMirror = 0;
    const int totalBlocks = static_cast<int>(13.0 * kSr / block);
    for (int b = 0; b < totalBlocks; ++b)
    {
        const auto name = proc.getCurrentSectionName().toStdString();
        if (proc.getSectionPhase() == 1 && name != cur && name != "Complete")
        {
            stats.push_back(RealSectionStat{});
            stats.back().name = name;
            cur = name;
        }

        const WavReader::PcmMono& src = (name == "CHORUS") ? chorus : verse;
        size_t& pos = (name == "CHORUS") ? chorusPos : versePos;
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
                p[i] = src.samples.empty()
                    ? 0.0f
                    : src.samples[(pos + static_cast<size_t>(i)) % src.samples.size()];
        }
        pos += static_cast<size_t>(block);

        ph.samples += block;
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();

        const int f = proc.getBassProducerCount(BassVoice::Producer::Frozen);
        const int m = proc.getBassProducerCount(BassVoice::Producer::Mirror);
        if (!stats.empty())
        {
            auto& s = stats.back();
            s.frozen += f - prevFrozen;
            s.mirror += m - prevMirror;
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2 && msg.getVelocity() > 0
                    && proc.getLastBassProducer() == BassVoice::Producer::Frozen)
                    s.frozenNotes.insert(msg.getNoteNumber());
            }
        }
        prevFrozen = f;
        prevMirror = m;
    }

    proc.playActive.store(false, std::memory_order_release);
    proc.setPlayHead(nullptr);
    proc.releaseResources();
    return stats;
}

} // namespace

// Step 2 §6.1/§6.3: first pass mirrors live (never silent) while capturing, and
// a return to the same section NAME replays the captured riff (Producer::Frozen).
TEST_CASE("bass mirror: Play learns a riff per section and replays it on return (real audio)",
          "[integration][bass][mirror][realaudio][step2]")
{
    WavReader::PcmMono verse, chorus;
    if (!WavReader::readMonoWav(rawPath("palm_mute/palm_mute.wav"), verse)
        || !WavReader::readMonoWav(fixturePath("single_note_run.wav"), chorus))
    {
        SUCCEED("skipped");
        return;
    }

    const auto stats = runPlayRealSections(verse, chorus, kBlock);
    REQUIRE(stats.size() >= 3);
    REQUIRE(stats[0].name == "VERSE");
    REQUIRE(stats[1].name == "CHORUS");
    REQUIRE(stats[2].name == "VERSE");

    // First pass: the live mirror is the bass, immediately (the "asap" contract).
    REQUIRE(stats[0].mirror > 0);
    REQUIRE(stats[0].frozen == 0);
    // The contrast section mirrors its own material too.
    REQUIRE(stats[1].mirror > 0);
    REQUIRE(stats[1].frozen == 0);
    // The return is the stored VERSE riff, not the live mirror.
    REQUIRE(stats[2].frozen > 0);
    REQUIRE(stats[2].mirror == 0);
    REQUIRE_FALSE(stats[2].frozenNotes.empty());
}

// Offline record-riff harness.
//
// Required env:
//   MA_RIFF_WAV = mono clean DI (01-*)
//   MA_RIFF_BPM = host tempo for the take (required)
//
//   MA_RIFF_WAV=/path/to/01-….wav MA_RIFF_BPM=85 \
//     ./build/MetalAccompanimentIntegrationTests "[riff]"
TEST_CASE("bass mirror: offline record-riff capture dump",
          "[integration][bass][mirror][riff]")
{
    const char* path = std::getenv("MA_RIFF_WAV");
    if (path == nullptr || *path == '\0')
    {
        SUCCEED("MA_RIFF_WAV unset — offline riff audit skipped");
        return;
    }
    const char* bpmEnv = std::getenv("MA_RIFF_BPM");
    REQUIRE(bpmEnv != nullptr);
    REQUIRE(*bpmEnv != '\0');
    const double bpm = std::atof(bpmEnv);
    REQUIRE(bpm >= 40.0);
    REQUIRE(bpm <= 300.0);

    WavReader::PcmMono pcm;
    std::string wavErr;
    const bool wavOk = WavReader::readMonoWav(path, pcm, &wavErr);
    INFO("WavReader: " << wavErr);
    REQUIRE(wavOk);
    REQUIRE_FALSE(pcm.samples.empty());
    REQUIRE(pcm.sampleRate > 0);

    const double sr = static_cast<double>(pcm.sampleRate);
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));   // Rock
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(bpm)));

    MovingPlayHead ph;
    ph.bpm = bpm;
    proc.setPlayHead(&ph);
    proc.requestRiffCaptureStart();

    auto producerName = [](BassVoice::Producer p) -> const char*
    {
        switch (p)
        {
            case BassVoice::Producer::Mirror:       return "Mirror";
            case BassVoice::Producer::Frozen:       return "Frozen";
            case BassVoice::Producer::GridAuthored: return "GridAuthored";
            case BassVoice::Producer::GridHarmonic: return "GridHarmonic";
            case BassVoice::Producer::Pickup:       return "Pickup";
            default:                                return "None";
        }
    };

    const int64_t total = static_cast<int64_t>(pcm.samples.size());
    int64_t start = 0;
    bool locked = false;
    int64_t lockStart = -1;
    struct Note { double t; int midi; float vel; BassVoice::Producer producer; bool held; };
    std::vector<Note> notes;

    std::printf("[RIFF] === clean-DI record-riff audit ===\n");
    std::printf("[RIFF] contract: mono clean DI only (01-*). Do not feed 02/03 stems.\n");

    for (; start + block <= total; start += block)
    {
        ph.samples = start;
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
                p[i] = pcm.samples[static_cast<size_t>(start + i)];
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn() && m.getChannel() == 2 && m.getVelocity() > 0)
            {
                const int64_t abs = start + meta.samplePosition;
                BassVoice::Producer prod = BassVoice::Producer::None;
                bool held = false;
                BassVoice::NoteOn recent[32];
                const int nRecent = proc.getRecentBassNoteOns(recent, 32);
                for (int i = 0; i < nRecent; ++i)
                {
                    if (recent[static_cast<size_t>(i)].sample == abs
                        && recent[static_cast<size_t>(i)].midi == m.getNoteNumber())
                    {
                        prod = recent[static_cast<size_t>(i)].producer;
                        held = recent[static_cast<size_t>(i)].held;
                        break;
                    }
                }
                if (prod == BassVoice::Producer::None)
                    prod = proc.getLastBassProducer();
                notes.push_back({ static_cast<double>(abs) / sr, m.getNoteNumber(),
                                  m.getFloatVelocity(), prod, held });
            }
        }
        if (!locked && proc.isGrooveLocked()) { locked = true; lockStart = start; }
    }

    const auto dbg = proc.getAttackDebug();
    std::printf("[RIFF] file=%s bpm=%.1f dur=%.1fs sr=%d locked=%d lockAt=%.2fs\n",
                path, bpm, static_cast<double>(total) / sr, pcm.sampleRate, locked ? 1 : 0,
                lockStart < 0 ? -1.0 : static_cast<double>(lockStart) / sr);
    std::printf("[RIFF] captured riff A: occupied=%d\n", proc.getRiffAOccupiedCount());
    std::printf("[RIFF] slots (idx:midi:gate):");
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
        if (proc.getRiffASlotOccupied(s))
            std::printf(" %d:%d:%d", s, proc.getRiffASlotMidi(s), proc.getRiffASlotGate(s));
    std::printf("\n");
    std::printf("[RIFF] producer totals: Mirror=%d Frozen=%d GridAuthored=%d GridHarmonic=%d\n",
                proc.getBassProducerCount(BassVoice::Producer::Mirror),
                proc.getBassProducerCount(BassVoice::Producer::Frozen),
                proc.getBassProducerCount(BassVoice::Producer::GridAuthored),
                proc.getBassProducerCount(BassVoice::Producer::GridHarmonic));
    std::printf("[RIFF] detector rise-candidate outcomes:\n");
    auto blockedName = [](int idx) -> const char*
    {
        switch (idx)
        {
            case 0: return "None(accepted)";
            case 1: return "NoRecentFall";
            case 2: return "NoSharpRise";
            case 3: return "TroughTooShallow";
            case 4: return "BelowAmplitudeFloor";
            case 5: return "NotTransient";
            case 6: return "MinIntervalGate";
            default: return "?";
        }
    };
    for (int i = 0; i < AttackDetector::AttackDebug::kBlockedCount; ++i)
        if (dbg.riseCandidateOutcome[i] > 0)
            std::printf("[RIFF]   %-20s %lld\n", blockedName(i),
                        static_cast<long long>(dbg.riseCandidateOutcome[i]));
    std::printf("[RIFF] bass note-ons (t pitch vel producer held) over %.1fs: %d\n",
                static_cast<double>(total) / sr, static_cast<int>(notes.size()));
    for (const auto& n : notes)
        std::printf("[RIFF]   %8.3f  %3d  %.2f  %-12s %s\n",
                    n.t, n.midi, static_cast<double>(n.vel),
                    producerName(n.producer), n.held ? "hold" : "gate");

    proc.playActive.store(false, std::memory_order_release);
    proc.setPlayHead(nullptr);
    proc.releaseResources();
    SUCCEED("riff dump ran");
}
