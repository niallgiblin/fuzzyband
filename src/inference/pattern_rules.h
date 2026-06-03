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
static constexpr int   kPatternCount         = 11;

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
 * SILENT→0, SOFT→1-3 or 7, LOUD→4-5 or 8-10.
 */
inline bool isPatternCompatibleWithState(int patternIndex, StructureState state) noexcept
{
    switch (state)
    {
        case StructureState::SILENT: return patternIndex == 0;
        case StructureState::SOFT:  return (patternIndex >= 1 && patternIndex <= 3) || patternIndex == 7;
        case StructureState::LOUD:  return (patternIndex >= 4 && patternIndex <= 5) || (patternIndex >= 8 && patternIndex <= 10);
    }
    return false;
}

static constexpr float kBreakdownBpmThreshold = 110.0f;

/**
 * @brief Whether an ONNX class prediction may be used at runtime.
 * Class 6 (Breakdown) is allowed for non-SILENT rows with raw BPM below the training oracle threshold.
 * Expansion indices 7-10 are always acceptable when state-compatible.
 * Exclusion cycling still uses isPatternCompatibleWithState (Breakdown never selected via wrap).
 */
inline bool isOnnxPatternAcceptable(int patternIndex, const FeatureVector& f) noexcept
{
    if (patternIndex == 6)
        return f.state != StructureState::SILENT && f.bpm < kBreakdownBpmThreshold;
    if (patternIndex >= 7 && patternIndex <= 10)
        return isPatternCompatibleWithState(patternIndex, f.state);
    return isPatternCompatibleWithState(patternIndex, f.state);
}

/**
 * @brief D-23-07 diversifyPattern: route within structural categories using energy/centroid/BPM/bar-phase.
 * Deterministic, no ONNX dependency — expands the base pattern selected by rule or ONNX classifier
 * into musically distinct groove variants (half-time, blast, sparse breakdown, thrash).
 */
inline int diversifyPattern(int base, const FeatureVector& f, int barMod8) noexcept
{
    // SILENT never diversifies
    if (base == 0) return 0;

    // New patterns map to themselves (already diversified)
    if (base >= 7 && base <= 10) return base;

    // SOFT patterns [1,3]
    if (base >= 1 && base <= 3)
    {
        if (f.rmsEnergy < 0.04f && (barMod8 % 4) < 2)
            return 7; // half-time feel
        return base;
    }

    // LOUD patterns [4,5]
    if (base >= 4 && base <= 5)
    {
        if (f.rmsEnergy < 0.06f && f.bpm < 140.0f)
            return 9; // sparse breakdown
        if (f.bpm >= 160.0f && f.spectralCentroid > 800.0f)
            return 8; // blast beat
        if (f.bpm >= 140.0f && (barMod8 % 2) == 0)
            return 10; // thrash
        return base;
    }

    // Breakdown (6)
    if (base == 6)
    {
        if (f.rmsEnergy < 0.03f && (barMod8 % 4) == 0)
            return 9; // sparse breakdown
        return 6;
    }

    return base;
}

/**
 * @brief D-23-04 single-shot exclusion: if result == excludeIndex, scan forward modulo-kPatternCount
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
