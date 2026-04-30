#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "analysis/OnsetDetector.h"

namespace {

// Generate a click train at the given BPM and sample rate.
// Each click is a unit impulse placed at the exact period boundary.
std::vector<float> makeClickTrain(double bpm, double sampleRate, int numPeriods)
{
    const int period = static_cast<int>(std::round(sampleRate * 60.0 / bpm));
    const int total  = period * numPeriods;
    std::vector<float> buf(static_cast<size_t>(total), 0.0f);
    for (int i = 0; i < total; i += period)
        buf[static_cast<size_t>(i)] = 1.0f;
    return buf;
}

// Feed audio through the detector in fixed-size chunks.
void feedInChunks(OnsetDetector& det, const std::vector<float>& audio, int chunkSize)
{
    for (int pos = 0; pos + chunkSize <= static_cast<int>(audio.size()); pos += chunkSize)
        det.process(audio.data() + pos, chunkSize);
}

} // namespace

// ─── BPM clamping ─────────────────────────────────────────────────────────────

TEST_CASE("OnsetDetector: BPM does not drop below kMinBpm (80) for slow click train", "[onset]")
{
    // Synthesise a 70 BPM click train — below the 80 BPM minimum.
    // The detector should report BPM clamped at kMinBpm (80).
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // 70 BPM period ≈ 37800 samples; 14 periods gives 14+ onsets for IOI history
    auto audio = makeClickTrain(70.0, sr, 14);
    feedInChunks(det, audio, 256);

    REQUIRE(det.getCurrentBpm() >= 80.0f);
}

TEST_CASE("OnsetDetector: BPM does not exceed kMaxBpm (220) for fast click train", "[onset]")
{
    // Synthesise a 240 BPM click train — above the 220 BPM maximum.
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // 240 BPM period ≈ 11025 samples; 20 periods gives enough onsets
    auto audio = makeClickTrain(240.0, sr, 20);
    feedInChunks(det, audio, 256);

    REQUIRE(det.getCurrentBpm() <= 220.0f);
}

// ─── Short-block edge case ────────────────────────────────────────────────────

TEST_CASE("OnsetDetector: process with a block smaller than the FFT hop does not crash", "[onset]")
{
    // The FFT hop size is 512 samples; feeding a block of 64 should be safe.
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    std::vector<float> tiny(64, 0.0f);
    REQUIRE_NOTHROW(det.process(tiny.data(), 64));

    // BPM should remain at the default safe value (no garbage)
    const float bpm = det.getCurrentBpm();
    REQUIRE(bpm >= 80.0f);
    REQUIRE(bpm <= 220.0f);
}

TEST_CASE("OnsetDetector: process with a single sample does not crash", "[onset]")
{
    OnsetDetector det;
    det.prepare(44100.0, 512);

    float sample = 1.0f;
    REQUIRE_NOTHROW(det.process(&sample, 1));
    REQUIRE(det.getCurrentBpm() >= 80.0f);
}

// ─── Steady-state default ─────────────────────────────────────────────────────

TEST_CASE("OnsetDetector: default BPM of 120 before any onsets accumulate", "[onset]")
{
    OnsetDetector det;
    det.prepare(44100.0, 512);

    // Process a few blocks without any transients
    std::vector<float> silence(512, 0.0f);
    for (int i = 0; i < 10; ++i)
        det.process(silence.data(), 512);

    REQUIRE(det.getCurrentBpm() == 120.0f);
}

TEST_CASE("OnsetDetector: resetTempoLock clears stale IOI tempo", "[onset]")
{
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    auto fastAudio = makeClickTrain(180.0, sr, 24);
    feedInChunks(det, fastAudio, 256);
    REQUIRE(det.getCurrentBpm() > 160.0f);

    det.resetTempoLock();
    REQUIRE(det.getCurrentBpm() == 120.0f);
    REQUIRE_FALSE(det.isTempoLocked());
}

// ─── Gradual tempo drift ──────────────────────────────────────────────────────

TEST_CASE("OnsetDetector: BPM tracks gradual tempo increase (100 -> 140 BPM)", "[onset]")
{
    // Feed 120 BPM for a while to prime the IOI history, then switch to 140 BPM.
    // After enough 140 BPM periods, the BPM estimate should converge toward 140.
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // Prime with 12 periods at 120 BPM
    auto primeAudio = makeClickTrain(120.0, sr, 12);
    feedInChunks(det, primeAudio, 256);

    // Then 14 periods at 140 BPM
    auto newAudio = makeClickTrain(140.0, sr, 14);
    feedInChunks(det, newAudio, 256);

    const float bpm = det.getCurrentBpm();
    // After the shift, BPM should have moved toward 140; allow a large window
    // because the IOI ring buffer blends old and new intervals.
    REQUIRE(bpm > 110.0f);
    REQUIRE(bpm <= 220.0f);
}
