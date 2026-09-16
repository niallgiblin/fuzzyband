#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include "analysis/PitchEstimator.h"
#include "analysis/EnergyAnalyser.h"
#include "fixtures/WavReader.h"

#if !defined(MA_REPO_ROOT)
#define MA_REPO_ROOT ""
#endif

TEST_CASE("PitchEstimator tracks 440 Hz sine near MIDI 69 (±0.25 semitone)", "[pitch]")
{
    const double sr = 48000.0;
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        buf[static_cast<size_t>(i)] = static_cast<float>(0.8 * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
    }

    PitchEstimator est;
    est.prepare(sr, n);
    est.process(buf.data(), n);

    const float midi = est.getMidiNote();
    REQUIRE(midi > 69.0f - 0.25f);
    REQUIRE(midi < 69.0f + 0.25f);
    REQUIRE(est.getConfidence() > 0.1f);
}

TEST_CASE("PitchEstimator silence yields low confidence", "[pitch]")
{
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);

    PitchEstimator est;
    est.prepare(48000.0, n);
    est.process(buf.data(), n);

    REQUIRE(est.getConfidence() < 0.2f);
}

TEST_CASE("PitchEstimator tracks drop-C low string (65.4 Hz ≈ MIDI 36)", "[pitch][dropc]")
{
    const double sr = 48000.0;
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        // Low C2 fundamental + a bit of second harmonic, like a palm-muted chug.
        buf[static_cast<size_t>(i)] = static_cast<float>(
            0.7 * std::sin(2.0 * 3.14159265358979323846 * 65.406 * t)
            + 0.25 * std::sin(2.0 * 3.14159265358979323846 * 130.812 * t));
    }

    PitchEstimator est;
    est.prepare(sr, n);
    est.process(buf.data(), n);

    const float midi = est.getMidiNote();
    INFO("detected midi: " << midi << " conf: " << est.getConfidence());
    REQUIRE(midi > 35.0f);   // C2 = 36, allow a semitone of error
    REQUIRE(midi < 38.0f);
    REQUIRE(est.getConfidence() > 0.1f);
}

TEST_CASE("PitchEstimator tracks A1 (55 Hz ≈ MIDI 33) — extended low range", "[pitch][dropc]")
{
    const double sr = 48000.0;
    const int n = 4096;
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / sr;
        buf[static_cast<size_t>(i)] = static_cast<float>(
            0.7 * std::sin(2.0 * 3.14159265358979323846 * 55.0 * t));
    }

    PitchEstimator est;
    est.prepare(sr, n);
    est.process(buf.data(), n);

    const float midi = est.getMidiNote();
    INFO("detected midi: " << midi << " conf: " << est.getConfidence());
    REQUIRE(midi > 32.0f);
    REQUIRE(midi < 35.0f);
    REQUIRE(est.getConfidence() > 0.1f);
}

TEST_CASE("PitchEstimator: fixed hops stay aligned with EnergyAnalyser at every block size",
          "[pitch][hop][realaudio]")
{
    // The processor reads the per-hop pitch only when both analysers report the
    // SAME hop count for the block:
    //
    //     hopPitchAligned = (pitchEstimator.getHopCount() == hopCount);
    //     pitchMidi = hopPitchAligned ? pitchEstimator.getHopMidi(h) : blockPitchMidi;
    //
    // If they ever diverge the processor silently reverts to the block-level
    // estimate for EVERY hop in the block — the "one note behind / two picks in
    // one buffer share one pitch" bug, and a buffer-size dependence. The two
    // analysers use the same 0.0107 s countdown by construction; this pins it on
    // real audio across the buffer range.
    WavReader::PcmMono pcm;
    const std::string path = std::string(MA_REPO_ROOT) + "/tests/fixtures/palm_mute_chug.wav";
    if (!WavReader::readMonoWav(path, pcm) || pcm.samples.empty())
    {
        WARN("palm_mute_chug.wav missing; skipping");
        return;
    }
    const double sr = static_cast<double>(pcm.sampleRate);
    const int64_t total = static_cast<int64_t>(pcm.samples.size());

    int blocksChecked = 0;
    int hopsChecked = 0;
    for (int block : { 64, 128, 256, 512, 1024, 2048, 4096 })
    {
        EnergyAnalyser energy;
        PitchEstimator pitch;
        energy.prepare(sr, block);
        pitch.prepare(sr, block);

        for (int64_t start = 0; start + block <= total; start += block)
        {
            energy.process(pcm.samples.data() + start, block);
            pitch.process(pcm.samples.data() + start, block);

            REQUIRE(pitch.getHopCount() == energy.getOnsetHopCount());
            REQUIRE(pitch.getHopSamples()
                    == static_cast<int>(std::lround(0.0107 * sr)));
            for (int h = 0; h < pitch.getHopCount(); ++h)
                REQUIRE(pitch.getHopOffset(h) == energy.getOnsetHopOffset(h));
            ++blocksChecked;
            hopsChecked += pitch.getHopCount();
        }
    }
    INFO("blocks=" << blocksChecked << " hops=" << hopsChecked);
    REQUIRE(blocksChecked > 500);
    REQUIRE(hopsChecked > 500);
}

TEST_CASE("PitchEstimator: fixed-hop estimates track a low E2 sine at every hop",
          "[pitch][hop]")
{
    const double sr = 48000.0;
    const int block = 512;
    PitchEstimator est;
    est.prepare(sr, block);
    std::vector<float> buf(static_cast<size_t>(block), 0.0f);
    int64_t abs = 0;
    int good = 0, bad = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const double t = static_cast<double>(abs + i) / sr;
            buf[static_cast<size_t>(i)] =
                static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 82.407 * t));
        }
        est.process(buf.data(), block);
        abs += block;
        if (b < 40)
            continue;
        for (int h = 0; h < est.getHopCount(); ++h)
        {
            if (est.getHopConf(h) <= 0.3f)
                continue;
            const int pc = ((static_cast<int>(std::lround(est.getHopMidi(h))) % 12) + 12) % 12;
            if (pc == 4) ++good; else ++bad;
        }
    }
    INFO("E2 hops: good=" << good << " bad=" << bad);
    REQUIRE(good > 0);
    REQUIRE(bad == 0);
}
