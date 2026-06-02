#pragma once

/**
 * @file
 * @brief Shared pattern-selection rules used by both RuleBasedInference and OnnxInference.
 *
 * Header-only, stateless inline functions. Single source of truth for BPM thresholds
 * and exclusion logic — prevents threshold drift between rule-based and ONNX fallback paths.
 * ARCH-03.
 */

#include "analysis/FeatureVector.h"
#include <algorithm>

namespace PatternRules
{

static constexpr float kSoftMidBpmThreshold  = 120.0f;
static constexpr float kSoftLoudBpmThreshold = 160.0f;
static constexpr int   kPatternCount         = 7;

/**
 * @brief Apply policyIntensity offset to BPM.
 * Intensity 0.5 → neutral (no shift). Range maps [0,1] → [-20, +20] BPM.
 */
inline float adjustedBpm(const FeatureVector& f) noexcept
{
    const float raw = f.bpm + (f.policyIntensity - 0.5f) * 40.0f;
    return std::clamp(raw, 80.0f, 220.0f);
}

/**
 * @brief Select the rule-based pattern index for the given feature vector.
 * Uses adjustedBpm and structural state to pick within [0,5].
 */
inline int rulePatternForState(const FeatureVector& f) noexcept
{
    const float bpmAdj = adjustedBpm(f);
    switch (f.state)
    {
        case StructureState::SILENT:
            return 0;
        case StructureState::SOFT:
            if (bpmAdj < kSoftMidBpmThreshold)  return 1;
            if (bpmAdj < kSoftLoudBpmThreshold) return 2;
            return 3;
        case StructureState::LOUD:
            return bpmAdj < kSoftLoudBpmThreshold ? 4 : 5;
    }
    return 0;
}

/**
 * @brief Return true if patternIndex is appropriate for the given structural state.
 * SILENT→0, SOFT→1-3, LOUD→4-5.
 */
inline bool isPatternCompatibleWithState(int patternIndex, StructureState state) noexcept
{
    switch (state)
    {
        case StructureState::SILENT: return patternIndex == 0;
        case StructureState::SOFT:  return patternIndex >= 1 && patternIndex <= 3;
        case StructureState::LOUD:  return patternIndex >= 4 && patternIndex <= 5;
    }
    return false;
}

static constexpr float kBreakdownBpmThreshold = 110.0f;

/**
 * @brief Whether an ONNX class prediction may be used at runtime.
 * Class 6 (Breakdown) is allowed for non-SILENT rows with raw BPM below the training oracle threshold.
 * Exclusion cycling still uses isPatternCompatibleWithState (Breakdown never selected via wrap).
 */
inline bool isOnnxPatternAcceptable(int patternIndex, const FeatureVector& f) noexcept
{
    if (patternIndex == 6)
        return f.state != StructureState::SILENT && f.bpm < kBreakdownBpmThreshold;
    return isPatternCompatibleWithState(patternIndex, f.state);
}

/**
 * @brief D-23-04 single-shot exclusion: if result == excludeIndex, scan forward modulo-7
 * for the next state-compatible pattern. Pass excludeIndex == -1 to disable.
 * When no compatible candidate exists, returns fallbackPattern (rule/state default).
 */
inline int applyExclusion(
    int result,
    int excludeIndex,
    StructureState state,
    int fallbackPattern) noexcept
{
    if (excludeIndex < 0 || result != excludeIndex)
        return result;
    for (int step = 1; step <= kPatternCount; ++step)
    {
        const int candidate = (excludeIndex + step) % kPatternCount;
        if (isPatternCompatibleWithState(candidate, state))
            return candidate;
    }
    return fallbackPattern;
}

} // namespace PatternRules
