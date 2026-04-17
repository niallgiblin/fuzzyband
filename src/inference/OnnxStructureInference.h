#pragma once

#include "IStructureInference.h"
#include "analysis/StructureHoldSmoother.h"
#include <array>
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    StructureHoldSmoother hold;
    StructureShadowMetrics metrics;

    double sampleRate = 44100.0;
    int64_t lastSampleTimestamp = -1;

    std::array<FeatureVector, 12> window{};

    void pushSnapshot(const FeatureVector& fv);
    void runRuleFallback(const FeatureVector& fv, double dtSec);
};
