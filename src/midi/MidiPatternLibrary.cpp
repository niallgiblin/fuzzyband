#include "MidiPatternLibrary.h"
#include <algorithm>

namespace
{
// ── MIDI note constants ──────────────────────────────────────────────────────
constexpr uint8_t kKick      = 36;
constexpr uint8_t kSnare     = 38;
constexpr uint8_t kHatClosed = 42;
constexpr uint8_t kHatOpen   = 46;
constexpr uint8_t kRide      = 51;
constexpr uint8_t kRideBell  = 53;
constexpr uint8_t kCrash     = 49;
constexpr uint8_t kChina     = 52;
constexpr uint8_t kSplash    = 55;
constexpr uint8_t kTomHi     = 48;
constexpr uint8_t kTomMid    = 45;
constexpr uint8_t kTomLo     = 41;
constexpr uint8_t kBassRoot  = 36; // C2 (drop C tuning)

// ── Event helpers ────────────────────────────────────────────────────────────

MidiEvent drum(uint8_t n, uint8_t vel, float beat, float dur = 0.25f)
{
    return MidiEvent{ n, vel, beat, dur };
}

MidiEvent bass(uint8_t n, uint8_t vel, float beat, float dur = 4.0f)
{
    return MidiEvent{ n, vel, beat, dur };
}

// ══════════════════════════════════════════════════════════════════════════════
// Pattern builders
// ══════════════════════════════════════════════════════════════════════════════

// ── 0: Silent ────────────────────────────────────────────────────────────────

MidiPattern buildSilent()
{
    MidiPattern p;
    p.name = "Silent";
    p.lengthInBars = 1.0f;
    return p;
}

// ── 1: Verse Groove (kick/snare locked, ride bell accent) ────────────────────

MidiPattern buildVerseGroove()
{
    MidiPattern p;
    p.name = "Verse Groove";
    p.lengthInBars = 2.0f; // 2-bar pattern for variation
    p.drumEvents = {
        // Bar 1
        drum(kKick,      115, 0.0f),
        drum(kHatClosed,  78, 0.5f),
        drum(kSnare,     110, 1.0f),
        drum(kHatClosed,  76, 1.5f),
        drum(kKick,      108, 2.0f),
        drum(kRideBell,   90, 2.0f),
        drum(kHatClosed,  76, 2.5f),
        drum(kSnare,     108, 3.0f),
        drum(kHatClosed,  74, 3.5f),
        // Bar 2 — ride bell variation
        drum(kKick,      115, 4.0f),
        drum(kRideBell,   88, 4.0f),
        drum(kHatClosed,  76, 4.5f),
        drum(kSnare,     112, 5.0f),
        drum(kHatClosed,  74, 5.5f),
        drum(kKick,      110, 6.0f),
        drum(kHatClosed,  76, 6.5f),
        drum(kSnare,     110, 7.0f),
        drum(kHatClosed,  74, 7.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 95, 0.0f, 1.9f),
        bass(kBassRoot, 92, 2.0f, 1.9f),
        bass(kBassRoot, 95, 4.0f, 1.9f),
        bass(kBassRoot, 90, 6.0f, 1.9f),
    };
    return p;
}

// ── 2: Verse Half-Time (snare on beat 3, sparse kicks) ───────────────────────

MidiPattern buildVerseHalfTime()
{
    MidiPattern p;
    p.name = "Verse Half-Time";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick,      112, 0.0f),
        drum(kHatClosed,  74, 0.5f),
        drum(kHatClosed,  72, 1.5f),
        drum(kSnare,     114, 2.0f),
        drum(kHatClosed,  72, 2.5f),
        drum(kRide,       82, 3.0f),
        drum(kHatClosed,  70, 3.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 93, 0.0f, 3.0f),
    };
    return p;
}

// ── 3: Verse Fast (double kick 16ths, snare 2/4, closed hat timekeeping) ─────

