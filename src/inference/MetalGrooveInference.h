#pragma once

/**
 * @file
 * @brief ONNX-based groove selector using mel-spectrogram CNN backbone (v0.8.0).
 *
 * Loads metal_groove.onnx. Runs groove embedding inference every drain (~50Hz).
 * Uses precomputed pattern bottleneck centroids (cosine similarity nearest-neighbor)
 * for pattern selection.
 *
 * Replaces the scalar-feature OnnxInference for the unified Mel-CNN pipeline.
 * OnnxInference remains as fallback.
 */

#include "IInference.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class MetalGrooveInference final : public IInference
{
public:
    MetalGrooveInference();
    ~MetalGrooveInference() override;

    /** @brief Load metal_groove.onnx from JUCE BinaryData. Returns true on success. */
    bool tryLoadModel();

    void prepare(double sampleRate) override;

    /** @brief Scalar-feature fallback — delegates to rule-based when mel path unavailable.
     *  Prefer selectPatternFromMel() for real inference. */
    int selectPattern(const FeatureVector& features, int excludeIndex = -1) override;

    std::string getName() const override;

    /** @brief Mel-spectrogram-driven inference — the real method.
     *  @param melData 2048 floats [64 bands × 32 frames] row-major mel-dB
     *  @param excludeIndex pattern index to exclude (-1 = none)
     *  @return pattern index [0, 21] */
    int selectPatternFromMel(const float* melData, int excludeIndex = -1);

    /** @brief Style classification from mel data (free, from same inference output).
     *  @return style index 0-4 (palm_mute, open_chord, single_note, sustain, silence) */
    int classifyStyle(const float* melData);

    uint64_t getLoadErrorCount() const noexcept;
    uint64_t getRunErrorCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<uint64_t> loadErrorCount{0};
    std::atomic<uint64_t> runErrorCount{0};
};
