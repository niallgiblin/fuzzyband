#include "PolicyPatternMapper.h"
#include <algorithm>
#include <cmath>

namespace PolicyPatternMapper
{
int applyPatternPolicy(int baseIndex, const FeatureVector& fv)
{
    int idx = std::clamp(baseIndex, 0, 6);
    idx = (idx + fv.policyGenreIndex) % 7;
    const int varShift = static_cast<int>(std::lround(fv.policyVariation * 2.f)) - 1;
    idx = (idx + varShift + 7) % 7;
    return idx;
}
}