MidiPattern buildVerseFast()
{
    MidiPattern p;
    p.name = "Verse Fast";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick,      118, 0.0f),
        drum(kHatClosed,  82, 0.25f),
        drum(kKick,      112, 0.5f),
        drum(kHatClosed,  80, 0.75f),
        drum(kSnare,     116, 1.0f),
        drum(kHatClosed,  80, 1.25f),
        drum(kKick,      114, 1.5f),
        drum(kHatClosed,  78, 1.75f),
        drum(kKick,      118, 2.0f),
        drum(kHatClosed,  82, 2.25f),
        drum(kKick,      112, 2.5f),
        drum(kHatClosed,  80, 2.75f),
        drum(kSnare,     116, 3.0f),
        drum(kHatClosed,  78, 3.25f),
        drum(kKick,      110, 3.5f),
        drum(kHatClosed,  76, 3.75f),
    };
    p.bassEvents = {
        bass(kBassRoot, 98, 0.0f, 0.9f),
        bass(kBassRoot, 96, 1.0f, 0.9f),
        bass(kBassRoot, 96, 2.0f, 0.9f),
        bass(kBassRoot, 94, 3.0f, 0.9f),
    };
    return p;
}

// ── 4: Chorus Mid (open hat groove, crash on 1) ──────────────────────────────

MidiPattern buildChorusMid()
{
    MidiPattern p;
    p.name = "Chorus Mid";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        // Bar 1 — crash accent on beat 1
        drum(kCrash,     110, 0.0f, 2.5f),
        drum(kKick,      120, 0.0f),
        drum(kHatOpen,    88, 0.5f),
        drum(kSnare,     118, 1.0f),
        drum(kHatOpen,    86, 1.5f),
        drum(kKick,      115, 2.0f),
        drum(kHatOpen,    88, 2.5f),
        drum(kSnare,     116, 3.0f),
        drum(kHatOpen,    84, 3.5f),
        // Bar 2 — ride bell on beat 1
        drum(kKick,      122, 4.0f),
        drum(kRideBell,   95, 4.0f),
        drum(kHatOpen,    90, 4.5f),
        drum(kSnare,     120, 5.0f),
        drum(kHatOpen,    88, 5.5f),
        drum(kKick,      118, 6.0f),
        drum(kRideBell,   92, 6.0f),
        drum(kHatOpen,    88, 6.5f),
        drum(kSnare,     118, 7.0f),
        drum(kHatOpen,    86, 7.5f),
    };
    p.bassEvents = {
        bass(kBassRoot,     102, 0.0f, 1.9f),
        bass(kBassRoot + 5,  98, 2.0f, 1.9f),
        bass(kBassRoot,     104, 4.0f, 1.9f),
        bass(kBassRoot + 7, 100, 6.0f, 1.9f),
    };
    return p;
}

// ── 5: Chorus Fast (double kick blast, china on 1, snare 2/4) ────────────────

MidiPattern buildChorusFast()
{
    MidiPattern p;
    p.name = "Chorus Fast";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kChina,     112, 0.0f, 1.5f),
        drum(kKick,      125, 0.0f),
        drum(kKick,      120, 0.25f),
        drum(kHatOpen,    92, 0.5f),
        drum(kKick,      122, 0.75f),
        drum(kSnare,     124, 1.0f),
        drum(kKick,      120, 1.25f),
        drum(kHatOpen,    90, 1.5f),
        drum(kKick,      122, 1.75f),
        drum(kSnare,     122, 2.0f),
        drum(kKick,      120, 2.25f),
        drum(kRide,       92, 2.5f),
        drum(kKick,      122, 2.75f),
        drum(kSnare,     124, 3.0f),
        drum(kKick,      120, 3.25f),
        drum(kHatOpen,    90, 3.5f),
        drum(kKick,      122, 3.75f),
    };
    p.bassEvents = {
        bass(kBassRoot, 104, 0.0f, 0.45f),
        bass(kBassRoot, 102, 0.5f, 0.45f),
        bass(kBassRoot, 104, 1.0f, 0.45f),
        bass(kBassRoot, 102, 1.5f, 0.45f),
        bass(kBassRoot, 104, 2.0f, 0.45f),
        bass(kBassRoot, 102, 2.5f, 0.45f),
        bass(kBassRoot, 104, 3.0f, 0.45f),
        bass(kBassRoot, 102, 3.5f, 0.45f),
    };
    return p;
}

// ── 6: Breakdown (2-bar sparse, multi-snare hits, crushing) ──────────────────

