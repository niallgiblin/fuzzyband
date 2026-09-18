#pragma once

/**
 * @file
 * @brief Groove rendering policy: velocity hierarchy, structured microtiming,
 *        and genre presets (Workstream A2/A3/B1 of the Musicality & Rock Pivot).
 *
 * A2.1/A2.2 replace the old white-noise humanisation (uniform ±10 velocity,
 * uniform ±2 ms timing) with a drummer-like model:
 *   - a per-16th-grid-cell velocity hierarchy (downbeat > backbeat > 8th hats
 *     > off-16th), and
 *   - structured per-cell microtiming (backbeat slightly late, kick slightly
 *     early, ghost notes early) plus a *bounded* gaussian around the structured
 *     offset instead of pure white noise.
 *
 * Workstream C1 (DATA_STRATEGY.md §6.1) has since replaced the hand-authored
 * velocity/microtiming numbers with **data-derived** distributions from the
 * Groove MIDI Dataset (GMD, CC-BY 4.0). The per-16th values live in the
 * auto-generated GrooveTemplateData.h (regenerate with
 * `python3 training/build_groove_template.py`); the rendering layer is unchanged
 * — it still only reads this fixed-size struct on the audio thread.
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include "midi/GrooveTemplateData.h"

namespace Groove
{

/** T3.3: baked templates must sit on a centred grid (|mean| < 1.5 ms). */
constexpr float timingMeanMs(const float (&a)[16]) noexcept
{
    float s = 0.0f;
    for (int i = 0; i < 16; ++i)
        s += a[i];
    return s * (1.0f / 16.0f);
}

static_assert(timingMeanMs(data::kRockTimingMs)  > -1.5f && timingMeanMs(data::kRockTimingMs)  < 1.5f);
static_assert(timingMeanMs(data::kMetalTimingMs) > -1.5f && timingMeanMs(data::kMetalTimingMs) < 1.5f);
static_assert(timingMeanMs(data::kPunkTimingMs)  > -1.5f && timingMeanMs(data::kPunkTimingMs)  < 1.5f);

/** Musical sections used for per-section rendering policy (A3.1 dynamic contrast). */
enum class SongSectionId
{
    Intro,
    Verse,
    Chorus,
    Breakdown,
    Solo,
    Outro,
    Unknown
};

inline SongSectionId sectionIdFromName(const char* name) noexcept
{
    if (name == nullptr)
        return SongSectionId::Unknown;
    if (std::strcmp(name, "INTRO") == 0)     return SongSectionId::Intro;
    if (std::strcmp(name, "VERSE") == 0)     return SongSectionId::Verse;
    if (std::strcmp(name, "CHORUS") == 0)    return SongSectionId::Chorus;
    if (std::strcmp(name, "BREAKDOWN") == 0) return SongSectionId::Breakdown;
    if (std::strcmp(name, "SOLO") == 0)      return SongSectionId::Solo;
    if (std::strcmp(name, "OUTRO") == 0)     return SongSectionId::Outro;
    return SongSectionId::Unknown;
}

/**
 * @brief 16th-note grid cell within a 4/4 bar.
 * Cell = round((beat mod 4) * 4), wrapped to [0, 15]. Bar 2's downbeat
 * (beat 4.0) maps to cell 0, bar 2's backbeat (beat 5.0) to cell 4, etc.
 */
inline int grid16Of(float beat) noexcept
{
    const float b = std::fmod(beat, 4.0f);
    const int cell = static_cast<int>(std::round(b * 4.0f));
    return ((cell % 16) + 16) % 16;
}

/**
 * @brief Groove template: one velocity multiplier and one microtiming offset
 * per 16th-grid cell, plus jitter parameters.
 *
 * velocityMul[cell]: accent multiplier applied on top of the authored velocity.
 * timingMs[cell]:   structured offset in milliseconds; positive = late
 *                   (laid-back backbeat), negative = early (punchy kick).
 */
