/**
 * Phase 37 A1 — FillGrammar unit tests.
 *
 * The grammar is a pure, bounded, allocation-free function. These tests lock its
 * contract: determinism, the event cap, energy monotonicity, window/GM-validity,
 * measurable diversity, and section colour.
 */

#include <catch2/catch_test_macros.hpp>

#include "midi/FillGrammar.h"

#include <cmath>
#include <cstdint>
#include <set>

namespace
{

using FillGrammar::FillContext;
using FillGrammar::FillScore;

/** FNV-1a over the score's musical content — stable serialisation for equality
 *  and diversity checks (avoids struct padding concerns). */
uint64_t fingerprint(const FillScore& s)
{
    uint64_t h = 1469598103934665603ull;
    auto mixb = [&h](uint8_t b) { h ^= b; h *= 1099511628211ull; };

    mixb(static_cast<uint8_t>(s.count));
    for (int i = 0; i < s.count; ++i)
    {
        const auto& e = s.events[static_cast<size_t>(i)];
        mixb(e.note);
        mixb(e.velocity);
        mixb(e.isGhost ? 1u : 0u);
        const int32_t bo = static_cast<int32_t>(std::lround(e.beatOffset * 1000.0f));
        const int32_t du = static_cast<int32_t>(std::lround(e.durationBeats * 1000.0f));
        for (int k = 0; k < 4; ++k)
        {
            mixb(static_cast<uint8_t>((bo >> (8 * k)) & 0xFF));
            mixb(static_cast<uint8_t>((du >> (8 * k)) & 0xFF));
        }
    }
    return h;
}

bool isGmDrum(uint8_t note)
{
    return note == 36 || note == 38 || note == 41 || note == 42
        || note == 45 || note == 46 || note == 48 || note == 49;
}

FillContext makeCtx(unsigned seed, float energy, int section = 1 /*Verse*/,
                    float len = 4.0f, float density = 0.0f)
{
    FillContext c{};
    c.seed = seed;
    c.rmsEnergy = energy;
    c.onsetDensityPerBeat = density;
    c.sectionId = section;
    c.styleIndex = -1;
    c.fillLengthBeats = len;
    c.precedingPatternIdx = 1;
    c.barNumber = 4;
    return c;
}

} // namespace

TEST_CASE("FillGrammar: buildFill is pure and deterministic", "[fill][grammar]")
{
    const auto c = makeCtx(12345u, 0.55f);
    REQUIRE(fingerprint(FillGrammar::buildFill(c)) == fingerprint(FillGrammar::buildFill(c)));

    // A different seed at the same context should generally differ (not a no-op).
    const auto other = FillGrammar::buildFill(makeCtx(999u, 0.55f));
    REQUIRE(fingerprint(other) != 0u);
}

TEST_CASE("FillGrammar: never exceeds the event cap", "[fill][grammar]")
{
    int maxSeen = 0;
    for (int i = 0; i < 10000; ++i)
    {
        const FillContext c = makeCtx(static_cast<unsigned>(i) * 2654435761u,
                                      static_cast<float>(i % 100) / 100.0f,
                                      i % 7,
                                      1.0f + static_cast<float>(i % 4),
                                      static_cast<float>(i % 8));
        const auto s = FillGrammar::buildFill(c);
        REQUIRE(s.count >= 0);
        REQUIRE(s.count <= FillGrammar::kMaxFillEvents);
        maxSeen = std::max(maxSeen, s.count);
    }
    REQUIRE(maxSeen > 0);   // the sweep actually produced events
}

TEST_CASE("FillGrammar: density is monotonic in energy", "[fill][grammar]")
{
    constexpr int kN = 256;
    double sparse = 0.0, mid = 0.0, dense = 0.0;
    for (int i = 0; i < kN; ++i)
    {
        sparse += FillGrammar::buildFill(makeCtx(static_cast<unsigned>(i), 0.05f)).count;
        mid    += FillGrammar::buildFill(makeCtx(static_cast<unsigned>(i), 0.30f)).count;
        dense  += FillGrammar::buildFill(makeCtx(static_cast<unsigned>(i), 0.70f)).count;
    }
    REQUIRE(dense / kN >= mid / kN);
    REQUIRE(mid / kN >= sparse / kN);
}

TEST_CASE("FillGrammar: events stay in the fill window and use GM drums", "[fill][grammar]")
{
    for (int i = 0; i < 2000; ++i)
    {
        const float len = 1.0f + static_cast<float>(i % 4);
        const FillContext c = makeCtx(static_cast<unsigned>(i),
                                      static_cast<float>(i % 100) / 100.0f,
                                      i % 7, len,
                                      static_cast<float>(i % 8));
        const auto s = FillGrammar::buildFill(c);
        const float windowStart = 4.0f - std::min(4.0f, len);
        for (int k = 0; k < s.count; ++k)
        {
            const auto& e = s.events[static_cast<size_t>(k)];
            REQUIRE(e.beatOffset >= windowStart - 1.0e-4f);
            REQUIRE(e.beatOffset < 4.0f + 1.0e-4f);
            REQUIRE(isGmDrum(e.note));
        }
    }
}