MidiPattern buildBreakdown()
{
    MidiPattern p;
    p.name = "Breakdown";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        drum(kChina,     115, 0.0f, 1.0f),
        drum(kKick,      118, 0.0f),
        drum(kSnare,      72, 0.75f),
        drum(kSnare,      74, 1.25f),
        drum(kKick,      112, 2.0f),
        drum(kSnare,      70, 3.0f),
        drum(kSnare,      76, 3.5f),
        drum(kKick,      116, 4.0f),
        drum(kChina,     110, 4.0f, 1.0f),
        drum(kSnare,      74, 4.75f),
        drum(kSnare,      76, 5.25f),
        drum(kKick,      110, 6.0f),
        drum(kSnare,      72, 7.0f),
        drum(kSnare,      78, 7.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 96, 0.0f, 1.9f),
        bass(kBassRoot, 90, 2.25f, 0.4f),
        bass(kBassRoot, 94, 4.0f, 1.9f),
        bass(kBassRoot, 88, 6.25f, 0.4f),
    };
    return p;
}

// ── 7: Half-Time (FIXED: removed conflicting kick at beat 2.0) ───────────────

MidiPattern buildHalfTime()
{
    MidiPattern p;
    p.name = "Half-Time";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick,      115, 0.0f),
        drum(kHatClosed,  78, 0.5f),
        drum(kSnare,     112, 1.0f),
        drum(kHatClosed,  76, 1.5f),
        drum(kKick,      108, 2.0f),
        drum(kHatClosed,  76, 2.5f),
        drum(kSnare,     114, 3.0f),
        drum(kHatClosed,  74, 3.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 95, 0.0f, 3.0f),
    };
    return p;
}

// ── 8: Blast Beat (alternating kick/snare 16ths, ride quarters) ──────────────

MidiPattern buildBlastBeat()
{
    MidiPattern p;
    p.name = "Blast Beat";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kCrash,     115, 0.0f, 1.5f),
        drum(kKick,      125, 0.0f),
        drum(kSnare,     122, 0.25f),
        drum(kKick,      122, 0.5f),
        drum(kSnare,     120, 0.75f),
        drum(kKick,      125, 1.0f),
        drum(kSnare,     122, 1.25f),
        drum(kKick,      122, 1.5f),
        drum(kSnare,     120, 1.75f),
        drum(kKick,      125, 2.0f),
        drum(kSnare,     122, 2.25f),
        drum(kKick,      122, 2.5f),
        drum(kSnare,     120, 2.75f),
        drum(kKick,      125, 3.0f),
        drum(kSnare,     122, 3.25f),
        drum(kKick,      122, 3.5f),
        drum(kSnare,     120, 3.75f),
        drum(kRide,       90, 0.0f),
        drum(kRide,       90, 1.0f),
        drum(kRide,       90, 2.0f),
        drum(kRide,       90, 3.0f),
    };
    p.bassEvents = {
        bass(kBassRoot, 100, 0.0f, 0.45f),
        bass(kBassRoot, 100, 0.5f, 0.45f),
        bass(kBassRoot, 100, 1.0f, 0.45f),
        bass(kBassRoot, 100, 1.5f, 0.45f),
        bass(kBassRoot, 100, 2.0f, 0.45f),
        bass(kBassRoot, 100, 2.5f, 0.45f),
        bass(kBassRoot, 100, 3.0f, 0.45f),
        bass(kBassRoot, 100, 3.5f, 0.45f),
    };
    return p;
}

// ── 9: Sparse Breakdown (2-bar, only 3 hits — ultra-minimal) ─────────────────

MidiPattern buildSparseBreakdown()
{
    MidiPattern p;
    p.name = "Sparse Breakdown";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        drum(kChina,     118, 0.0f, 2.0f),
        drum(kKick,      115, 0.0f),
        drum(kKick,      108, 6.0f),
        drum(kSnare,     112, 6.0f),
    };
    p.bassEvents = {
        bass(kBassRoot, 95, 0.0f, 3.5f),
    };
    return p;
}

// ── 10: Thrash (double kick 16ths, snare 2/4, closed hat driving) ────────────

