#pragma once

/**
 * @file
 * @brief ONNX Runtime–based playing-style classifier (M009 S05).
 *
 * Loads the bundled `classifier.onnx` model and classifies mel-spectrogram
 * windows into five playing styles: Palm Mute, Open Chord, Single Note,
 * Sustain, Silence. Only compiled when `MA_ENABLE_ONNX` is defined.
 *
 * Runs on the background inference thread — not the audio callback.
 */

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#if defined(MA_ENABLE_ONNX)
struct OrtSessionDeleter;
#endif

/**
 * @brief Result from a single classification call.
 */
struct PlayingStyleResult
{
    int classIndex = 0;
    float confidence = 0.0f;
    std::string className;
};

/**
 * @brief Wraps an ONNX classification session for playing-style detection.
 *
 * Usage:
 *   PlayingStyleClassifier classifier;
 *   classifier.tryLoadModel();
 *   // On background thread:
 *   auto result = classifier.classify(melData); // 2048 floats
 *   int style = result.classIndex;
 */
class PlayingStyleClassifier final
{
public:
    PlayingStyleClassifier();
    ~PlayingStyleClassifier();

    /** @brief Load model from JUCE BinaryData. Returns true on success. */
    bool tryLoadModel();

    /** @brief Classify a mel-spectrogram window.
     *  @param melData 2048 floats (64 mel bands × 32 time frames), row-major
     *  @return classification result with class index and confidence */
    PlayingStyleResult classify(const float* melData) noexcept;

    /** @brief Human-readable label for logging. */
    static std::string classNameForIndex(int index);

    /** @brief Number of playing-style classes. */
    static constexpr int kNumClasses = 5;

    /** @brief Mel spectrogram input dimensions. */
    static constexpr int kMelBands = 64;
    static constexpr int kTimeFrames = 32;
    static constexpr int kInputSize = kMelBands * kTimeFrames; // 2048

    /** @brief Error counters for diagnostics. */
    uint64_t getLoadErrorCount() const noexcept;
    uint64_t getRunErrorCount() const noexcept;

    /** @brief Whether the model loaded successfully. */
    bool isLoaded() const noexcept { return loaded; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<uint64_t> loadErrorCount{ 0 };
    std::atomic<uint64_t> runErrorCount{ 0 };
    std::atomic<bool> loaded{ false };

#if !defined(MA_ENABLE_ONNX)
    // Stub: ONNX not compiled in
#endif
};
