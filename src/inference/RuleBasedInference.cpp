#include "RuleBasedInference.h"
#include "PolicyPatternMapper.h"

void RuleBasedInference::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
}

int RuleBasedInference::selectPattern(const FeatureVector& f)
{
    (void)sampleRate;

    const float bpmAdj = f.bpm + (f.policyIntensity - 0.5f) * 40.f;

    int base = 0;
    switch (f.state)
    {
        case StructureState::SILENT:
            base = 0;
            break;
        case StructureState::SOFT:
            if (bpmAdj < 120.0f)
                base = 1;
            else if (bpmAdj < 160.0f)
                base = 2;
            else
                base = 3;
            break;
        case StructureState::LOUD:
            if (bpmAdj < 160.0f)
                base = 4;
            else
                base = 5;
            break;
    }

    return PolicyPatternMapper::applyPatternPolicy(base, f);
}