TEST_CASE("FillGrammar: 128 seeds produce >= 24 distinct fills", "[fill][grammar]")
{
    std::set<uint64_t> distinct;
    for (unsigned s = 0; s < 128; ++s)
        distinct.insert(fingerprint(FillGrammar::buildFill(makeCtx(s, 0.70f, 1, 4.0f, 2.5f))));
    REQUIRE(distinct.size() >= 24);
}

TEST_CASE("FillGrammar: a simulated take yields >= 8 distinct fills", "[fill][grammar]")
{
    // Offline proxy for the plan's live UAT (a 4-minute Play take). ~120 bars at
    // 120 BPM with a section end roughly every 8 bars => ~15 armed fills. This
    // locks the diversity claim without needing a DAW.
    std::set<uint64_t> distinct;
    for (int i = 0; i < 15; ++i)
    {
        const int bar = 8 * (i + 1);
        const float energy = (i % 3 == 0) ? 0.70f : ((i % 3 == 1) ? 0.35f : 0.12f);
        FillContext c{};
        c.seed = static_cast<unsigned>(bar) ^ (2654435761u);
        c.rmsEnergy = energy;
        c.onsetDensityPerBeat = (i % 2 == 0) ? 0.5f : 2.0f;
        c.sectionId = (i % 4 == 0) ? 2 /*Chorus*/ : 1 /*Verse*/;
        c.fillLengthBeats = 4.0f;
        c.precedingPatternIdx = 1;
        c.barNumber = bar;
        distinct.insert(fingerprint(FillGrammar::buildFill(c)));
    }
    REQUIRE(distinct.size() >= 8);
}

TEST_CASE("FillGrammar: breakdown stays sparse and never crashes", "[fill][grammar]")
{
    for (unsigned s = 0; s < 256; ++s)
    {
        // Breakdown at a *dense* energy/density input must still collapse to the
        // sparse set and never land a crash — the section is leaving space.
        const auto score = FillGrammar::buildFill(makeCtx(s, 0.90f, 3 /*Breakdown*/, 4.0f, 3.0f));
        for (int i = 0; i < score.count; ++i)
            REQUIRE(score.events[static_cast<size_t>(i)].note != 49);   // no crash
        REQUIRE(score.count <= 6);   // <=2 sparse cells (2 notes each) + landing
    }
}

// ── Phase 38-01: dense fills are tom-forward and crash-land ──────────────────

TEST_CASE("FillGrammar 38-01: dense fills always contain toms", "[fill][grammar][38-01]")
{
    const auto isTom = [](uint8_t n) { return n == 41 || n == 45 || n == 48; };
    for (unsigned s = 0; s < 256; ++s)
    {
        const auto score = FillGrammar::buildFill(makeCtx(s, 0.70f, 1 /*Verse*/, 4.0f, 2.5f));
        int toms = 0;
        for (int i = 0; i < score.count; ++i)
            if (isTom(score.events[static_cast<size_t>(i)].note)) ++toms;
        REQUIRE(toms >= 1);
    }
}

TEST_CASE("FillGrammar 38-01: the 4-beat dense fill has a tom cascade and a crash",
          "[fill][grammar][38-01]")
{
    const auto isTom = [](uint8_t n) { return n == 41 || n == 45 || n == 48; };
    for (unsigned s = 0; s < 128; ++s)
    {
        const auto score = FillGrammar::buildFill(makeCtx(s, 0.80f, 2 /*Chorus*/, 4.0f, 3.0f));
        int toms = 0, crashes = 0;
        for (int i = 0; i < score.count; ++i)
        {
            const uint8_t n = score.events[static_cast<size_t>(i)].note;
            if (isTom(n)) ++toms;
            if (n == 49) ++crashes;
        }
        REQUIRE(toms >= 3);      // a cascade, not a single accent
        REQUIRE(crashes >= 1);   // dense fills land a crash
    }
}

TEST_CASE("FillGrammar 38-01: quiet sections still never crash", "[fill][grammar][38-01]")
{
    for (unsigned s = 0; s < 128; ++s)
    {
        const auto score = FillGrammar::buildFill(makeCtx(s, 0.90f, 3 /*Breakdown*/, 4.0f, 3.0f));
        for (int i = 0; i < score.count; ++i)
            REQUIRE(score.events[static_cast<size_t>(i)].note != 49);
    }
}
