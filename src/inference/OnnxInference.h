#pragma once

#include "IInference.h"

/** Phase 2 placeholder — compiled but not used in Phase 1. */
class OnnxInference final : public IInference
{
public:
    void prepare(double sampleRate) override;
    int selectPattern(const FeatureVector& features) override;
    std::string getName() const override;
};
