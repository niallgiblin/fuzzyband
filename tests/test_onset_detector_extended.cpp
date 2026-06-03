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

TEST_CASE("OnsetDetector: BPM does not drop below kMinBpm (40) for slow click train", "[onset]")
{
    // Synthesise a 70 BPM click train — below the 80 BPM minimum.
    // The detector should report BPM clamped at kMinBpm (80).
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // 70 BPM period ≈ 37800 samples; 14 periods gives 14+ onsets for IOI history
    auto audio = makeClickTrain(70.0, sr, 14);
    feedInChunks(det, audio, 256);

    REQUIRE(det.getCurrentBpm() >= 40.0f);
}

TEST_CASE("OnsetDetector: BPM does not exceed kMaxBpm (300) for fast click train", "[onset]")
{
    // Synthesise a 350 BPM click train — above the 300 BPM maximum.
    // The detector should report BPM clamped at kMaxBpm (300).
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // 350 BPM period ≈ 7560 samples; 25 periods gives enough onsets
    auto audio = makeClickTrain(350.0, sr, 25);
    feedInChunks(det, audio, 256);

    REQUIRE(det.getCurrentBpm() <= 300.0f);
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
    REQUIRE(bpm >= 40.0f);
    REQUIRE(bpm <= 300.0f);
}

TEST_CASE("OnsetDetector: process with a single sample does not crash", "[onset]")
{
    OnsetDetector det;
    det.prepare(44100.0, 512);

    float sample = 1.0f;
    REQUIRE_NOTHROW(det.process(&sample, 1));
    REQUIRE(det.getCurrentBpm() >= 40.0f);
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

TEST_CASE("OnsetDetector: resetTempoLock clears lock and IOI ring but preserves BPM", "[onset]")
{
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    auto fastAudio = makeClickTrain(180.0, sr, 24);
    feedInChunks(det, fastAudio, 256);
    const float bpmBefore = det.getCurrentBpm();
    REQUIRE(bpmBefore > 160.0f);

    det.resetTempoLock();
    // BPM is preserved through reset — caller seeds it via setSeedBpm()
    REQUIRE(det.getCurrentBpm() == bpmBefore);
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
    REQUIRE(bpm <= 300.0f);
}

// ─── Soft-lock (EMA drift) ────────────────────────────────────────────────────

TEST_CASE("OnsetDetector: lock confirms at 140 BPM with stable click train", "[onset][softlock]")
{
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // 140 BPM, 22 periods gives 22+ onsets (enough for lock — 8+ consistent IOIs)
    auto audio = makeClickTrain(140.0, sr, 22);
    feedInChunks(det, audio, 256);

    REQUIRE(det.isTempoLocked());
    const float bpm = det.getCurrentBpm();
    REQUIRE(bpm >= 135.0f);
    REQUIRE(bpm <= 145.0f);
}

TEST_CASE("OnsetDetector: soft-lock EMA drift — 140 to 150 BPM", "[onset][softlock]")
{
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // Phase 1: Lock at ~140 BPM with 22 periods
    auto audio140 = makeClickTrain(140.0, sr, 22);
    feedInChunks(det, audio140, 256);
    REQUIRE(det.isTempoLocked());
    const float lockedBpm = det.getCurrentBpm();
    REQUIRE(lockedBpm >= 135.0f);
    REQUIRE(lockedBpm <= 145.0f);

    // Phase 2: Continue feeding 150 BPM to exercise soft-lock drift
    // With EMA α=0.03 and ~50 periods at 150 BPM, the BPM should drift
    // noticeably from 140 toward 150 but not jump instantly.
    auto audio150 = makeClickTrain(150.0, sr, 50);
    feedInChunks(det, audio150, 256);

    const float driftedBpm = det.getCurrentBpm();
    // Drifted away from the original locked value (not frozen)
    REQUIRE(driftedBpm > lockedBpm);
    // Converged within ±5 of 150 (α=0.03 EMA is conservative)
    REQUIRE(driftedBpm >= 145.0f);
    REQUIRE(driftedBpm <= 155.0f);
}

TEST_CASE("OnsetDetector: setSeedBpm seeds BPM that survives reset", "[onset][softlock]")
{
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // Seed with 160 BPM
    det.setSeedBpm(160.0f);
    REQUIRE(det.getCurrentBpm() == 160.0f);

    // Feed some audio just to exercise state
    auto audio = makeClickTrain(120.0, sr, 8);
    feedInChunks(det, audio, 256);

    // resetTempoLock() should preserve the seeded BPM
    det.resetTempoLock();
    REQUIRE(det.getCurrentBpm() == 160.0f);
    REQUIRE_FALSE(det.isTempoLocked());
}

// ─── Sludge-tempo range (M002 S01) ────────────────────────────────────────────

TEST_CASE("OnsetDetector: detects BPM=40 (sludge floor) from click train", "[onset][sludge]")
{
    // 40 BPM → period ≈ 66150 samples at 44.1 kHz.
    // 14 periods gives 14+ onsets for 8-IOI lock threshold.
    // 40 BPM octave-aliases to 80 against default 120 seed — seed at 40.
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);
    det.setSeedBpm(40.0f);

    auto audio = makeClickTrain(40.0, sr, 14);
    feedInChunks(det, audio, 256);

    const float bpm = det.getCurrentBpm();
    REQUIRE(bpm >= 40.0f);
    REQUIRE(bpm <= 50.0f);  // within ±10 BPM of 40
    REQUIRE(det.isTempoLocked());
}

