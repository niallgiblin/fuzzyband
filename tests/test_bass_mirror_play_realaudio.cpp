/**
 * @file
 * @brief End-to-end bass-mirror diagnosis on REAL recorded guitar, in Play mode,
 *        through the full AccompanimentProcessor.
 *
 * Why: the mirror-vs-harmony arbitration is a ratio question, and every prior
 * attempt to answer it used synthetic sine input or the learner's internal
 * counters. This drives the real plugin over the real recordings and measures
 * how many bass note-ons came from the mirror (`triggerLearnedBassNote`) versus
 * the authored/harmonic fallback grid (`emitBassRange`), plus the attack
 * detector's predicate breakdown. See `docs/BASS_MIRRORING.md` §6.
 *
 * The 10 s `tests/fixtures` excerpts are the *densest* windows of each raw take,
 * so they are the best case for the detector. The `data/raw` windows below are
 * the representative case: minutes of ordinary playing, sampled in 30 s blocks.
 */

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
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
        info.setBpm(kBpm);
        info.setIsPlaying(true);
        info.setTimeInSamples(samples);
        return info;
    }
    int64_t samples = 0;
};

/** Run the real processor in Play mode over [startSample, startSample+numSamples). */
PlayMirrorStats runPlay(const WavReader::PcmMono& pcm, int64_t startSample, int64_t numSamples,
                        int blockSize = kBlock)
{
    PlayMirrorStats s;
    s.loaded = true;
    s.seconds = static_cast<double>(numSamples) / pcm.sampleRate;

    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, blockSize);
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
    std::printf("[REC-MIRROR] t(s) | dLearned dGrid | capturing locked transition\n");
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
            std::printf("[REC-MIRROR] %4lld | %8d %5d | %9s %6s %10s\n",
                        static_cast<long long>(nextBucket / pcm.sampleRate),
                        learned - lastLearned, grid - lastGrid,
                        proc.isRiffCapturing() ? "yes" : "no",
                        proc.isGrooveLocked() ? "yes" : "no",
                        proc.isTransitionSectionActive() ? "yes" : "no");
            lastLearned = learned;
            lastGrid = grid;
            nextBucket += bucket;
        }
    }
    SUCCEED("record timeline ran");
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
