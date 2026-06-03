#pragma once

#include "IStructureInference.h"
#include "analysis/StructureHoldSmoother.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

/**
 * @brief Windowed ONNX structure head over @c structure_model.onnx (Phase 12 shadow path).
 */
class OnnxStructureInference final : public IStructureInference
{
public:
    OnnxStructureInference();
    ~OnnxStructureInference() override;

    /** @brief Load from JUCE BinaryData (`structure_model.onnx`). */
    bool tryLoadModel();

    void prepare(double sampleRate) override;
    void reset() override;
    void process(const FeatureVector& fv, double dtSec) override;
    const StructureShadowMetrics& getLastShadowMetrics() const override { return metrics; }
    std::string getName() const override;
    uint64_t getLoadErrorCount() const noexcept { return loadErrorCount.load(std::memory_order_relaxed); }
    uint64_t getRunErrorCount() const noexcept { return runErrorCount.load(std::memory_order_relaxed); }

    // S02: ONNX structure confidence gate thresholds.
    // softmaxTop1 must be at least this for the top class to be considered valid.
    static constexpr float kSoftmaxConfidenceThreshold = 0.75f;  // was 0.6
    // margin (top1 − top2) must be at least this for the classification to be unambiguous.
    static constexpr float kMarginThreshold = 0.20f;             // was 0.1

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    StructureHoldSmoother hold;
    StructureShadowMetrics metrics;

    double sampleRate = 44100.0;
    int64_t lastSampleTimestamp = -1;

    std::array<FeatureVector, 12> window{};
    std::atomic<uint64_t> loadErrorCount{ 0 };
    std::atomic<uint64_t> runErrorCount{ 0 };

    void pushSnapshot(const FeatureVector& fv);
    void runRuleFallback(const FeatureVector& fv, double dtSec);
};
