#include "RuleBasedInference.h"
#include "pattern_rules.h"
#include <algorithm>

void RuleBasedInference::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
}

int RuleBasedInference::selectPattern(const FeatureVector& f, int excludeIndex)
{
    (void)sampleRate;

    int result = std::clamp(PatternRules::rulePatternForState(f), 0, 6);
    return PatternRules::applyExclusion(result, excludeIndex);
}