struct Template
{
    float velocityMul[16] = {};
    float timingMs[16] = {};
    float timingJitterMs = 1.5f;  // bounded-gaussian sigma around the structured offset
    float velocityJitter = 3.0f;  // bounded-gaussian sigma for velocity
    float ghostVelocityLo = 30.0f;
    float ghostVelocityHi = 55.0f;
    float ghostTimingMs = -2.0f;  // ghost notes sit slightly early
    float bassPocketMs = 2.0f;    // bass sits ~2 ms behind the kick
    uint8_t ghostThreshold = 62;  // authored velocities <= this are treated as ghosts
};

/** @brief Fill a Template's per-16th arrays + jitter/ghost knobs from C1 data.
 *  Non-derived feel fields (ghostTimingMs, bassPocketMs) keep the struct
 *  defaults — GMD MIDI does not supply a guitar/bass pocket offset. */
inline void applyData(Template& t,
                      const float (&velocityMul)[16],
                      const float (&timingMs)[16],
                      float timingJitterMs,
                      float velocityJitter,
                      float ghostVelocityLo,
                      float ghostVelocityHi,
                      unsigned char ghostThreshold) noexcept
{
    for (int i = 0; i < 16; ++i)
    {
        t.velocityMul[i] = velocityMul[i];
        t.timingMs[i]    = timingMs[i];
    }
    t.timingJitterMs  = timingJitterMs;
    t.velocityJitter  = velocityJitter;
    t.ghostVelocityLo = ghostVelocityLo;
    t.ghostVelocityHi = ghostVelocityHi;
    t.ghostThreshold  = ghostThreshold;
}

/** @brief Rock default: data-derived from GMD rock grooves (C1). */
inline Template rock() noexcept
{
    Template t;
    applyData(t, data::kRockVelocityMul, data::kRockTimingMs,
              data::kRockTimingJitterMs, data::kRockVelocityJitter,
              data::kRockGhostVelocityLo, data::kRockGhostVelocityHi,
              data::kRockGhostThreshold);
    return t;
}

/** @brief Metal template: rock stats tightened toward the grid (C1 derivation). */
inline Template metal() noexcept
{
    Template t;
    applyData(t, data::kMetalVelocityMul, data::kMetalTimingMs,
              data::kMetalTimingJitterMs, data::kMetalVelocityJitter,
              data::kMetalGhostVelocityLo, data::kMetalGhostVelocityHi,
              data::kMetalGhostThreshold);
    return t;
}

/** @brief Punk template: rock stats pulled near-grid with tight jitter (C1 derivation). */
inline Template punk() noexcept
{
    Template t;
    applyData(t, data::kPunkVelocityMul, data::kPunkTimingMs,
              data::kPunkTimingJitterMs, data::kPunkVelocityJitter,
              data::kPunkGhostVelocityLo, data::kPunkGhostVelocityHi,
              data::kPunkGhostThreshold);
    return t;
}

inline const Template& templateFor(int templateId) noexcept
{
    static const Template kRock  = rock();
    static const Template kMetal = metal();
    static const Template kPunk  = punk();
    switch (templateId)
    {
        case 2:  return kPunk;
        case 1:  return kMetal;
        default: return kRock;
    }
}

/**
 * @brief Per-pattern feel (Phase 37 C1): a blend applied on top of the genre
 * template so all 28 patterns do not share one velocity/timing curve.
 *
 * Baked in `GrooveTemplateData.h` from the committed pattern MIDI
 * (`data/pattern_midi`) — machine-independent. Values are tighten-only for
 * timing/jitter (<= 1.0) so the engine's microtiming slack bound (computed from
 * the base template) always remains sufficient.
 */
struct PatternFeel
{
    float accentDepth = 1.0f;   // scales (velocityMul - 1): <1 flatter, >1 deeper
    float timingScale = 1.0f;   // scales timingMs (<= 1 = tighter to the grid)
    float jitterScale = 1.0f;   // scales timingJitterMs (<= 1 = less wobble)
};

