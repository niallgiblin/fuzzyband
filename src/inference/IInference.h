#pragma once

#include "analysis/FeatureVector.h"
#include <string>

class IInference
{
public:
    virtual ~IInference() = default;

    virtual void prepare(double sampleRate) = 0;
    virtual int selectPattern(const FeatureVector& features) = 0;
    virtual std::string getName() const = 0;
};