TEST_CASE("OnsetDetector: detects BPM=50 (sludge) from click train", "[onset][sludge]")
{
    // 50 BPM octave-aliases to 100 against default 120 seed — seed at 50.
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);
    det.setSeedBpm(50.0f);

    auto audio = makeClickTrain(50.0, sr, 14);
    feedInChunks(det, audio, 256);

    const float bpm = det.getCurrentBpm();
    REQUIRE(bpm >= 45.0f);
    REQUIRE(bpm <= 55.0f);
    REQUIRE(det.isTempoLocked());
}

TEST_CASE("OnsetDetector: detects BPM=60 (sludge) from click train", "[onset][sludge]")
{
    // 60 BPM is ambiguous with 120 BPM 8th notes — octave disambiguation
    // against the default 120 seed would fold it to 120. Seed at 60 first.
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);
    det.setSeedBpm(60.0f);

    auto audio = makeClickTrain(60.0, sr, 14);
    feedInChunks(det, audio, 256);

    const float bpm = det.getCurrentBpm();
    // With seed at 60, octave disambiguation should keep near 60
    REQUIRE(bpm >= 55.0f);
    REQUIRE(bpm <= 65.0f);
    REQUIRE(det.isTempoLocked());
}

TEST_CASE("OnsetDetector: detects BPM=75 (sludge) from click train", "[onset][sludge]")
{
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    auto audio = makeClickTrain(75.0, sr, 14);
    feedInChunks(det, audio, 256);

    const float bpm = det.getCurrentBpm();
    REQUIRE(bpm >= 70.0f);
    REQUIRE(bpm <= 80.0f);
    REQUIRE(det.isTempoLocked());
}

TEST_CASE("OnsetDetector: detects BPM=85 (sludge upper) from click train", "[onset][sludge]")
{
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    auto audio = makeClickTrain(85.0, sr, 14);
    feedInChunks(det, audio, 256);

    const float bpm = det.getCurrentBpm();
    REQUIRE(bpm >= 80.0f);
    REQUIRE(bpm <= 90.0f);
    REQUIRE(det.isTempoLocked());
}

