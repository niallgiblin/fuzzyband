/**
 * @file
 * @brief Diagnostic probe: dump the REAL PitchEstimator's output on a supplied
 *        clean DI so agents can compare it against an independent reference.
 *
 * Env-gated, diagnostic only (never fails on measurement):
 *   MA_PROBE_WAV   = mono clean DI (stem 01-*)
 *   MA_PROBE_BPM   = host tempo (default 170)
 *   MA_PROBE_TIMES = comma-separated seconds to probe, e.g. "3.43,4.22,5.05"
 *   MA_PROBE_LEN   = middle onset window length in samples (default 2048)
 *
 * For each probe time it reports the fixed-hop and block estimates AT the pick,
 * plus estimateOnset() windows starting at the pick at several lengths.
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#include "analysis/PitchEstimator.h"
#include "fixtures/WavReader.h"

namespace
{
const char* kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void printMidi(const char* tag, float midi, float conf)
{
    const int m = static_cast<int>(std::lround(midi));
    std::printf("    %-12s midi=%6.2f %2s(%3d) conf=%.2f\n",
                tag, midi, kNames[((m % 12) + 12) % 12], m, conf);
}
} // namespace

TEST_CASE("pitch probe: real DI through the real PitchEstimator",
          "[integration][pitch][probe]")
{
    const char* path = std::getenv("MA_PROBE_WAV");
    if (path == nullptr || *path == '\0')
    {
        SUCCEED("MA_PROBE_WAV unset — pitch probe skipped");
        return;
    }
    const char* bpmEnv = std::getenv("MA_PROBE_BPM");
    const double bpm = (bpmEnv != nullptr && *bpmEnv != '\0') ? std::atof(bpmEnv) : 170.0;

    WavReader::PcmMono pcm;
    std::string err;
    REQUIRE(WavReader::readMonoWav(path, pcm, &err));
    REQUIRE_FALSE(pcm.samples.empty());
    const double sr = static_cast<double>(pcm.sampleRate);
    const int block = 512;
    const int len = [] {
        const char* e = std::getenv("MA_PROBE_LEN");
        return (e != nullptr && *e != '\0') ? std::atoi(e) : 2048;
    }();

    std::vector<double> times;
    if (const char* te = std::getenv("MA_PROBE_TIMES"); te != nullptr && *te != '\0')
    {
        std::string s(te);
        size_t p = 0;
        while (p < s.size())
        {
            const size_t q = s.find(',', p);
            times.push_back(std::atof(s.substr(p, q - p).c_str()));
            if (q == std::string::npos) break;
            p = q + 1;
        }
    }
    else
    {
        times = { 3.43, 3.60, 4.22, 4.30, 5.05, 5.30 };
    }

    PitchEstimator pe;
    pe.prepare(sr, block);

    std::vector<float> blockMidi(times.size(), 0.0f), blockConf(times.size(), 0.0f);
    std::vector<float> hopMidi(times.size(), 0.0f), hopConf(times.size(), 0.0f);
    std::vector<bool> captured(times.size(), false), printed(times.size(), false);

    std::printf("[PITCH-PROBE] file=%s sr=%.0f bpm=%.1f len=%d\n", path, sr, bpm, len);

    int64_t start = 0;
    const int64_t total = static_cast<int64_t>(pcm.samples.size());
    while (start + block <= total)
    {
        pe.process(pcm.samples.data() + start, block);
        const int64_t blockEnd = start + block;

        // Snapshot the block/hop estimate at the pick instant.
        for (size_t i = 0; i < times.size(); ++i)
        {
            if (captured[i] || static_cast<double>(blockEnd) / sr < times[i])
                continue;
            blockMidi[i] = pe.getMidiNote();
            blockConf[i] = pe.getConfidence();
            int bestHop = -1;
            float bestD = 1e9f;
            for (int h = 0; h < pe.getHopCount(); ++h)
            {
                const float hs = static_cast<float>(pe.getHopOffset(h)) / static_cast<float>(sr);
                const float d = std::abs(hs - static_cast<float>(times[i] - static_cast<double>(start) / sr));
                if (d < bestD) { bestD = d; bestHop = h; }
            }
            if (bestHop >= 0)
            {
                hopMidi[i] = pe.getHopMidi(bestHop);
                hopConf[i] = pe.getHopConf(bestHop);
            }
            captured[i] = true;
        }

        // Resolve the onset window once exactly `length` samples after the
        // pick are in the ring (the ring is only 4096 long, so wait just long
        // enough: blockEnd == onset + 2048).
        for (size_t i = 0; i < times.size(); ++i)
        {
            if (printed[i])
                continue;
            const int64_t onsetAbs = static_cast<int64_t>(std::llround(times[i] * sr));
            if (blockEnd < onsetAbs + 2048)
                continue;
            std::printf("[PITCH-PROBE] t=%.3fs (elapsed=%.1fms)\n", times[i],
                        1000.0 * static_cast<double>(blockEnd - onsetAbs) / sr);
            printMidi("block@pick", blockMidi[i], blockConf[i]);
            printMidi("hop@pick", hopMidi[i], hopConf[i]);
            for (int l : { 512, 1024, 2048 })
            {
                if (static_cast<int64_t>(l) > blockEnd - onsetAbs)
                    continue;
                float m = 0.0f, c = 0.0f;
                if (pe.estimateOnset(blockEnd, onsetAbs, l, m, c))
                {
                    char tag[32];
                    std::snprintf(tag, sizeof(tag), "onset[%d]", l);
                    printMidi(tag, m, c);
                }
            }
            printed[i] = true;
        }
        start += block;
    }
    SUCCEED("pitch probe ran");
}
