#include "RuleStructureInference.h"

void RuleStructureInference::prepare(double sr)
{
    sampleRate = (sr > 0.0) ? sr : 44100.0;
    reset();
}

void RuleStructureInference::reset()
{
    hold.reset();
    metrics = {};
}

void RuleStructureInference::process(const FeatureVector& fv, double dtSec)
{
    (void)sampleRate;
    const StructureState desired = fv.state;
    const StructureState smoothed = hold.advance(desired, dtSec);

    metrics.ruleState = fv.state;
    metrics.mlValid = false;
    metrics.smoothedState = smoothed;
    metrics.softmaxTop1 = 1.0f;
    metrics.margin = 1.0f;
    metrics.agreeRule = (smoothed == fv.state);
}

std::string RuleStructureInference::getName() const
{
    return "RuleStructureInference";
}