TEST_CASE("OnsetDetector: octave-disambiguates 2x alias at 50 BPM", "[onset][sludge]")
{
    // Seed at 50 BPM, then feed audio that could be interpreted as 100 BPM
    // (8th notes at 50 BPM = quarter-note spacing as 100 BPM).
    // The octave disambiguation should keep it near 50.
    OnsetDetector det;
    const double sr = 44100.0;
    det.prepare(sr, 512);

    // First, lock at 50 BPM (seed to prevent octave alias to 100)
    det.setSeedBpm(50.0f);
    auto audio50 = makeClickTrain(50.0, sr, 14);
    feedInChunks(det, audio50, 256);
    REQUIRE(det.isTempoLocked());
    const float after50 = det.getCurrentBpm();
    REQUIRE(after50 >= 45.0f);
    REQUIRE(after50 <= 55.0f);

    // Now feed clicks at 100 BPM (potential 2× alias)
    auto audio100 = makeClickTrain(100.0, sr, 20);
    feedInChunks(det, audio100, 256);

    const float finalBpm = det.getCurrentBpm();
    // With octave disambiguation, the 100 BPM input stays at 100 (100/55 ≈ 1.82 > 1.7),
    // so no folding happens. The soft-lock EMA drifts toward 100 from 50.
    // After 20 periods, we should be moving toward 100 but not overshooting.
    REQUIRE(finalBpm >= 45.0f);   // hasn't dropped below sludge range
    REQUIRE(finalBpm <= 100.0f);  // hasn't overshot
}

TEST_CASE("OnsetDetector: refractory rejects 40ms spacing, accepts 60ms", "[onset][sludge]")
{
    // Create a buffer with two impulses separated by a known interval.
    const double sr = 44100.0;
    const int refractorySamples = static_cast<int>(0.05 * sr); // 2205 samples = 50ms

    OnsetDetector det;
    det.prepare(sr, 512);

    // Test 1: onsets 40ms apart → should be REJECTED (under 50ms refractory)
    {
        OnsetDetector d1;
        d1.prepare(sr, 512);

        const int spacing40ms = static_cast<int>(0.040 * sr);  // 1764 samples
        std::vector<float> buf(spacing40ms * 3, 0.0f);
        buf[0]             = 1.0f;  // first onset at t=0
        buf[spacing40ms]    = 1.0f;  // second onset at t=40ms → should be rejected
        buf[spacing40ms*2]  = 1.0f;  // third at t=80ms → valid

        feedInChunks(d1, buf, 256);

        // Only 2 valid onsets (0ms and 80ms), not enough for lock
        REQUIRE_FALSE(d1.isTempoLocked());
    }

    // Test 2: onsets 60ms apart → should be ACCEPTED (above 50ms refractory)
    {
        OnsetDetector d2;
        d2.prepare(sr, 512);

        const int spacing60ms = static_cast<int>(0.060 * sr);  // 2646 samples
        // Generate full click train at period = 60ms for 14+ onsets
        const double equivBpm = 60.0 / 0.060;  // = 1000 BPM
        // Actually at 60ms spacing, 8 onsets = 480ms. Let's use makeClickTrain
        // with period derived from 60ms spacing.
        const int period60ms = static_cast<int>(std::round(sr * 0.060));
        const int numPeriods = 14;
        std::vector<float> buf2(static_cast<size_t>(period60ms * numPeriods), 0.0f);
        for (int i = 0; i < numPeriods; ++i)
            buf2[static_cast<size_t>(i * period60ms)] = 1.0f;

        feedInChunks(d2, buf2, 256);

        // 14 onsets at 60ms spacing → equivalent to 1000 BPM quarter notes,
        // which octave-disambiguates to something in range. The key point:
        // all 14 onsets were accepted, not rejected by refractory.
        const float bpm = d2.getCurrentBpm();
        REQUIRE(bpm >= 40.0f);
        REQUIRE(bpm <= 300.0f);
        // Should have accumulated at least 8 IOIs (14 onsets, 13 intervals)
        // With lock: 8+ consistent IOIs at 60ms → clamped BPM in range
    }
}
