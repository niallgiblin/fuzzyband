#pragma once

/**
 * @file
 * @brief Lock-free SPSC ring buffer for audio window accumulation.
 *
 * Audio thread writes samples. Consumer thread checks if a full window is ready
 * and reads it. Single-producer (audio), single-consumer (inference thread).
 */

#include <atomic>
#include <cstdint>
#include <vector>

/**
 * @brief Accumulates audio samples into fixed-size windows.
 *
 * Write from audio thread via @ref write. Poll from inference thread via
 * @ref isWindowReady and read the full window via @ref readWindow.
 */
class AudioRingBuffer
{
public:
    /** @param windowSize number of samples per window (e.g. 22050 = 512ms @ 44.1kHz). */
    explicit AudioRingBuffer(int windowSize = 22050);

    /** @brief Write @p numSamples from @p data. Audio thread only. */
    void write(const float* data, int numSamples) noexcept;

    /** @brief True when at least one full window is accumulated. Consumer thread. */
    bool isWindowReady() const noexcept;

    /** @brief Read the next full window into @p out (must be size windowSize). Consumer thread.
     *  @return number of samples copied, or 0 if no window ready. */
    int readWindow(float* out) noexcept;

    /** @brief Reset all state. */
    void reset() noexcept;

private:
    const int windowSize;
    std::vector<float> buffer;
    std::atomic<int> writePos{ 0 };
    std::atomic<int> windowsReady{ 0 };
    int readPos = 0;  // consumer-thread only
};