/** @brief Direct-index feel lookup (no scan). Out-of-range returns the identity. */
inline PatternFeel feelFor(int patternIndex) noexcept
{
    if (patternIndex < 0 || patternIndex >= data::kPatternFeelCount)
        return PatternFeel{};
    return PatternFeel{ data::kPatternAccentDepth[patternIndex],
                        data::kPatternTimingScale[patternIndex],
                        data::kPatternJitterScale[patternIndex] };
}

/**
 * @brief The genre template with the per-pattern feel blended in.
 *
 * Returns a copy so the static cached templates are never mutated. velocityMul
 * is clamped to [0.6, 1.4] and timingJitterMs to [1.0, 3.0] so a hot feel cannot
 * clip the loudest accents or turn the humaniser into white noise.
 */
inline Template templateForPattern(int patternIndex, int templateId) noexcept
{
    Template t = templateFor(templateId);
    const PatternFeel f = feelFor(patternIndex);
    for (int i = 0; i < 16; ++i)
    {
        t.velocityMul[i] = std::clamp(1.0f + (t.velocityMul[i] - 1.0f) * f.accentDepth,
                                      0.6f, 1.4f);
        t.timingMs[i] = t.timingMs[i] * f.timingScale;
    }
    t.timingJitterMs = std::clamp(t.timingJitterMs * f.jitterScale, 1.0f, 3.0f);
    return t;
}

/**
 * @brief Routing family for a genre: which pattern vocabulary it draws from.
 * Rock-leaning genres (Rock, Punk, Classic Rock, …) route into the rock-first
 * pattern set; Metal-family genres (Metal, Sludge, Thrash, Death, …) keep the
 * original metal routing. Replaces the old index-threshold (`genreId >= 3`)
 * so the genre list can grow without silently re-categorising new entries.
 */
enum class GenreFamily
{
    Rock,
    Metal
};

/**
 * @brief Genre preset dimension (B1): what a genre selects for rendering.
 * Pattern pool choice lives in PatternRules::sectionPatternPoolForGenre /
 * diversifyPatternForGenre; this struct covers velocity profile, swing default,
 * half-time bias and BPM range.
 */
struct GenrePreset
{
    const char* name = "Rock";
    int templateId = 0;           // 0 rock, 1 metal, 2 punk
    GenreFamily family = GenreFamily::Rock;  // routing family (rock-leaning vs metal)
    int grooveSlot = 0;           // groove-renderer feel slot 0..4 this genre uses
    float defaultSwing = 0.0f;    // swing/shuffle ratio 0..1 (A2.3)
    float halfTimeBias = 0.15f;   // 0..1 — reserved for A4.2 generated grooves
    float velocityScale = 1.0f;   // overall drum velocity gain
    float ghostDensity = 0.35f;   // 0..1 — off-16th ghost note density (A3.2)
    int minBpm = 40;
    int maxBpm = 300;

    // Per-section velocity multipliers (A3.1 dynamic contrast). The plan's
    // metric: chorus backbeat - verse backbeat >= 15.
    float verseVel = 0.92f;
    float chorusVel = 1.06f;
    float breakdownVel = 0.98f;
    float introVel = 0.88f;
    float outroVel = 0.80f;
    float soloVel = 1.06f;
    float defaultVel = 1.0f;

    float sectionVelocityMultiplier(SongSectionId s) const noexcept
    {
        switch (s)
        {
            case SongSectionId::Verse:     return verseVel;
            case SongSectionId::Chorus:    return chorusVel;
            case SongSectionId::Breakdown: return breakdownVel;
            case SongSectionId::Intro:     return introVel;
            case SongSectionId::Outro:     return outroVel;
            case SongSectionId::Solo:      return soloVel;
            case SongSectionId::Unknown:   return defaultVel;
        }
        return defaultVel;
    }
};

/**
 * @brief Built-in presets: rock-first default, metal retained, plus broader rock
 * and metal subgenres. New genres are appended AFTER the original five so the
 * original indices (and their PatternPriors rows / persisted session values)
 * stay stable. `family` routes each genre into the rock or metal vocabulary;
 * `grooveSlot` maps it onto one of the five groove-renderer feel slots.
 */
