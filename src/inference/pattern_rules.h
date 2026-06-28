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
#include <cstring>

namespace PatternRules
{

static constexpr float kSoftMidBpmThreshold  = 120.0f;
static constexpr float kSoftLoudBpmThreshold = 160.0f;
static constexpr int   kPatternCount         = 22;

/**
 * @brief Apply policyIntensity offset to BPM.
 * Intensity 0.5 → neutral (no shift). Range maps [0,1] → [-20, +20] BPM.
 */
inline float adjustedBpm(const FeatureVector& f) noexcept
{
    const float raw = f.bpm + (f.policyIntensity - 0.5f) * 40.0f;
    return std::clamp(raw, 40.0f, 300.0f);
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
        case StructureState::SOFT:   return (patternIndex >= 1 && patternIndex <= 3) || patternIndex == 7 || patternIndex == 20;
        case StructureState::LOUD:   return (patternIndex >= 4 && patternIndex <= 5) || (patternIndex >= 8 && patternIndex <= 10) || (patternIndex >= 13 && patternIndex <= 14) || patternIndex == 6 || patternIndex == 9 || patternIndex == 15;
    }
    return false;
}

/**
 * @brief Whether an ONNX class prediction may be used at runtime.
 * Expansion indices 7+ are always acceptable when state-compatible.
 */
inline bool isOnnxPatternAcceptable(int patternIndex, const FeatureVector& f) noexcept
{
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
    if (base >= 7) return base;

    // SOFT patterns [1,3]
    if (base >= 1 && base <= 3)
    {
        // Half-time window: 6 of 8 bars for sludge feel
        if (f.rmsEnergy < 0.04f && (barMod8 % 8) < 6)
            return 7; // half-time feel
        return base;
    }

    // LOUD patterns [4,6]
    if (base >= 4 && base <= 6)
    {
        // Sludge half-time: BPM < 85, bars 0-1 of each 4-bar group
        if (f.bpm < 85.0f && (barMod8 % 4) < 2)
            return 7; // half-time feel
        if (f.rmsEnergy < 0.06f && f.bpm < 140.0f)
            return 9; // sparse
        if (f.bpm >= 160.0f && f.spectralCentroid > 800.0f)
            return 8; // blast beat
        if (f.bpm >= 140.0f && (barMod8 % 2) == 0)
            return 10; // thrash
        return base;
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

// ══════════════════════════════════════════════════════════════════════════
// Section → Pattern Pool mapping (D006: structure-driven selection)
// ══════════════════════════════════════════════════════════════════════════

/**
 * @brief Pattern indices for each section type.
 * Each pool has 2-4 patterns that rotate on bar boundaries for variety.
 */
struct SectionPatternPool
{
    int count;
    int indices[4];
};

/**
 * @brief Map a section name to its pattern pool.
 * Returns pool with count=0 if section unknown (fallback to rulePatternForState).
 */
inline SectionPatternPool sectionPatternPool(const char* sectionName) noexcept
{
    using P = SectionPatternPool;

    if (!sectionName) return P{ 0, {} };

    if (std::strcmp(sectionName, "INTRO") == 0)
        return P{ 2, { 11, 12 } };                // Intro Build, Intro Full
    if (std::strcmp(sectionName, "VERSE") == 0)
        return P{ 3, { 1, 2, 3 } };               // Verse Groove, Half-Time, Fast
    if (std::strcmp(sectionName, "CHORUS") == 0)
        return P{ 3, { 4, 14, 21 } };              // Chorus Mid, Open Groove, Blast
    if (std::strcmp(sectionName, "BREAKDOWN") == 0)
        return P{ 3, { 6, 15, 9 } };               // Heavy, Full, Sparse
    if (std::strcmp(sectionName, "SOLO") == 0)
        return P{ 2, { 4, 14 } };                  // Chorus Mid, Open Groove
    if (std::strcmp(sectionName, "OUTRO") == 0)
        return P{ 1, { 16 } };                     // Outro Decay

    return P{ 0, {} };
}

/**
 * @brief M009: Map playing style classifier output (0-4) to pattern indices.
 */
inline SectionPatternPool stylePatternPool(int styleIndex) noexcept
{
    using P = SectionPatternPool;
    switch (styleIndex)
    {
        case 0: return P{ 3, { 7, 1, 9 } };   // Palm mute chugs: half-time, verse groove, sparse breakdown
        case 1: return P{ 3, { 4, 6, 14 } };  // Open chord: heavy chorus, breakdown, open groove
        case 2: return P{ 3, { 3, 10, 2 } };  // Single note runs: verse fast, thrash, half-time
        case 3: return P{ 3, { 6, 9, 7 } };   // Sustain/drone: breakdown, sparse, ambient half-time
        case 4: return P{ 1, { 0 } };          // Silence → Silent
        default: return P{ 0, {} };
    }
}

/**
 * @brief Select a fill pattern index based on transition type.
 */
inline int selectFillPattern(int barsRemaining) noexcept
{
    if (barsRemaining <= 0) return 19;  // Big fill on last bar
    return 17;                          // Short fill otherwise
}

} // namespace PatternRules