MidiPattern buildThrash()
{
    MidiPattern p;
    p.name = "Thrash";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick,      122, 0.0f),
        drum(kKick,      118, 0.25f),
        drum(kHatClosed,  88, 0.5f),
        drum(kSnare,     120, 1.0f),
        drum(kHatClosed,  86, 1.5f),
        drum(kKick,      122, 2.0f),
        drum(kKick,      118, 2.25f),
        drum(kHatClosed,  88, 2.5f),
        drum(kSnare,     120, 3.0f),
        drum(kHatClosed,  86, 3.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 102, 0.0f, 0.45f),
        bass(kBassRoot, 98,  0.5f, 0.45f),
        bass(kBassRoot, 102, 1.0f, 0.45f),
        bass(kBassRoot, 98,  1.5f, 0.45f),
        bass(kBassRoot, 102, 2.0f, 0.45f),
        bass(kBassRoot, 98,  2.5f, 0.45f),
        bass(kBassRoot, 102, 3.0f, 0.45f),
        bass(kBassRoot, 98,  3.5f, 0.45f),
    };
    return p;
}

// ══════════════════════════════════════════════════════════════════════════════
// NEW patterns (indices 11–21)
// ══════════════════════════════════════════════════════════════════════════════

// ── 11: Intro Build (starts sparse, tom fill into full on bar 4) ─────────────

MidiPattern buildIntroBuild()
{
    MidiPattern p;
    p.name = "Intro Build";
    p.lengthInBars = 4.0f;
    p.drumEvents = {
        // Bar 1 — barely there (splash + ride taps)
        drum(kSplash,     90, 0.0f, 0.8f),
        drum(kRide,       70, 0.0f),
        drum(kRide,       68, 2.0f),
        // Bar 2 — kick enters
        drum(kKick,      100, 4.0f),
        drum(kRide,       74, 4.0f),
        drum(kKick,      102, 6.0f),
        drum(kRide,       74, 6.0f),
        // Bar 3 — snare ghost notes join
        drum(kKick,      108, 8.0f),
        drum(kRide,       78, 8.0f),
        drum(kSnare,      80, 9.0f),
        drum(kKick,      110, 10.0f),
        drum(kRide,       78, 10.0f),
        drum(kSnare,      84, 11.0f),
        // Bar 4 — tom fill into crash
        drum(kKick,      115, 12.0f),
        drum(kRide,       84, 12.0f),
        drum(kKick,      112, 12.5f),
        drum(kTomHi,     105, 13.0f),
        drum(kTomMid,    105, 13.25f),
        drum(kTomLo,     108, 13.5f),
        drum(kKick,      118, 13.75f),
        drum(kCrash,     120, 14.0f, 2.0f),
        drum(kSnare,     116, 14.0f),
    };
    p.bassEvents = {};
    return p;
}

// ── 12: Intro Full (full groove with crash, leading into verse) ──────────────

MidiPattern buildIntroFull()
{
    MidiPattern p;
    p.name = "Intro Full";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        drum(kCrash,     118, 0.0f, 2.0f),
        drum(kKick,      118, 0.0f),
        drum(kHatClosed,  84, 0.5f),
        drum(kSnare,     114, 1.0f),
        drum(kHatClosed,  82, 1.5f),
        drum(kKick,      115, 2.0f),
        drum(kHatClosed,  84, 2.5f),
        drum(kSnare,     116, 3.0f),
        drum(kHatClosed,  82, 3.5f),
        // Bar 2 — tom fill lead-in to verse
        drum(kKick,      120, 4.0f),
        drum(kHatClosed,  86, 4.5f),
        drum(kSnare,     118, 5.0f),
        drum(kHatClosed,  84, 5.5f),
        drum(kTomHi,     108, 6.0f),
        drum(kTomMid,    108, 6.25f),
        drum(kTomLo,     112, 6.5f),
        drum(kKick,      122, 6.75f),
        drum(kCrash,     120, 7.0f, 3.0f),
        drum(kSnare,     120, 7.0f),
    };
    p.bassEvents = {
        bass(kBassRoot, 95, 0.0f, 1.9f),
        bass(kBassRoot, 93, 2.0f, 1.9f),
        bass(kBassRoot, 96, 4.0f, 1.9f),
        bass(kBassRoot, 92, 6.0f, 1.9f),
    };
    return p;
}

// ── 13: Pre-Chorus Rise (floor tom build + ride, tension) ────────────────────