inline const GenrePreset* presets() noexcept
{
    static const GenrePreset kPresets[] = {
        // name            tmpl family              slot swing  half  vel   ghost min  max  verse chorus brkdn intro outro solo  def
        { "Rock",          0, GenreFamily::Rock,    0, 0.00f, 0.15f, 1.00f, 0.35f, 40, 300, 0.92f, 1.06f, 0.98f, 0.88f, 0.80f, 1.06f, 1.00f },
        { "Hard Rock",     1, GenreFamily::Rock,    1, 0.10f, 0.25f, 1.03f, 0.30f, 40, 300, 0.94f, 1.08f, 0.96f, 0.90f, 0.82f, 1.08f, 1.00f },
        { "Punk",          2, GenreFamily::Rock,    2, 0.00f, 0.10f, 1.05f, 0.15f, 80, 300, 0.98f, 1.06f, 1.00f, 0.92f, 0.88f, 1.04f, 1.00f },
        { "Metal",         1, GenreFamily::Metal,   3, 0.00f, 0.30f, 1.00f, 0.10f, 40, 300, 0.95f, 1.05f, 1.00f, 0.90f, 0.85f, 1.05f, 1.00f },
        { "Sludge",        1, GenreFamily::Metal,   4, 0.00f, 0.70f, 0.95f, 0.05f, 40, 220, 0.98f, 1.02f, 1.02f, 0.92f, 0.86f, 1.02f, 1.00f },
        // ── Metal subgenres ──────────────────────────────────────────────────
        { "Thrash Metal",  1, GenreFamily::Metal,   3, 0.00f, 0.15f, 1.05f, 0.20f, 90, 300, 0.98f, 1.12f, 1.00f, 0.92f, 0.88f, 1.10f, 1.00f },
        { "Death Metal",   1, GenreFamily::Metal,   3, 0.00f, 0.10f, 1.08f, 0.25f, 80, 300, 0.98f, 1.12f, 1.02f, 0.90f, 0.85f, 1.12f, 1.00f },
        { "Black Metal",   1, GenreFamily::Metal,   3, 0.00f, 0.05f, 1.05f, 0.30f, 100, 300, 1.00f, 1.08f, 1.00f, 0.92f, 0.88f, 1.08f, 1.00f },
        { "Doom Metal",    1, GenreFamily::Metal,   4, 0.00f, 0.60f, 0.92f, 0.08f, 40, 160, 0.96f, 1.02f, 1.02f, 0.90f, 0.84f, 1.02f, 1.00f },
        { "Djent",         1, GenreFamily::Metal,   3, 0.05f, 0.25f, 1.00f, 0.15f, 60, 240, 0.97f, 1.08f, 0.99f, 0.91f, 0.87f, 1.08f, 1.00f },
        // ── Broader rock ─────────────────────────────────────────────────────
        { "Classic Rock",  0, GenreFamily::Rock,    0, 0.10f, 0.10f, 1.00f, 0.30f, 60, 200, 0.90f, 1.05f, 0.96f, 0.86f, 0.78f, 1.05f, 1.00f },
        { "Alternative",   0, GenreFamily::Rock,    0, 0.05f, 0.15f, 1.00f, 0.35f, 60, 220, 0.92f, 1.06f, 0.97f, 0.88f, 0.80f, 1.06f, 1.00f },
        { "Grunge",        0, GenreFamily::Rock,    0, 0.08f, 0.30f, 0.97f, 0.25f, 50, 200, 0.94f, 1.06f, 1.00f, 0.90f, 0.82f, 1.06f, 1.00f },
    };
    return kPresets;
}

inline constexpr int kPresetCount = 13;

inline int presetCount() noexcept
{
    return kPresetCount;
}

inline const GenrePreset& presetFor(int id) noexcept
{
    const int i = (id < 0) ? 0 : (id >= presetCount() ? presetCount() - 1 : id);
    return presets()[i];
}

/** @brief True when @p genreId routes into the metal (not rock-leaning) vocabulary. */
inline bool isMetalFamily(int genreId) noexcept
{
    return presetFor(genreId).family == GenreFamily::Metal;
}

} // namespace Groove
