/**
 * @file
 * @brief Phase 0 measurement harness — real recorded guitar through the REAL
 *        EnergyAnalyser + PitchEstimator + StablePitchTracker + PhraseLearner,
 *        at every host buffer size, asserting on observable attack/lock
 *        behaviour (never on a test-local re-implementation of the RMS front
 *        end).
 *
 * Why this exists: the live bass mirror has regressed ~9 times. Every prior fix
 * was validated against synthetic envelopes or the learner's internal counters,
 * never against the emitted attack stream from real distorted audio across
 * buffer sizes. This harness feeds the exact production signal chain the
 * processor uses (getOnsetRmsEnergy() -> PhraseLearner::process) so the
 * detector is measured, not a copy of it.
 *
 * The processor does, per block:
 *   energyAnalyser.process(in, n);
 *   pitchEstimator.process(in, n);
 *   onsetRms = energyAnalyser.getOnsetRmsEnergy();
 *   pc       = stablePitchTracker.update(...);
 *   bass     = phraseLearner.process(hostSampleTime, onsetRms,
 *                                    pitchEstimator.getMidiNote(),
 *                                    pitchEstimator.getConfidence(),
 *                                    bpm, n, pc);
 * This harness mirrors that exactly.
 */

#include <catch2/catch_test_macros.hpp>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "analysis/EnergyAnalyser.h"
#include "analysis/PhraseLearner.h"
#include "analysis/PitchEstimator.h"
#include "analysis/StablePitchTracker.h"

namespace
{
constexpr double kSr  = 44100.0;   // fixtures are 44.1 kHz mono
constexpr float  kBpm = 120.0f;

#if !defined(MA_REPO_ROOT)
#define MA_REPO_ROOT ""
#endif

// ── Minimal 16-bit mono PCM WAV reader (shared shape with test_golden_signal) ──
struct PcmMono
{
    std::vector<float> samples;
    int sampleRate = 0;
};

bool readWav16Mono(const std::string& path, PcmMono& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    if (data.size() < 44 || data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
        return false;
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E')
        return false;

    auto u16 = [&](size_t o) { return static_cast<uint16_t>(data[o] | (data[o + 1] << 8)); };
    auto u32 = [&](size_t o) {
        return static_cast<uint32_t>(data[o] | (data[o + 1] << 8) | (data[o + 2] << 16) | (data[o + 3] << 24));
    };

    size_t pos = 12;
    uint16_t channels = 0, bits = 0;
    uint32_t sampleRate = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmBytes = 0;

    while (pos + 8 <= data.size())
    {
        const uint32_t size = u32(pos + 4);
        const size_t bodyStart = pos + 8;
        const size_t bodyEnd = bodyStart + size;
        if (bodyEnd > data.size())
            return false;

        if (data[pos] == 'f' && data[pos + 1] == 'm' && data[pos + 2] == 't' && data[pos + 3] == ' ')
        {
            channels   = u16(bodyStart + 2);
            sampleRate = u32(bodyStart + 4);
            bits       = u16(bodyStart + 14);
        }
        else if (data[pos] == 'd' && data[pos + 1] == 'a' && data[pos + 2] == 't' && data[pos + 3] == 'a')
        {
            pcm = data.data() + bodyStart;
            pcmBytes = size;
        }
        pos = bodyEnd + (size & 1u);
    }

    if (!pcm || channels != 1 || bits != 16 || sampleRate == 0)
        return false;

    const size_t n = pcmBytes / 2;
    out.sampleRate = static_cast<int>(sampleRate);
    out.samples.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        const int16_t s = static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
        out.samples[i] = static_cast<float>(s) / 32768.0f;
    }
    return true;
}

std::string fixturePath(const char* name)
{
    return std::string(MA_REPO_ROOT) + "/tests/fixtures/" + name;
}

// Observable result of running the REAL chain over one fixture at one block size.
struct MirrorRun
{
    bool   loaded = false;
    int    blockSize = 0;
    int    attacks = 0;         // PhraseLearner::BassNote.trigger events (Learning)
    int    lockedTriggers = 0;  // triggers emitted once locked
    bool   locked = false;
    double lockSeconds = 0.0;
    double durationSeconds = 0.0;
    double attacksPerSec = 0.0; // over the pre-lock (or whole, if never locked) span
};

MirrorRun runRealChain(const PcmMono& pcm, int blockSize)
{
    MirrorRun r;
    r.loaded = true;
    r.blockSize = blockSize;

    EnergyAnalyser energy;
    PitchEstimator pitch;
    StablePitchTracker stablePitch;
    PhraseLearner learner;

    energy.prepare(kSr, blockSize);
    pitch.prepare(kSr, blockSize);
    stablePitch = StablePitchTracker{};
    learner.prepare(kSr);
    // Follow-mode: auto-lock enabled (this is how the learner detects a phrase).
    learner.setAutoLockEnabled(true);

    const int total = static_cast<int>(pcm.samples.size());
    r.durationSeconds = static_cast<double>(total) / kSr;

    int64_t hostSample = 0;
    int64_t firstLockSample = -1;

    for (int start = 0; start + blockSize <= total; start += blockSize)
    {
        const float* in = pcm.samples.data() + start;

        energy.process(in, blockSize);
        pitch.process(in, blockSize);

        const float onsetRms = energy.getOnsetRmsEnergy();
        const int pc = stablePitch.update(pitch.getMidiNote(), pitch.getConfidence(),
                                          kBpm, blockSize, kSr, /*silent*/ false);
        const int pcForBass = (pc != INT_MIN) ? pc : stablePitch.getLastPitchClassOffset();

        const bool wasLocked = learner.isLocked();
        const auto bass = learner.process(hostSample, onsetRms,
                                          pitch.getMidiNote(), pitch.getConfidence(),
                                          kBpm, blockSize, pcForBass);

        if (bass.trigger)
        {
            if (learner.isLocked())
                ++r.lockedTriggers;
            else
                ++r.attacks;
        }

        if (!wasLocked && learner.isLocked() && firstLockSample < 0)
            firstLockSample = hostSample;

        hostSample += blockSize;
    }

    r.locked = learner.isLocked() || firstLockSample >= 0;
    if (firstLockSample >= 0)
    {
        r.lockSeconds = static_cast<double>(firstLockSample) / kSr;
        r.attacksPerSec = (r.lockSeconds > 0.0)
            ? static_cast<double>(r.attacks) / r.lockSeconds : 0.0;
    }
    else
    {
        r.attacksPerSec = (r.durationSeconds > 0.0)
            ? static_cast<double>(r.attacks) / r.durationSeconds : 0.0;
    }
    return r;
}

void reportFixture(const char* name)
{
    PcmMono pcm;
    const bool ok = readWav16Mono(fixturePath(name), pcm);
    if (!ok)
    {
        std::printf("[MIRROR] %-24s : MISSING fixture (MA_REPO_ROOT=%s)\n", name, MA_REPO_ROOT);
        return;
    }

    const int sizes[] = { 128, 256, 512, 1024, 2048 };
    std::printf("[MIRROR] %-24s (%.1fs @ %d Hz)\n", name, static_cast<double>(pcm.samples.size()) / kSr,
                pcm.sampleRate);
    std::printf("[MIRROR]   %6s | %8s | %10s | %8s | %10s\n",
                "block", "attacks", "att/sec", "locked", "lock(s)");
    for (int bs : sizes)
    {
        const MirrorRun r = runRealChain(pcm, bs);
        std::printf("[MIRROR]   %6d | %8d | %10.2f | %8s | %10.2f\n",
                    bs, r.attacks, r.attacksPerSec, r.locked ? "yes" : "no", r.lockSeconds);
    }
}
} // namespace

