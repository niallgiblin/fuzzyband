/**
 * @file
 * @brief Golden-signal tests — real recorded guitar through the exact analysis
 *        pipeline (0.1 s RMS window + PitchEstimator + PhraseLearner).
 *
 * These catch the recurring *layer* regressions invisible to synthetic tests:
 * a too-strict attack detector, a pitch-confidence gate that starved attacks,
 * and sparse learned playback. Each fixture is a short excerpt from the raw
 * 24-bit 44.1 kHz captures under `data/raw/`; see `tests/fixtures/README.md`
 * for how they were produced.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#include "analysis/PhraseLearner.h"
#include "analysis/PitchEstimator.h"

namespace
{
constexpr double kSr    = 44100.0;   // fixtures are 44.1 kHz mono
constexpr int    kBlock = 512;
constexpr float  kBpm   = 120.0f;

#if !defined(MA_REPO_ROOT)
#define MA_REPO_ROOT ""
#endif

// ── Minimal 16-bit mono PCM WAV reader ────────────────────────────────────────
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

    // Walk chunks to find fmt and data.
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

/** @brief EnergyAnalyser-equivalent 0.1 s RMS window (×4, clamped [0,1]). */
class RmsWindow
{
public:
    explicit RmsWindow(double sampleRate)
        : win(static_cast<size_t>(static_cast<int>(0.1 * sampleRate)), 0.0f) {}

    void push(float s)
    {
        win[w] = s * s;
        w = (w + 1) % static_cast<int>(win.size());
        if (fill < static_cast<int>(win.size()))
            ++fill;
    }

    float getRms() const
    {
        float acc = 0.0f;
        for (int i = 0; i < fill; ++i)
            acc += win[static_cast<size_t>(i)];
        if (fill <= 0)
            return 0.0f;
        return std::clamp(std::sqrt(acc / static_cast<float>(fill)) * 4.0f, 0.0f, 1.0f);
    }

private:
    std::vector<float> win;
    int w = 0;
    int fill = 0;
};

struct GoldenResult
{
    bool locked = false;
    double lockSeconds = 0.0;      // time (s) to first lock
    double notesPerSec = 0.0;      // locked-playback density over the 2 s after lock
    int patternLength = 0;
};

GoldenResult runPipeline(const std::string& path)
{
    PcmMono audio;
    GoldenResult r;
    if (!readWav16Mono(path, audio))
        return r;

    PitchEstimator pitch;
    pitch.prepare(kSr, kBlock);
    PhraseLearner learner;
    learner.prepare(kSr);
    RmsWindow rms(kSr);

    const size_t totalSamples = audio.samples.size();
    int64_t lockSample = -1;
    int triggersAfterLock = 0;
    int64_t postLockWindowEnd = -1;  // collect density until this sample

    for (size_t start = 0; start < totalSamples; start += kBlock)
    {
        const size_t n = std::min<size_t>(kBlock, totalSamples - start);
        const int64_t sampleTime = static_cast<int64_t>(start);

        std::vector<float> block(audio.samples.begin() + static_cast<ptrdiff_t>(start),
                                 audio.samples.begin() + static_cast<ptrdiff_t>(start + n));
        for (float s : block)
            rms.push(s);
        pitch.process(block.data(), static_cast<int>(n));

        const auto note = learner.process(
            sampleTime,
            rms.getRms(),
            pitch.getMidiNote(),
            pitch.getConfidence(),
            kBpm, static_cast<int>(n));

        if (!r.locked && learner.isLocked())
        {
            r.locked = true;
            r.lockSeconds = static_cast<double>(sampleTime) / kSr;
            r.patternLength = learner.getPatternLength();
            lockSample = sampleTime;
            postLockWindowEnd = lockSample + static_cast<int64_t>(2.0 * kSr);
        }

        if (r.locked && sampleTime < postLockWindowEnd && note.trigger)
            ++triggersAfterLock;
    }

    if (r.locked)
        r.notesPerSec = static_cast<double>(triggersAfterLock) / 2.0;
    return r;
}

std::string fixturePath(const char* name)
{
    return std::string(MA_REPO_ROOT) + "/tests/fixtures/" + name;
}
} // namespace

TEST_CASE("Golden signal: palm-mute chug locks and stays dense", "[golden][phrase]")
{
    const auto r = runPipeline(fixturePath("palm_mute_chug.wav"));
    REQUIRE(r.locked);
    REQUIRE(r.lockSeconds <= 5.0);     // locks within a few seconds of real chugging
    REQUIRE(r.patternLength >= 2);
    REQUIRE(r.notesPerSec >= 6.0);     // dense, not the sparse 1–2/s regression (0.9.12)
}

TEST_CASE("Golden signal: thrash chug locks and stays dense", "[golden][phrase]")
{
    const auto r = runPipeline(fixturePath("thrash_chug.wav"));
    REQUIRE(r.locked);
    REQUIRE(r.lockSeconds <= 5.0);
    REQUIRE(r.notesPerSec >= 6.0);
}

TEST_CASE("Golden signal: open-chord passage locks (rhythm mirror)", "[golden][phrase]")
{
    const auto r = runPipeline(fixturePath("open_chord_passage.wav"));
    REQUIRE(r.locked);
    REQUIRE(r.lockSeconds <= 8.0);
}

TEST_CASE("Golden signal: single-note run locks (rhythm mirror)", "[golden][phrase]")
{
    const auto r = runPipeline(fixturePath("single_note_run.wav"));
    REQUIRE(r.locked);
    REQUIRE(r.lockSeconds <= 8.0);
}
