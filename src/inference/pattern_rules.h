#pragma once

/**
 * @file
 * @brief Shared pattern-selection rules used by both RuleBasedInference and MetalGrooveInference.
 *
 * Header-only, stateless inline functions. Single source of truth for BPM thresholds
 * and exclusion logic — prevents threshold drift between rule-based and ONNX fallback paths.
 * ARCH-03.
 */

#include "analysis/FeatureVector.h"
#include "midi/MidiPatternLibrary.h"
#include "midi/GrooveTemplate.h"
#include "inference/PatternPriors.h"
#include <algorithm>
#include <cstring>

namespace PatternRules
{

static constexpr float kSoftMidBpmThreshold  = 120.0f;
static constexpr float kSoftLoudBpmThreshold = 160.0f;

/** @brief Total pattern count; kept in sync with MidiPatternLibrary (unit-tested). */
static constexpr int kPatternCount = MidiPatternLibrary::kPatternCount;

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
 * SILENT→0, SOFT→1-3,7,20,22-24,26-27, LOUD→4-6,8-10,13-15,25.
 */
inline bool isPatternCompatibleWithState(int patternIndex, StructureState state) noexcept
{
    switch (state)
    {
        case StructureState::SILENT: return patternIndex == 0;
        case StructureState::SOFT:
            return (patternIndex >= 1 && patternIndex <= 3) || patternIndex == 7 || patternIndex == 20
                || patternIndex == 22 || patternIndex == 23 || patternIndex == 24
                || patternIndex == 26 || patternIndex == 27;
        case StructureState::LOUD:
            return (patternIndex >= 4 && patternIndex <= 5) || (patternIndex >= 8 && patternIndex <= 10)
                || (patternIndex >= 13 && patternIndex <= 14) || patternIndex == 6
                || patternIndex == 9 || patternIndex == 15 || patternIndex == 25;
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
 * @brief B1: genre-aware diversification for the reactive path.
 * Rock-leaning genres (Rock, Hard Rock, Punk, Classic Rock, Alternative, Grunge)
 * route the ONNX mel selector's metal-era indices (7-21) into the rock
 * vocabulary by structure state + energy — so a "Rock" genre in follow mode does
 * not default to metal-extreme picks (blast / thrash) — and keep the original
 * routing for indices 1-6. Metal-family genres keep the original metal routing.
 */
inline int diversifyPatternForGenre(int base, const FeatureVector& f, int barMod8, int genreId) noexcept
{
    if (Groove::isMetalFamily(genreId))
        return diversifyPattern(base, f, barMod8);

    if (base == 0) return 0;

    // ── Rock-leaning genres: re-home mel-selector indices 7-21 ──────────────
    // The ONNX mel selector returns any of indices 0-21 (all metal-era
    // grooves). Previously these were returned unchanged, so a "Rock" genre in
    // follow mode defaulted to metal-extreme picks (blast / thrash). Re-home
    // them into the rock vocabulary by structure state + energy, always
    // respecting state compatibility so a groove never contradicts the
    // detected energy. Metal-family genres are handled above and are unchanged.
    // Base 1-6 keep the original routing (deliberately preserved).
    if (base >= 7)
    {
        switch (f.state)
        {
            case StructureState::SILENT: return 0;
            case StructureState::SOFT:
                if (f.rmsEnergy < 0.04f)
                    return (barMod8 % 2 == 0) ? 22 : 23;         // Rock Backbeat / Rock Half-Time
                return (barMod8 % 3 == 0) ? 1 : ((barMod8 % 3 == 1) ? 22 : 26); // Verse Groove / Rock Backbeat / Rock Ballad
            case StructureState::LOUD:
                if (f.rmsEnergy < 0.06f && f.bpm < 140.0f)
                    return 9;                                    // sparse breakdown (shared)
                if (f.bpm < 130.0f && f.rmsEnergy < 0.09f)
                    return (barMod8 % 2 == 0) ? 24 : 25;         // Rock Shuffle / Punk D-Beat
                if (f.bpm >= 170.0f)
                    return (barMod8 % 2 == 0) ? 25 : 24;         // fast hard-rock/punk: d-beat / shuffle
                return (barMod8 % 2 == 0) ? 4 : 14;              // rock chorus: Chorus Mid / Chorus Open
        }
        return base;
    }

    if (base >= 1 && base <= 3)  // SOFT
    {
        if (f.rmsEnergy < 0.04f)
            return (barMod8 % 2 == 0) ? 22 : 23;  // Rock Backbeat / Rock Half-Time
        return base;
    }

    if (base >= 4 && base <= 6)  // LOUD
    {
        if (f.rmsEnergy < 0.06f && f.bpm < 140.0f)
            return 9;  // sparse
        if (f.bpm >= 160.0f && f.spectralCentroid > 800.0f)
            return 8;  // blast beat
        if (f.bpm < 130.0f && f.rmsEnergy < 0.09f)
            return (barMod8 % 2 == 0) ? 24 : 25;  // Rock Shuffle / Punk D-Beat
        return base;
    }

    return base;
}

/**
 * @brief R1 (rhythm-driven selection): correct a base groove toward the density
 * the guitarist is actually playing. The timbre/energy selector picks a "family"
 * but cannot see that the player is chugging 16ths vs holding half-notes. Only
 * the unambiguous DENSE case steers: >=1.8 attacks/beat (8th-note chugging or
 * faster) → a state-compatible denser groove. Sparse/sparse-ish rhythm is left
 * to the energy/BPM rules (a LOUD sustained note has few attacks but is not
 * "sparse playing", so density alone must not soften it). Deterministic, stateless, RT-safe.
 */
inline int refineByRhythm(int base, const FeatureVector& f) noexcept
{
    if (base == 0)
        return 0;
    const float density = std::clamp(f.onsetDensityPerBeat, 0.0f, 8.0f);
    if (density < 1.8f)
        return base;   // only clearly-dense picking steers; mid/sparse left to rules
    switch (f.state)
    {
        case StructureState::LOUD:
            if (isPatternCompatibleWithState(10, f.state)) return 10;  // thrash
            if (isPatternCompatibleWithState(8, f.state)) return 8;    // blast
            break;
        case StructureState::SOFT:
            if (isPatternCompatibleWithState(3, f.state)) return 3;    // verse fast
            if (isPatternCompatibleWithState(7, f.state)) return 7;    // half-time
            break;
        default: break;
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
 * @brief B1: genre-aware section → pattern pool mapping for play (song-form) mode.
 * Rock-leaning genres prefer the rock-first pattern set; Metal-family genres
 * keep the original metal pools.
 */
inline SectionPatternPool sectionPatternPoolForGenre(const char* sectionName, int genreId) noexcept
{
    using P = SectionPatternPool;

    if (!sectionName)
        return P{ 0, {} };

    if (Groove::isMetalFamily(genreId))
        return sectionPatternPool(sectionName);

    if (std::strcmp(sectionName, "INTRO") == 0)
        return P{ 2, { 11, 12 } };
    if (std::strcmp(sectionName, "VERSE") == 0)
        return P{ 4, { 22, 23, 1, 2 } };         // Rock Backbeat, Rock Half-Time, Verse Groove, Verse Half-Time
    if (std::strcmp(sectionName, "CHORUS") == 0)
        return P{ 4, { 4, 24, 25, 14 } };         // Chorus Mid, Rock Shuffle, Punk D-Beat, Chorus Open
    if (std::strcmp(sectionName, "BREAKDOWN") == 0)
        return P{ 3, { 6, 15, 9 } };
    if (std::strcmp(sectionName, "SOLO") == 0)
        return P{ 3, { 4, 14, 24 } };
    if (std::strcmp(sectionName, "OUTRO") == 0)
        return P{ 2, { 16, 26 } };                // Outro Decay, Rock Ballad

    return P{ 0, {} };
}
// ══════════════════════════════════════════════════════════════════════════
// C2 (DATA_STRATEGY.md §6.2): data-derived Lakh selection priors → pool ordering
// ══════════════════════════════════════════════════════════════════════════

/**
 * @brief Data-derived popularity weight for (patternIndex, genreId) in [0,1].
 * Baked at build time in PatternPriors.h from rock-weighted Lakh subsets with
 * content-derived tempo. Returns 0 for out-of-range inputs.
 */
inline float priorWeight(int patternIndex, int genreId) noexcept
{
    if (genreId < 0 || genreId >= PatternPriors::kNumGenres) return 0.0f;
    if (patternIndex < 0 || patternIndex >= PatternPriors::kPatternCount) return 0.0f;
    return PatternPriors::kWeight[genreId][patternIndex];
}

/**
 * @brief Reorder a pool's indices by descending selection prior (most common
 * groove for the genre first), stable on ties. Pure reordering of <=4 ints — no
 * RT allocation, no change to which patterns are in the pool. This is the
 * build-time "pool weighting" hook: consumers may prefer this ordering to bias
 * selection toward in-distribution grooves. Out-of-range genre leaves the pool
 * unchanged.
 */
inline SectionPatternPool orderPoolByPriors(SectionPatternPool pool, int genreId) noexcept
{
    if (genreId < 0 || genreId >= PatternPriors::kNumGenres) return pool;
    for (int i = 1; i < pool.count; ++i)
    {
        const int cur = pool.indices[i];
        const float cw = priorWeight(cur, genreId);
        int j = i - 1;
        while (j >= 0 && priorWeight(pool.indices[j], genreId) < cw)
        {
            pool.indices[j + 1] = pool.indices[j];
            --j;
        }
        pool.indices[j + 1] = cur;
    }
    return pool;
}

/**
 * @brief Genre-aware section pool ordered by data-derived Lakh priors.
 * Same membership as sectionPatternPoolForGenre, ordered most-popular-first.
 */
inline SectionPatternPool orderedSectionPatternPoolForGenre(const char* sectionName, int genreId) noexcept
{
    return orderPoolByPriors(sectionPatternPoolForGenre(sectionName, genreId), genreId);
}

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
 * @brief B1/G2: genre-aware playing-style → groove-family pool.
 *
 * Rock-leaning genres steer the style pool into the rock-first vocabulary
 * (indices 22-27) so the perception head makes a "Rock" genre sound like rock
 * rather than pulling it back toward metal. Metal-family genres keep the
 * original metal style pools. Membership is still filtered against the live
 * structure state by diversifyPatternForStyle, so a style can never force a
 * structurally-wrong groove.
 */
inline SectionPatternPool stylePatternPoolForGenre(int styleIndex, int genreId) noexcept
{
    using P = SectionPatternPool;

    if (Groove::isMetalFamily(genreId))
        return stylePatternPool(styleIndex);

    switch (styleIndex)
    {
        case 0: return P{ 3, { 23, 22, 9 } };   // Palm mute chugs → Rock Half-Time, Rock Backbeat, Sparse Breakdown
        case 1: return P{ 3, { 22, 4, 14 } };   // Open chord → Rock Backbeat, Chorus Mid, Chorus Open
        case 2: return P{ 3, { 3, 24, 25 } };   // Single note runs → Verse Fast, Rock Shuffle, Punk D-Beat
        case 3: return P{ 3, { 26, 9, 7 } };    // Sustain/drone → Rock Ballad, Sparse Breakdown, Half-Time
        case 4: return P{ 1, { 0 } };           // Silence → Silent
        default: return P{ 0, {} };
    }
}

/**
 * @brief Last-bar fill sized to energy, varied by phrase seed (T7.1).
 *
 * Loud ends alternate 18/19 so Fill Big is reachable even when the caller
 * used to pass seed 0 (Record). @p seed is mixed so consecutive phrases
 * diverge without depending on raw parity of a constant.
 */
inline int selectFillPatternForEnergy(float rmsEnergy, unsigned seed = 0) noexcept
{
    unsigned h = seed * 0x9E3779B1u + 19u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    if (rmsEnergy >= 0.45f) return (h & 1u) ? 19 : 18;  // loud → big or medium
    if (rmsEnergy >= 0.20f) return (h & 1u) ? 18 : 17;  // mid  → medium or short
    return 17;                                          // quiet → short
}

/**
 * @brief Select a fill pattern index based on transition type.
 *
 * Last-bar fills are sized to the section-end energy (loud → big/medium,
 * quiet → short) and varied within the tier by @p seed, so a repeated section
 * never plays the identical fill twice. Fill Medium (18) — previously dead —
 * is now reachable. Mid-section calls (barsRemaining > 0) stay on the short
 * fill as a subtle build.
 *
 * @param barsRemaining bars until the section end (0 = last bar)
 * @param rmsEnergy     current input RMS (0..1)
 * @param seed          per-section-instance seed for within-tier variety
 */
inline int selectFillPattern(int barsRemaining, float rmsEnergy = 1.0f, unsigned seed = 0) noexcept
{
    if (barsRemaining > 0) return 17;                 // mid-section build: short fill
    return selectFillPatternForEnergy(rmsEnergy, seed);
}

// ══════════════════════════════════════════════════════════════════════════
// Pool phrasing & seeded rotation (variety): hold each groove for a musical
// phrase (2-4 bars), seed the rotation per section *instance* so verse 1 and
// verse 2 diverge, and exclude the immediately-previous groove. All
// deterministic, allocation-free, audio-thread safe (≤4 pool members).
// ══════════════════════════════════════════════════════════════════════════

/** @brief SplitMix32-style avalanche of (seed, slot) — good low-bit diffusion
 *  so even seed 0 spreads pool members. Deterministic per (seed, slot). */
inline unsigned hashMix(unsigned a, unsigned b) noexcept
{
    unsigned h = a * 0x9E3779B1u + b;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

/**
 * @brief How many bars one groove is held before rotating (phrasing, A4.3).
 * Breakdown/Intro/Outro hold longer (4 bars); VERSE/CHORUS/SOLO move every 2.
 */
inline int barsPerGrooveForSection(const char* sectionName) noexcept
{
    if (sectionName == nullptr) return 2;
    if (std::strcmp(sectionName, "BREAKDOWN") == 0) return 4;
    if (std::strcmp(sectionName, "INTRO") == 0)     return 4;
    if (std::strcmp(sectionName, "OUTRO") == 0)     return 4;
    return 2;  // VERSE / CHORUS / SOLO / default
}

/**
 * @brief Pick one pattern from a section pool for a groove slot.
 *
 * Uniform seed-hashed pick (NOT prior-weighted: the Lakh priors are heavily
 * peaked — many 0.0 / 1.0 weights — so weighting would collapse a pool to a
 * single pattern, killing variety; priors stay as build-time pool *ordering*).
 * The pick is deterministic per (seed, grooveSlot) — a section instance plays
 * the same phrases on every repeat — and `excludeIndex` (the previously played
 * groove) is never picked again, so consecutive phrases always differ.
 *
 * @param pool         the section's pattern pool (≤4 members)
 * @param seed         per-section-instance seed (e.g. global bar at entry)
 * @param grooveSlot   phrase index within the section (bar / barsPerGroove)
 * @param excludeIndex pattern played in the previous phrase (-1 = none)
 * @return picked pattern index, or -1 if the pool is empty
 */
inline int pickPoolPattern(const SectionPatternPool& pool, unsigned seed,
                           int grooveSlot, int excludeIndex) noexcept
{
    if (pool.count <= 0) return -1;
    if (pool.count == 1) return pool.indices[0];

    const unsigned h = hashMix(seed, static_cast<unsigned>(grooveSlot));
    const int chosen = pool.indices[static_cast<int>(h % static_cast<unsigned>(pool.count))];

    if (chosen != excludeIndex)
        return chosen;

    // Avoid an immediate repeat: step forward to the next member != exclude.
    int pos = 0;
    for (int i = 0; i < pool.count; ++i)
        if (pool.indices[i] == chosen) { pos = i; break; }
    for (int s = 1; s < pool.count; ++s)
    {
        const int cand = pool.indices[(pos + s) % pool.count];
        if (cand != excludeIndex) return cand;
    }
    return chosen;  // pool of one distinct pattern
}

/**
 * @brief A4.1: route the selection through the playing-style pool when the
 * perception head has classified a stable articulation.
 *
 * stylePatternPool maps style → groove family: palm-mute chugs → half-time /
 * breakdown, open chords → chorus / breakdown, single-note runs → fast / thrash,
 * sustain → sparse. The style pool is filtered to the current structure state
 * (SOFT/LOUD) so a style can never force a structurally-wrong pattern, rotated
 * by bar phase for variety. Silence, unknown styles and empty/state-filtered
 * pools leave the base selection untouched.
 *
 * @param base       the (genre-diversified) selection before style steering
 * @param styleIndex perception class 0..4 (4 = silence)
 * @param barMod8    bar phase in [0,7] for pool rotation
 * @param state      current structure state (state-compat filter)
 * @return style-steered pattern index, or @p base when no steering applies
 */
inline int diversifyPatternForStyle(int base, int styleIndex, int barMod8, StructureState state) noexcept
{
    if (base == 0 || styleIndex < 0 || styleIndex > 3) return base;

    const SectionPatternPool pool = stylePatternPool(styleIndex);
    if (pool.count <= 0) return base;

    int compat[4];
    int n = 0;
    for (int i = 0; i < pool.count && n < 4; ++i)
        if (isPatternCompatibleWithState(pool.indices[i], state))
            compat[n++] = pool.indices[i];
    if (n == 0) return base;

    const int idx = barMod8 % n;
    return compat[idx];
}

/**
 * @brief B1/G2: genre-aware style steering.
 *
 * Same contract as diversifyPatternForStyle, but the style → groove-family
 * pool is selected by genre (see stylePatternPoolForGenre): rock-leaning
 * genres (0-2) steer into the rock-first vocabulary, Metal/Sludge (>= 3) keep
 * the original metal pools. The pool is still filtered to the live structure
 * state, rotated by bar phase, and leaves the base untouched on silence /
 * unknown / empty pools.
 */
inline int diversifyPatternForStyle(int base, int styleIndex, int barMod8, StructureState state, int genreId) noexcept
{
    if (base == 0 || styleIndex < 0 || styleIndex > 3) return base;

    const SectionPatternPool pool = stylePatternPoolForGenre(styleIndex, genreId);
    if (pool.count <= 0) return base;

    int compat[4];
    int n = 0;
    for (int i = 0; i < pool.count && n < 4; ++i)
        if (isPatternCompatibleWithState(pool.indices[i], state))
            compat[n++] = pool.indices[i];
    if (n == 0) return base;

    const int idx = barMod8 % n;
    return compat[idx];
}

// ══════════════════════════════════════════════════════════════════════════
// Post-lock transition grammar (A5.2): which section follows a groove-lock
// expiry, and which follows that. Deterministic, data-informed (Lakh priors).
// ══════════════════════════════════════════════════════════════════════════

/** @brief One decision in the transition grammar: a target section + its pool. */
struct TransitionSection
{
    const char* name = "VERSE";
    SectionPatternPool pool{};
};

/**
 * @brief The section family a pattern index belongs to (for contrast routing).
 * Determined by pool membership (first section whose pool contains it).
 */
inline const char* sectionFamilyOfPattern(int patternIndex, int genreId) noexcept
{
    // Check in musical-energy order so ambiguous patterns (e.g. 4 = chorus-mid
    // AND solo-open) resolve to their most structural home first.
    const char* sections[] = { "CHORUS", "BREAKDOWN", "VERSE", "SOLO", "INTRO", "OUTRO" };
    for (const char* name : sections)
    {
        auto pool = sectionPatternPoolForGenre(name, genreId);
        for (int i = 0; i < pool.count; ++i)
            if (pool.indices[i] == patternIndex)
                return name;
    }
    return "VERSE"; // default family for unknown indices
}

/**
 * @brief Data-derived popularity of a section pool for the genre (sum of priors).
 */
inline float sectionPoolPopularity(const char* sectionName, int genreId) noexcept
{
    auto pool = sectionPatternPoolForGenre(sectionName, genreId);
    float total = 0.0f;
    for (int i = 0; i < pool.count; ++i)
        total += priorWeight(pool.indices[i], genreId);
    return total;
}

/**
 * @brief Pick the next section after a groove lock expires.
 *
 * The locked riff's pattern maps to a *family* (VERSE/CHORUS/BREAKDOWN/…). The
 * next section must be a **contrast** — never the same family as the riff, and
 * not any already-assigned contrast slot (so A→B→A→C keeps B and C distinct).
 * Among candidates, the most data-derived popular pool for the genre wins
 * (Lakh priors, C2). Deterministic, no allocation, audio-thread safe.
 *
 * @param lockedPatternIndex the pattern that was frozen by the lock
 * @param genreId            genre preset id (for priors + genre pools)
 * @param avoidSections      names already used by earlier contrast slots
 * @param avoidCount         number of entries in @p avoidSections
 * @return the chosen TransitionSection (name + ordered pool)
 */
inline TransitionSection pickNextSectionAfterLock(int lockedPatternIndex, int genreId,
                                                  const char* const* avoidSections, int avoidCount) noexcept
{
    const char* family = sectionFamilyOfPattern(lockedPatternIndex, genreId);

    auto isAvoided = [avoidSections, avoidCount](const char* name) noexcept -> bool
    {
        if (name == nullptr || avoidSections == nullptr)
            return false;
        for (int i = 0; i < avoidCount; ++i)
        {
            const char* a = avoidSections[i];
            if (a != nullptr && a[0] != '\0' && std::strcmp(name, a) == 0)
                return true;
        }
        return false;
    };

    // Contrast ladder: families ordered by musical energy, skipping the riff's
    // own family. We score candidates by Lakh popularity and pick the winner.
    const char* ladder[] = { "VERSE", "CHORUS", "BREAKDOWN", "SOLO", "INTRO", "OUTRO" };
    const char* best = nullptr;
    float bestScore = -1.0f;

    for (const char* name : ladder)
    {
        if (std::strcmp(name, family) == 0)
            continue;                       // never repeat the riff's own feel
        if (isAvoided(name))
            continue;                       // never reuse an earlier contrast slot

        const float score = sectionPoolPopularity(name, genreId);
        if (score > bestScore)
        {
            bestScore = score;
            best = name;
        }
    }

    // Last-resort fallback: any section that is not the riff's own family.
    if (best == nullptr)
    {
        for (const char* name : ladder)
        {
            if (std::strcmp(name, family) != 0)
            {
                best = name;
                break;
            }
        }
    }
    if (best == nullptr)
        best = "VERSE";

    TransitionSection ts;
    ts.name = best;
    ts.pool = orderedSectionPatternPoolForGenre(best, genreId);
    return ts;
}

inline TransitionSection pickNextSectionAfterLock(int lockedPatternIndex, int genreId,
                                                  const char* avoidSection) noexcept
{
    const char* avoids[1] { avoidSection };
    const int n = (avoidSection != nullptr && avoidSection[0] != '\0') ? 1 : 0;
    return pickNextSectionAfterLock(lockedPatternIndex, genreId, avoids, n);
}

/**
 * @brief Keep @p patternIndex if it is already in @p pool; otherwise snap to the
 *        first state-compatible member (pool is prior-ordered, so [0] is home).
 */
inline bool poolContains(const SectionPatternPool& pool, int patternIndex) noexcept
{
    for (int i = 0; i < pool.count; ++i)
        if (pool.indices[i] == patternIndex)
            return true;
    return false;
}

/** @brief First library index compatible with @p state (0 if none). */
inline int firstCompatiblePattern(StructureState state) noexcept
{
    for (int i = 0; i < kPatternCount; ++i)
        if (isPatternCompatibleWithState(i, state))
            return i;
    return 0;
}

inline int constrainToPool(int patternIndex, const SectionPatternPool& pool,
                           StructureState state = StructureState::SOFT) noexcept
{
    if (pool.count <= 0)
        return patternIndex;
    for (int i = 0; i < pool.count; ++i)
    {
        if (pool.indices[i] == patternIndex)
            return patternIndex;
    }
    for (int i = 0; i < pool.count; ++i)
    {
        if (isPatternCompatibleWithState(pool.indices[i], state))
            return pool.indices[i];
    }
    // T4.1: never collapse a loud verse onto pool[0] when no member matches
    // the live state — fall back to a globally compatible groove instead.
    return firstCompatiblePattern(state);
}

/** @brief 8th-kick verse-neighborhood (the 85 BPM take's A/B were too close). */
inline bool isVerseFeelNeighborhood(int patternIndex) noexcept
{
    return patternIndex == 1 || patternIndex == 2 || patternIndex == 3
        || patternIndex == 20 || patternIndex == 22 || patternIndex == 23;
}

/** @brief Breakdown / half-time / chorus-open — audibly unlike verse 8th-kicks. */
inline bool isStrongContrastPattern(int patternIndex) noexcept
{
    return patternIndex == 6 || patternIndex == 7 || patternIndex == 9
        || patternIndex == 14 || patternIndex == 15;
}

/**
 * @brief Home groove for a Record-riff B contrast: pool[0] unless that still
 *        sits in the same feel-neighborhood as @p drumA.
 */
inline int contrastHomePattern(int drumA, const SectionPatternPool& pool) noexcept
{
    if (pool.count <= 0)
        return drumA;
    if (!isVerseFeelNeighborhood(drumA))
        return pool.indices[0];
    for (int i = 0; i < pool.count; ++i)
    {
        if (isStrongContrastPattern(pool.indices[i]))
            return pool.indices[i];
    }
    return pool.indices[0];
}

} // namespace PatternRules
