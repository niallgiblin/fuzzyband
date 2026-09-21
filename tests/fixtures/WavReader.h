#pragma once

/**
 * @file
 * @brief Minimal mono PCM WAV reader for the real-audio test fixtures.
 *
 * Handles the two encodings in this repo: 16-bit PCM (the 10 s `tests/fixtures`
 * excerpts) and 24-bit PCM (the minutes-long `data/raw/` captures). Shared by
 * `test_golden_signal.cpp`, `test_bass_mirror_realaudio.cpp` and
 * `test_bass_mirror_play_realaudio.cpp` so the fixtures are decoded one way.
 *
 * **Mono only.** Stem bounces (drums/bass) are often stereo — reject them with a
 * clear error so agent audits cannot silently skip or analyse the wrong file.
 * The plugin input contract is clean DI on stem `01-*`.
 */

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace WavReader
{
struct PcmMono
{
    std::vector<float> samples;
    int sampleRate = 0;
};

inline bool readMonoWav(const std::string& path, PcmMono& out, std::string* error = nullptr)
{
    auto fail = [&](const char* msg) -> bool
    {
        if (error != nullptr)
            *error = msg;
        return false;
    };

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return fail("file not found / unreadable");

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    if (data.size() < 44 || data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
        return fail("not a RIFF file");
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E')
        return fail("not a WAVE file");

    auto u16 = [&](size_t o) { return static_cast<uint16_t>(data[o] | (data[o + 1] << 8)); };
    auto u32 = [&](size_t o) {
        return static_cast<uint32_t>(data[o] | (data[o + 1] << 8)
                                   | (data[o + 2] << 16) | (data[o + 3] << 24));
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
            return fail("truncated WAV chunk");

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

    if (!pcm || sampleRate == 0)
        return fail("missing fmt/data chunk");
    if (channels != 1)
        return fail("stereo/multi-channel rejected — use mono clean DI stem 01-* only "
                    "(02=drums, 03=bass are outputs, not plugin input)");
    if (bits != 16 && bits != 24)
        return fail("only 16-bit or 24-bit PCM supported");

    const size_t bytesPerSample = bits / 8u;
    const size_t n = pcmBytes / bytesPerSample;
    out.sampleRate = static_cast<int>(sampleRate);
    out.samples.resize(n);
    if (bits == 16)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const int16_t s = static_cast<int16_t>(pcm[i * 2] | (pcm[i * 2 + 1] << 8));
            out.samples[i] = static_cast<float>(s) / 32768.0f;
        }
    }
    else
    {
        for (size_t i = 0; i < n; ++i)
        {
            const unsigned char* p = pcm + i * 3;
            int32_t v = static_cast<int32_t>(p[0] | (p[1] << 8) | (p[2] << 16));
            if (v & 0x00800000)          // sign-extend 24-bit
                v |= ~0x00FFFFFF;
            out.samples[i] = static_cast<float>(v) / 8388608.0f;
        }
    }
    if (error != nullptr)
        error->clear();
    return true;
}
} // namespace WavReader