MidiPattern buildPreChorusRise()
{
    MidiPattern p;
    p.name = "Pre-Chorus Rise";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        // Bar 1 — ride + floor tom pattern
        drum(kKick,      112, 0.0f),
        drum(kRide,       88, 0.0f),
        drum(kTomLo,     100, 1.0f),
        drum(kKick,      114, 2.0f),
        drum(kRide,       90, 2.0f),
        drum(kTomLo,     104, 2.5f),
        drum(kTomLo,     106, 3.0f),
        // Bar 2 — tom build into crash
        drum(kKick,      116, 4.0f),
        drum(kTomLo,     108, 4.0f),
        drum(kTomMid,    108, 4.5f),
        drum(kTomHi,     110, 5.0f),
        drum(kSnare,     108, 5.5f),
        drum(kKick,      120, 6.0f),
        drum(kTomLo,     112, 6.25f),
        drum(kTomMid,    112, 6.5f),
        drum(kTomHi,     114, 6.75f),
        drum(kCrash,     122, 7.0f, 2.0f),
        drum(kSnare,     122, 7.0f),
    };
    p.bassEvents = {
        bass(kBassRoot + 5, 98, 0.0f, 1.9f),
        bass(kBassRoot + 7, 100, 2.0f, 1.9f),
    };
    return p;
}

// ── 14: Chorus Open Groove (big open hats, crashes, ride bell) ───────────────

MidiPattern buildChorusOpenGroove()
{
    MidiPattern p;
    p.name = "Chorus Open Groove";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        // Bar 1 — big crash + open hat drive
        drum(kCrash,     120, 0.0f, 2.5f),
        drum(kKick,      122, 0.0f),
        drum(kRideBell,   98, 0.0f),
        drum(kHatOpen,    92, 0.5f),
        drum(kSnare,     120, 1.0f),
        drum(kHatOpen,    90, 1.5f),
        drum(kKick,      118, 2.0f),
        drum(kHatOpen,    92, 2.5f),
        drum(kSnare,     118, 3.0f),
        drum(kHatOpen,    88, 3.5f),
        // Bar 2 — china accent on beat 1, open hat variation
        drum(kChina,     112, 4.0f, 1.5f),
        drum(kKick,      124, 4.0f),
        drum(kHatOpen,    94, 4.5f),
        drum(kSnare,     122, 5.0f),
        drum(kHatOpen,    92, 5.5f),
        drum(kKick,      120, 6.0f),
        drum(kRideBell,   96, 6.0f),
        drum(kHatOpen,    92, 6.5f),
        drum(kSnare,     120, 7.0f),
        drum(kHatOpen,    90, 7.5f),
    };
    p.bassEvents = {
        bass(kBassRoot,     104, 0.0f, 1.9f),
        bass(kBassRoot + 7, 100, 2.0f, 1.9f),
        bass(kBassRoot,     106, 4.0f, 1.9f),
        bass(kBassRoot + 5, 102, 6.0f, 1.9f),
    };
    return p;
}

// ── 15: Breakdown Full (heavy china, ghost snare, crushing kicks) ────────────

MidiPattern buildBreakdownFull()
{
    MidiPattern p;
    p.name = "Breakdown Full";
    p.lengthInBars = 2.0f;
    p.drumEvents = {
        drum(kChina,     120, 0.0f, 1.5f),
        drum(kKick,      120, 0.0f),
        drum(kSnare,      76, 0.75f),
        drum(kSnare,      78, 1.25f),
        drum(kKick,      114, 2.0f),
        drum(kSnare,      72, 2.5f),
        drum(kSnare,      74, 3.0f),
        drum(kSnare,      80, 3.5f),
        drum(kKick,      118, 4.0f),
        drum(kChina,     116, 4.0f, 1.0f),
        drum(kSnare,      76, 4.75f),
        drum(kSnare,      78, 5.25f),
        drum(kKick,      112, 6.0f),
        drum(kSnare,      74, 6.5f),
        drum(kSnare,      76, 7.0f),
        drum(kCrash,     115, 7.0f, 2.0f),
        drum(kSnare,      82, 7.5f),
    };
    p.bassEvents = {
        bass(kBassRoot, 98, 0.0f, 1.9f),
        bass(kBassRoot, 92, 2.25f, 0.5f),
        bass(kBassRoot, 96, 4.0f, 1.9f),
        bass(kBassRoot, 90, 6.25f, 0.5f),
    };
    return p;
}

// ── 16: Outro Decay (simplifying pattern, final crash) ───────────────────────

