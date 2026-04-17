#pragma once

/**
 * @file
 * @brief Shared post-processing of pattern indices from rule / ONNX paths (PUI-01).
 */

#include "analysis/FeatureVector.h"

namespace PolicyPatternMapper
{
/** @brief Apply genre + variation remap to a base pattern index in [0, 6]. */
int applyPatternPolicy(int baseIndex, const FeatureVector& fv);
}