#pragma once

#include "IStructureInference.h"
#include "analysis/StructureHoldSmoother.h"

/**
 * @brief Rule-only shadow: desired class = @c FeatureVector::state, then @ref StructureHoldSmoother.
 */
class RuleStructureInference final : public IStructureInference
{
public:
    void prepare(double sampleRate) override;
    void reset() override;
    void process(const FeatureVector& fv, double dtSec) override;
    const StructureShadowMetrics& getLastShadowMetrics() const override { return metrics; }
    std::string getName() const override;

private:
    StructureHoldSmoother hold;
    StructureShadowMetrics metrics;
    double sampleRate = 44100.0;
};