MidiPattern buildOutroDecay()
{
    MidiPattern p;
    p.name = "Outro Decay";
    p.lengthInBars = 4.0f;
    p.drumEvents = {
        // Bar 1 — full groove
        drum(kKick,      115, 0.0f),
        drum(kRide,       82, 0.0f),
        drum(kSnare,     112, 1.0f),
        drum(kRide,       80, 2.0f),
        drum(kKick,      110, 2.0f),
        drum(kSnare,     110, 3.0f),
        // Bar 2 — simplify, no kick
        drum(kSnare,     105, 5.0f),
        drum(kRide,       74, 6.0f),
        drum(kSnare,     102, 7.0f),
        // Bar 3 — ride only, fading
        drum(kRide,       68, 8.0f),
        drum(kRide,       66, 10.0f),
        // Bar 4 — final crash and done
        drum(kCrash,     118, 14.0f, 2.0f),
        drum(kKick,      112, 14.0f),
    };
    p.bassEvents = {
        bass(kBassRoot, 90, 0.0f, 3.5f),
        bass(kBassRoot, 85, 8.0f, 3.5f),
    };
    return p;
}

// ── 17: Fill Short (1-beat tom + kick burst on beat 4) ───────────────────────

MidiPattern buildFillShort()
{
    MidiPattern p;
    p.name = "Fill Short";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        // Standard groove for beats 1-3
        drum(kKick,      115, 0.0f),
        drum(kHatClosed,  78, 0.5f),
        drum(kSnare,     110, 1.0f),
        drum(kHatClosed,  76, 1.5f),
        drum(kKick,      110, 2.0f),
        drum(kHatClosed,  76, 2.5f),
        // Fill on beat 4
        drum(kTomHi,     108, 3.0f),
        drum(kTomMid,    108, 3.25f),
        drum(kKick,      115, 3.5f),
        drum(kTomLo,     112, 3.5f),
        drum(kCrash,     118, 3.75f, 1.5f),
    };
    p.bassEvents = {};
    return p;
}

// ── 18: Fill Medium (1-bar tom roll) ─────────────────────────────────────────

MidiPattern buildFillMedium()
{
    MidiPattern p;
    p.name = "Fill Medium";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        // Standard groove first 2 beats
        drum(kKick,      115, 0.0f),
        drum(kHatClosed,  78, 0.5f),
        drum(kSnare,     110, 1.0f),
        drum(kHatClosed,  76, 1.5f),
        // Fill starts beat 3
        drum(kTomLo,     108, 2.0f),
        drum(kTomMid,    108, 2.25f),
        drum(kTomHi,     110, 2.5f),
        drum(kSnare,     112, 2.75f),
        drum(kTomHi,     110, 3.0f),
        drum(kTomMid,    110, 3.25f),
        drum(kTomLo,     114, 3.5f),
        drum(kKick,      118, 3.75f),
        drum(kCrash,     120, 3.75f, 2.0f),
    };
    p.bassEvents = {};
    return p;
}

// ── 19: Fill Big (full-bar tom roll + crash) ──────────────────────────────────

MidiPattern buildFillBig()
{
    MidiPattern p;
    p.name = "Fill Big";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kCrash,     122, 0.0f, 2.0f),
        drum(kTomLo,     112, 0.0f),
        drum(kTomLo,     110, 0.25f),
        drum(kTomMid,    110, 0.5f),
        drum(kTomHi,     112, 0.75f),
        drum(kSnare,     114, 1.0f),
        drum(kTomHi,     112, 1.25f),
        drum(kTomMid,    112, 1.5f),
        drum(kTomLo,     114, 1.75f),
        drum(kSnare,     116, 2.0f),
        drum(kTomHi,     114, 2.25f),
        drum(kTomMid,    114, 2.5f),
        drum(kTomLo,     116, 2.75f),
        drum(kKick,      120, 3.0f),
        drum(kTomHi,     114, 3.25f),
        drum(kTomMid,    114, 3.5f),
        drum(kTomLo,     118, 3.75f),
        drum(kCrash,     124, 3.75f, 2.5f),
    };
    p.bassEvents = {};
    return p;
}

// ── 20: Verse Ghost (snare ghost notes, groove-oriented) ──────────────────────

