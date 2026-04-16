#pragma once

#include "IInference.h"

class RuleBasedInference final : public IInference
{
public:
    void prepare(double sampleRate) override;
    int selectPattern(const FeatureVector& features) override;
    std::string getName() const override { return "RuleBasedInference"; }

private:
    double sampleRate = 44100.0;
};
