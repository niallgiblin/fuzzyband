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
 * The numbers below are musically-baked defaults. Workstream C1 (E-GMD stats)
 * can replace them with data-derived distributions without touching the
 * rendering code — the rendering layer only reads this struct.
 */

#include <cmath>
#include <cstring>

namespace Groove
{

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

/** @brief Rock default: downbeat/backbeat accents, laid-back backbeat, punchy kick. */
inline Template rock() noexcept
{
    Template t;
    for (int i = 0; i < 16; ++i)
    {
        t.velocityMul[i] = 1.0f;
        t.timingMs[i] = 0.0f;
    }

    // ── Velocity hierarchy ─────────────────────────────────────────────────
    t.velocityMul[0]  = 1.10f;   // beat 1 (downbeat)
    t.velocityMul[4]  = 1.12f;   // backbeat 2
    t.velocityMul[8]  = 1.04f;   // beat 3
    t.velocityMul[12] = 1.10f;   // backbeat 4
    for (int c : { 2, 6, 10, 14 }) t.velocityMul[c] = 0.95f;  // 8th-note hats
    for (int c : { 1, 3, 5, 7, 9, 11, 13, 15 }) t.velocityMul[c] = 0.90f;  // off-16ths

    // ── Structured microtiming ─────────────────────────────────────────────
    t.timingMs[0]  = -1.0f;   // kick slightly early (punch)
    t.timingMs[4]  = 4.0f;    // backbeat laid back
    t.timingMs[8]  = 1.0f;    // beat 3 slightly late
    t.timingMs[12] = 4.0f;    // backbeat laid back
    // hats stay on grid (0.0)

    t.timingJitterMs = 1.5f;
    t.velocityJitter = 3.0f;
    return t;
}

/** @brief Heavier template: tighter timing, stronger accents (metal preset). */
inline Template metal() noexcept
{
    Template t = rock();
    t.velocityMul[0] = 1.12f;
    t.velocityMul[4] = 1.12f;
    t.velocityMul[12] = 1.12f;
    t.timingMs[4] = 2.5f;
    t.timingMs[12] = 2.5f;
    t.timingJitterMs = 1.0f;
    return t;
}

/** @brief Straight, driving template (punk preset): everything near the grid. */
inline Template punk() noexcept
{
    Template t = metal();
    t.timingMs[0] = 0.0f;
    t.timingMs[4] = 1.0f;
    t.timingMs[12] = 1.0f;
    t.timingJitterMs = 0.8f;
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
 * @brief Genre preset dimension (B1): what a genre selects for rendering.
 * Pattern pool choice lives in PatternRules::sectionPatternPoolForGenre /
 * diversifyPatternForGenre; this struct covers velocity profile, swing default,
 * half-time bias and BPM range.
 */
struct GenrePreset
{
    const char* name = "Rock";
    int templateId = 0;           // 0 rock, 1 metal, 2 punk
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

/** @brief Built-in presets: rock-first default, metal retained as a preset. */
inline const GenrePreset* presets() noexcept
{
    static const GenrePreset kPresets[] = {
        //              name         tmpl swing half  velScale ghost min max verse  chorus brkdn  intro  outro  solo   def
        { "Rock",       0, 0.00f, 0.15f, 1.00f, 0.35f, 40, 300, 0.92f, 1.06f, 0.98f, 0.88f, 0.80f, 1.06f, 1.00f },
        { "Hard Rock",  1, 0.10f, 0.25f, 1.03f, 0.30f, 40, 300, 0.94f, 1.08f, 0.96f, 0.90f, 0.82f, 1.08f, 1.00f },
        { "Punk",       2, 0.00f, 0.10f, 1.05f, 0.15f, 80, 300, 0.98f, 1.06f, 1.00f, 0.92f, 0.88f, 1.04f, 1.00f },
        { "Metal",      1, 0.00f, 0.30f, 1.00f, 0.10f, 40, 300, 0.95f, 1.05f, 1.00f, 0.90f, 0.85f, 1.05f, 1.00f },
        { "Sludge",     1, 0.00f, 0.70f, 0.95f, 0.05f, 40, 220, 0.98f, 1.02f, 1.02f, 0.92f, 0.86f, 1.02f, 1.00f },
    };
    return kPresets;
}

inline int presetCount() noexcept
{
    return 5;
}

inline const GenrePreset& presetFor(int id) noexcept
{
    const int i = (id < 0) ? 0 : (id >= presetCount() ? presetCount() - 1 : id);
    return presets()[i];
}

} // namespace Groove
