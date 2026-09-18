#pragma once

/**
 * @file
 * @brief Deterministic fill grammar: assemble a drum fill from motivic cells.
 *
 * Replaces the "pick one of three authored fills" mechanism (patterns 17/18/19)
 * with a pure function that builds a bounded `FillScore` from the musical context
 * (energy, pick density, section, playing style, fill length).
 *
 * Contract (Phase 37 A1):
 *   - PURE: the same FillContext always yields the same FillScore. No clock, no
 *     RNG, no global state, no block-size input.
 *   - BOUNDED: never writes past `kMaxFillEvents`; `FillScore` is trivially
 *     copyable (fixed array, no allocation).
 *   - AUDIO-THREAD SAFE: called inside `PatternPlayer::emitBarFill`. The pattern
 *     emits the returned events by ABSOLUTE sample, so the render stays
 *     buffer-size invariant (T9.2).
 *
 * The caller keeps the authored patterns as the `humanizeAmount == 0` fallback.
 */

#include <algorithm>
#include <cstdint>

#include "midi/GrooveTemplate.h"   // Groove::SongSectionId

namespace FillGrammar
{

/** Hard event cap. One bar of 16ths is 16 events; four 3-note cells + a landing
 *  is 13, so 24 is a comfortable bound that still cannot overflow. */
static constexpr int kMaxFillEvents = 24;

// ── General MIDI drum notes (mirror MidiPatternLibrary.cpp / PatternPlayer.cpp) ──
static constexpr uint8_t kKick      = 36;
static constexpr uint8_t kSnare     = 38;
static constexpr uint8_t kTomLo     = 41;
static constexpr uint8_t kHatClosed = 42;
static constexpr uint8_t kHatOpen   = 46;
static constexpr uint8_t kTomMid    = 45;
static constexpr uint8_t kTomHi     = 48;
static constexpr uint8_t kCrash     = 49;

struct FillEvent
{
    uint8_t note = 0;
    uint8_t velocity = 100;
    float   beatOffset = 0.0f;    // relative to the fill bar start, in [0, 4)
    float   durationBeats = 0.25f;
    bool    isGhost = false;
};

struct FillScore
{
    int count = 0;
    FillEvent events[kMaxFillEvents] {};
};

struct FillContext
{
    unsigned seed = 0;                 // per-bar/instance hash seed
    float rmsEnergy = 0.0f;            // [0,1]
    float onsetDensityPerBeat = 0.0f;  // guitar attacks per beat
    int   sectionId = 0;               // Groove::SongSectionId as int
    int   styleIndex = -1;             // 0..3 committed style; -1 = unknown
    float fillLengthBeats = 4.0f;      // 1 (pat 17), 2 (pat 18), 4 (pat 19)
    int   precedingPatternIdx = 0;     // groove the fill leads into
    int   barNumber = 0;
};

/** @brief SplitMix32-style avalanche — deterministic per (seed, slot). */
inline unsigned mix(unsigned a, unsigned b) noexcept
{
    unsigned h = a * 0x9E3779B1u + b;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

enum class CellKind
{
    SnareDouble,
    TomCascade,
    KickDouble,
    Flam,
    HatChoke,
    TripletKick,
    CrashLanding
};

/** @brief Beats a cell occupies on the 16th grid. */
inline float cellSpan(CellKind k) noexcept
{
    switch (k)
    {
        case CellKind::SnareDouble:  return 0.50f;
        case CellKind::TomCascade:   return 0.75f;
        case CellKind::KickDouble:   return 0.50f;
        case CellKind::Flam:         return 0.25f;
        case CellKind::HatChoke:     return 0.50f;
        case CellKind::TripletKick:  return 1.00f;
        case CellKind::CrashLanding: return 0.25f;
    }
    return 0.25f;
}

/** @brief Append one event, silently dropping it if the cap is reached. */
inline void push(FillScore& s, uint8_t note, uint8_t vel,
                 float beat, float dur, bool ghost = false) noexcept
{
    if (s.count >= kMaxFillEvents)
        return;
    s.events[static_cast<size_t>(s.count++)] = FillEvent{ note, vel, beat, dur, ghost };
}

/** @brief Emit a motivic cell at @p t0. Returns the cell's span in beats. */
inline float emitCell(FillScore& s, CellKind k, float t0) noexcept
{
    switch (k)
    {
        case CellKind::SnareDouble:
            push(s, kSnare, 98, t0, 0.25f);
            push(s, kSnare, 104, t0 + 0.25f, 0.25f);
            break;
        case CellKind::TomCascade:
            push(s, kTomHi, 104, t0, 0.25f);
            push(s, kTomMid, 106, t0 + 0.25f, 0.25f);
            push(s, kTomLo, 110, t0 + 0.50f, 0.25f);
            break;
        case CellKind::KickDouble:
            push(s, kKick, 108, t0, 0.25f);
            push(s, kKick, 100, t0 + 0.25f, 0.25f);
            break;
        case CellKind::Flam:
            push(s, kSnare, 44, t0, 0.125f, /*ghost=*/true);   // grace
            push(s, kSnare, 104, t0 + 0.125f, 0.25f);
            break;
        case CellKind::HatChoke:
            push(s, kHatClosed, 74, t0, 0.25f);
            push(s, kHatOpen, 80, t0 + 0.25f, 0.50f);
            break;
        case CellKind::TripletKick:
            push(s, kKick, 104, t0, 0.33f);
            push(s, kKick, 104, t0 + 0.333f, 0.33f);
            push(s, kKick, 104, t0 + 0.667f, 0.33f);
            break;
        case CellKind::CrashLanding:
            push(s, kCrash, 118, t0, 2.0f);
            push(s, kKick, 112, t0, 0.25f);
            break;
    }
    return cellSpan(k);
}

/**
 * @brief Build a fill for the given context.
 *
 * Density tier from energy/density, cell vocabulary from the tier, colour from
 * the section, assembled left-to-right across the fill window and landed on the
 * final 16th before the next downbeat.
 */
inline FillScore buildFill(const FillContext& ctx) noexcept
{
    FillScore s;

    const float len = std::clamp(ctx.fillLengthBeats, 1.0f, 4.0f);
    const float windowStart = 4.0f - len;
    const float windowEnd   = 4.0f;

    const bool dense = (ctx.rmsEnergy >= 0.45f) || (ctx.onsetDensityPerBeat >= 1.8f);
    const bool mid   = !dense && (ctx.rmsEnergy >= 0.20f);

    const auto sec = static_cast<Groove::SongSectionId>(ctx.sectionId);
    const bool quietSection  = (sec == Groove::SongSectionId::Breakdown
                             || sec == Groove::SongSectionId::Outro);

    // 39-03: mid AND dense fills are tom-forward so they read as fills, not
    // grooves (the real-take audit found mid-tier fills arriving tom-less on Hard
    // Rock). Sparse fills stay kick/flam — a quiet fill does not need toms.
    const bool tomForward = (dense || mid) && !quietSection;

    // ── Allowed cell vocabulary by tier (fixed order; selection is hash-driven).
    // A quiet section (breakdown/outro) collapses to the sparse set and never
    // gets a crash, so we never fill the space the section is trying to leave.
    CellKind list[5] = {};
    int listCount = 0;
    if (dense && !quietSection)
    {
        list[0] = CellKind::SnareDouble;  list[1] = CellKind::TomCascade;
        list[2] = CellKind::TripletKick;  list[3] = CellKind::CrashLanding;
        list[4] = CellKind::HatChoke;     listCount = 5;
    }
    else if (mid && !quietSection)
    {
        list[0] = CellKind::SnareDouble;  list[1] = CellKind::TomCascade;
        list[2] = CellKind::KickDouble;   list[3] = CellKind::Flam;
        listCount = 4;
    }
    else
    {
        list[0] = CellKind::KickDouble;   list[1] = CellKind::Flam;
        listCount = 2;
    }

    int maxCells = dense ? 4 : (mid ? 3 : 2);
    if (quietSection)
        maxCells = std::min(maxCells, 2);

    // ── Assemble left-to-right, stopping before the window is overrun.
    float t = windowStart;
    for (int i = 0; i < maxCells; ++i)
    {
        if (s.count + 3 > kMaxFillEvents)
            break;

        const unsigned h = mix(ctx.seed, static_cast<unsigned>(i) + 1u);
        CellKind kind = list[h % static_cast<unsigned>(listCount)];
        if (i == 0 && tomForward)
            kind = CellKind::TomCascade;   // guarantee a tom run in dense fills

        if (t + cellSpan(kind) > windowEnd - 1.0e-6f)
            break;   // no room for another cell

        emitCell(s, kind, t);
        t += cellSpan(kind);
    }

    // ── Terminal landing: always lead the phrase into the next downbeat.
    // A crash is the strong landing (dense/bright); otherwise a kick.
    bool lastIsLanding = false;
    if (s.count > 0)
        lastIsLanding = s.events[static_cast<size_t>(s.count - 1)].beatOffset >= 3.5f;

    if (!lastIsLanding && s.count < kMaxFillEvents)
    {
        // Dense fills always land a crash; sparse/mid land a kick. Quiet sections
        // (breakdown/outro) never crash — they are leaving space.
        const bool wantCrash = dense && !quietSection;
        if (wantCrash)
            push(s, kCrash, 118, 3.75f, 2.0f);
        else
            push(s, kKick, 108, 3.75f, 0.25f);
    }

    return s;
}

} // namespace FillGrammar
