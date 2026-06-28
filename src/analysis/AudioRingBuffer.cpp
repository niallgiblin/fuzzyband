#include "AudioRingBuffer.h"
#include <algorithm>
#include <cstring>

AudioRingBuffer::AudioRingBuffer(int windowSize)
    : windowSize(windowSize)
    , buffer(static_cast<size_t>(windowSize * 3)) // triple-buffered
{
}

void AudioRingBuffer::write(const float* data, int numSamples) noexcept
{
    if (numSamples <= 0 || data == nullptr) return;

    int pos = writePos.load(std::memory_order_relaxed);
    const int bufSize = static_cast<int>(buffer.size());

    for (int i = 0; i < numSamples; ++i)
    {
        buffer[static_cast<size_t>(pos)] = data[i];
        ++pos;
        if (pos >= bufSize)
            pos = 0;

        // Check for a complete window boundary
        if ((pos % windowSize) == 0)
            windowsReady.fetch_add(1, std::memory_order_release);
    }

    writePos.store(pos, std::memory_order_release);
}

bool AudioRingBuffer::isWindowReady() const noexcept
{
    return windowsReady.load(std::memory_order_acquire) > 0;
}

int AudioRingBuffer::readWindow(float* out) noexcept
{
    if (!isWindowReady() || out == nullptr) return 0;

    const int bufSize = static_cast<int>(buffer.size());
    for (int i = 0; i < windowSize; ++i)
    {
        out[i] = buffer[static_cast<size_t>(readPos)];
        ++readPos;
        if (readPos >= bufSize)
            readPos = 0;
    }

    windowsReady.fetch_sub(1, std::memory_order_release);
    return windowSize;
}

void AudioRingBuffer::reset() noexcept
{
    writePos.store(0, std::memory_order_release);
    windowsReady.store(0, std::memory_order_release);
    readPos = 0;
}