MidiPattern buildVerseGhost()
{
    MidiPattern p;
    p.name = "Verse Ghost";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kKick,      112, 0.0f),
        drum(kHatClosed,  74, 0.25f),
        drum(kSnare,      70, 0.5f),  // ghost
        drum(kHatClosed,  72, 0.75f),
        drum(kSnare,     108, 1.0f),
        drum(kHatClosed,  74, 1.25f),
        drum(kSnare,      68, 1.5f),  // ghost
        drum(kHatClosed,  72, 1.75f),
        drum(kKick,      110, 2.0f),
        drum(kHatClosed,  74, 2.25f),
        drum(kSnare,      70, 2.5f),  // ghost
        drum(kHatClosed,  72, 2.75f),
        drum(kSnare,     110, 3.0f),
        drum(kHatClosed,  74, 3.25f),
        drum(kSnare,      72, 3.5f),  // ghost
        drum(kHatClosed,  70, 3.75f),
    };
    p.bassEvents = {
        bass(kBassRoot, 93, 0.0f, 1.9f),
        bass(kBassRoot, 90, 2.0f, 1.9f),
    };
    return p;
}

// ── 21: Chorus Blast (double bass + snare blast, ride bell drive) ────────────

MidiPattern buildChorusBlast()
{
    MidiPattern p;
    p.name = "Chorus Blast";
    p.lengthInBars = 1.0f;
    p.drumEvents = {
        drum(kCrash,     122, 0.0f, 1.5f),
        drum(kKick,      124, 0.0f),
        drum(kKick,      118, 0.25f),
        drum(kSnare,     120, 0.5f),
        drum(kKick,      120, 0.75f),
        drum(kSnare,     122, 1.0f),
        drum(kKick,      118, 1.25f),
        drum(kSnare,     120, 1.5f),
        drum(kKick,      122, 1.75f),
        drum(kSnare,     122, 2.0f),
        drum(kKick,      118, 2.25f),
        drum(kSnare,     120, 2.5f),
        drum(kKick,      122, 2.75f),
        drum(kRideBell,   98, 2.0f),
        drum(kSnare,     124, 3.0f),
        drum(kKick,      120, 3.25f),
        drum(kSnare,     120, 3.5f),
        drum(kKick,      124, 3.75f),
        drum(kRideBell,   98, 3.0f),
        drum(kCrash,     120, 3.75f, 2.0f),
    };
    p.bassEvents = {
        bass(kBassRoot, 106, 0.0f, 0.45f),
        bass(kBassRoot, 104, 0.5f, 0.45f),
        bass(kBassRoot, 106, 1.0f, 0.45f),
        bass(kBassRoot, 104, 1.5f, 0.45f),
        bass(kBassRoot, 106, 2.0f, 0.45f),
        bass(kBassRoot, 104, 2.5f, 0.45f),
        bass(kBassRoot, 106, 3.0f, 0.45f),
        bass(kBassRoot, 104, 3.5f, 0.45f),
    };
    return p;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Library
// ══════════════════════════════════════════════════════════════════════════════

MidiPatternLibrary::MidiPatternLibrary()
{
    patterns.push_back(buildSilent());           // 0
    patterns.push_back(buildVerseGroove());      // 1
    patterns.push_back(buildVerseHalfTime());    // 2
    patterns.push_back(buildVerseFast());        // 3
    patterns.push_back(buildChorusMid());        // 4
    patterns.push_back(buildChorusFast());       // 5
    patterns.push_back(buildBreakdown());        // 6
    patterns.push_back(buildHalfTime());         // 7
    patterns.push_back(buildBlastBeat());        // 8
    patterns.push_back(buildSparseBreakdown());  // 9
    patterns.push_back(buildThrash());           // 10
    patterns.push_back(buildIntroBuild());       // 11
    patterns.push_back(buildIntroFull());        // 12
    patterns.push_back(buildPreChorusRise());    // 13
    patterns.push_back(buildChorusOpenGroove()); // 14
    patterns.push_back(buildBreakdownFull());    // 15
    patterns.push_back(buildOutroDecay());       // 16
    patterns.push_back(buildFillShort());        // 17
    patterns.push_back(buildFillMedium());       // 18
    patterns.push_back(buildFillBig());          // 19
    patterns.push_back(buildVerseGhost());       // 20
    patterns.push_back(buildChorusBlast());      // 21
}

const MidiPattern& MidiPatternLibrary::getPattern(int index) const
{
    const int i = std::clamp(index, 0, patternCount() - 1);
    return patterns[static_cast<size_t>(i)];
}