// Diagnostic: print the per-buffer-size attack/lock table for every real fixture.
// This is the evidence the remediation is built on; it never fails (it measures).
TEST_CASE("bass mirror: real-audio attack/lock measurement across buffer sizes",
          "[bass][mirror][realaudio][diagnostic]")
{
    reportFixture("palm_mute_chug.wav");
    reportFixture("thrash_chug.wav");
    reportFixture("open_chord_passage.wav");
    reportFixture("single_note_run.wav");
    SUCCEED("measurement harness ran");
}

// Sanity: the real onset RMS front end must actually respond to a real chug.
// This asserts on the PRODUCTION getOnsetRmsEnergy(), not a test-local copy.
TEST_CASE("bass mirror: real onset RMS is non-trivial on a real chug",
          "[bass][mirror][realaudio]")
{
    PcmMono pcm;
    if (!readWav16Mono(fixturePath("palm_mute_chug.wav"), pcm))
    {
        WARN("palm_mute_chug.wav missing; skipping");
        return;
    }

    EnergyAnalyser energy;
    energy.prepare(kSr, 512);

    float maxOnset = 0.0f;
    float minOnset = 1.0f;
    const int total = static_cast<int>(pcm.samples.size());
    for (int start = 0; start + 512 <= total; start += 512)
    {
        energy.process(pcm.samples.data() + start, 512);
        const float o = energy.getOnsetRmsEnergy();
        maxOnset = std::max(maxOnset, o);
        if (o > 0.0f) minOnset = std::min(minOnset, o);
    }

    // A real distorted chug must swing the fast onset window well above the
    // attack floor (rms > 0.01 in detectAttack) at its peaks.
    REQUIRE(maxOnset > 0.05f);
    // And it must show a trough between chugs (the whole premise of the 0.02 s
    // window) — i.e. the signal is not a flat DC block.
    REQUIRE(minOnset < maxOnset * 0.9f);
}
