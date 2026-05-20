#pragma once

/**
 * @file
 * @brief ONNX Runtime–based @ref IInference (Phase 2).
 */

#include "IInference.h"
#include <atomic>
#include <cstdint>
#include <memory>

/**
 * @brief Loads bundled `accompaniment_model.onnx` when @c MA_ENABLE_ONNX is defined.
 *
 * Call @ref tryLoadModel before use; if it returns false, @ref AccompanimentProcessor falls back to
 * @ref RuleBasedInference. Inference runs only on the background thread (see `ARCHITECTURE.md`).
 */
class OnnxInference final : public IInference
{
public:
    OnnxInference();
    ~OnnxInference() override;

    /** @brief Load model from JUCE BinaryData. Safe to call once; returns true on success. */
    bool tryLoadModel();

    void prepare(double sampleRate) override;
    int selectPattern(const FeatureVector& features, int excludeIndex = -1) override;
    std::string getName() const override;
    uint64_t getLoadErrorCount() const noexcept { return loadErrorCount.load(std::memory_order_relaxed); }
    uint64_t getRunErrorCount() const noexcept { return runErrorCount.load(std::memory_order_relaxed); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::atomic<uint64_t> loadErrorCount{ 0 };
    std::atomic<uint64_t> runErrorCount{ 0 };
};
