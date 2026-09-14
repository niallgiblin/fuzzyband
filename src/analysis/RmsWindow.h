#pragma once

/**
 * @file
 * @brief Fixed-length sliding RMS window shared by the analyser and its tests.
 *
 * One definition of the RMS window so tests cannot silently drift from the
 * engine (the 1.0.3 change had to update a hand-copied window in
 * `test_golden_signal.cpp` — see `docs/BASS_MIRRORING.md` §4.4).
 */

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

/**
 * @brief Sliding mean-square RMS over the last N samples.
 *
 * Returns the same scale `EnergyAnalyser` has always used: `sqrt(mean of
 * squares) × 4`, clamped to [0, 1]. The window length is given in *seconds* and
 * converted with the sample rate, so it is buffer-size invariant.
 */
class RmsWindow
{
public:
    RmsWindow() = default;

    RmsWindow(double sampleRate, double seconds) { prepare(sampleRate, seconds); }

    void prepare(double sampleRate, double seconds)
    {
        const int len = std::max(1, static_cast<int>(seconds * sampleRate));
        window.assign(static_cast<size_t>(len), 0.0f);
        write = 0;
        fill = 0;
    }

    /** @brief Push one sample. O(1) — call @ref getRms once per block. */
    void push(float sample) noexcept
    {
        window[static_cast<size_t>(write)] = sample * sample;
        write = (write + 1) % static_cast<int>(window.size());
        if (fill < static_cast<int>(window.size()))
            ++fill;
    }

    /** @brief Current analyser-scaled RMS (×4, clamped to [0,1]). */
    float getRms() const noexcept
    {
        if (fill <= 0)
            return 0.0f;
        float acc = 0.0f;
        for (int i = 0; i < fill; ++i)
            acc += window[static_cast<size_t>(i)];
        return std::clamp(std::sqrt(acc / static_cast<float>(fill)) * 4.0f, 0.0f, 1.0f);
    }

    int getWindowSamples() const noexcept { return static_cast<int>(window.size()); }

private:
    std::vector<float> window;
    int write = 0;
    int fill = 0;
};
