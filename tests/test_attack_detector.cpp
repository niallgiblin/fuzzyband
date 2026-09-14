/**
 * @file
 * @brief AttackDetector unit tests — the four-term attack predicate and its
 *        time-based decay window, measured through the module's own interface.
 *
 * These are the tests the bass-mirror post-mortem asked for
 * (`docs/BASS_MIRRORING.md` §6, H1/H2): they assert *why* a block was refused
 * (`AttackVerdict::blockedBy`), not just the cumulative counters, and they pin
 * the decay-recency window to musical time rather than block count.
 */

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

#include "analysis/AttackDetector.h"

namespace
{
using Blocked = AttackVerdict::Blocked;

// Attacks must be > kMinAttackIntervalSamples (2000) apart; 3000 gives headroom.
constexpr std::int64_t kStep = 3000;

/**
 * @brief The attack predicate exactly as it stood inside PhraseLearner before
 *        the extraction, used as the differential oracle for @ref AttackDetector.
 *
 * This is the behaviour-freeze guard for the move: shape changed, decisions did not.
 */
struct ReferenceDetector
{
    double sampleRate = 48000.0;
    float prevRms = 0.0f;
    float rmsSmooth = 0.0f;
    std::int64_t lastFallSample = -1;
    std::int64_t fallWindowSamples = 9600;
    float rmsFloorSinceArm = 0.0f;
    bool risePending = false;
    std::int64_t lastAttackSample = 0;
    static constexpr std::int64_t kMinAttackIntervalSamples = 2000;

    void prepare(double sr)
    {
        sampleRate = sr;
        fallWindowSamples = static_cast<std::int64_t>(std::llround(0.2 * sr));
        reset();
    }
    void reset()
    {
        prevRms = 0.0f; rmsSmooth = 0.0f; lastFallSample = -1;
        rmsFloorSinceArm = 0.0f; risePending = false; lastAttackSample = 0;
    }

    bool classify(float rms, std::int64_t sampleTime)
    {
        const float prev = prevRms;
        prevRms = rms;
        rmsSmooth = 0.85f * rmsSmooth + 0.15f * rms;
        const bool fell = (rms < prev);
        if (fell) { lastFallSample = sampleTime; rmsFloorSinceArm = rms; }
        else if (lastFallSample >= 0 && (sampleTime - lastFallSample) < fallWindowSamples)
        {
            if (rms < rmsFloorSinceArm) rmsFloorSinceArm = rms;
        }
        if (rms < 0.002f) { risePending = false; return false; }
        const bool armed = (lastFallSample >= 0) && (sampleTime - lastFallSample) < fallWindowSamples;
        const bool sharpRise = (rms > rmsSmooth * 1.15f) || (rms > prev * 1.08f);
        const bool clearsFloor = (rms > rmsFloorSinceArm * 1.15f + 0.005f);
        const bool aboveFloor = (rms > 0.01f);
        const bool riseEdge = armed && sharpRise && clearsFloor && aboveFloor;
        if (riseEdge) risePending = true;
        const bool canAttack = (sampleTime - lastAttackSample) > kMinAttackIntervalSamples;
        const bool attack = canAttack && risePending;
        if (attack)
        {
            lastAttackSample = sampleTime;
            lastFallSample = -1;
            risePending = false;
            rmsFloorSinceArm = rms;
        }
        return attack;
    }
};
} // namespace

TEST_CASE("AttackDetector: a decay then a sharp rise is accepted", "[attack][detector]")
{
    AttackDetector d;
    d.prepare(48000.0);

    const auto first  = d.classify(0.20f, 0);
    const auto fall   = d.classify(0.10f, kStep);
    const auto rise   = d.classify(0.30f, 2 * kStep);

    REQUIRE_FALSE(first.accepted);                       // no prior fall yet
    REQUIRE(first.blockedBy == Blocked::NoRecentFall);
    REQUIRE_FALSE(fall.accepted);                        // the fall itself is not an attack
    REQUIRE(rise.accepted);
    REQUIRE(rise.blockedBy == Blocked::None);
    REQUIRE(rise.troughMargin > 0.0f);
    REQUIRE(rise.riseRatio > 1.0f);
    REQUIRE(rise.clearsFloor);
    REQUIRE(rise.sharpRise);
    REQUIRE(rise.aboveFloor);
    REQUIRE(d.getDebug().accepted == 1);
    REQUIRE(d.getLastAttackSample() == 2 * kStep);
}

TEST_CASE("AttackDetector: a block below the amplitude floor is refused",
          "[attack][detector]")
{
    AttackDetector d;
    d.prepare(48000.0);

    const auto v = d.classify(0.001f, 0);
    REQUIRE_FALSE(v.accepted);
    REQUIRE(v.blockedBy == Blocked::BelowAmplitudeFloor);
    REQUIRE_FALSE(v.aboveFloor);
}

TEST_CASE("AttackDetector: a rise with no preceding decay is refused",
          "[attack][detector]")
{
    AttackDetector d;
    d.prepare(48000.0);

    const auto v = d.classify(0.20f, 0);
    REQUIRE_FALSE(v.accepted);
    REQUIRE(v.blockedBy == Blocked::NoRecentFall);
    REQUIRE_FALSE(v.armed);
}

TEST_CASE("AttackDetector: a rise that does not clear the trough is refused",
          "[attack][detector]")
{
    AttackDetector d;
    d.prepare(48000.0);

    d.classify(0.40f, 0);
    const auto fall = d.classify(0.30f, kStep);
    const auto rise = d.classify(0.33f, 2 * kStep);   // < 0.30*1.15 + 0.005

    REQUIRE_FALSE(fall.accepted);
    REQUIRE_FALSE(rise.accepted);
    REQUIRE(rise.blockedBy == Blocked::TroughTooShallow);
    REQUIRE(rise.sharpRise);
    REQUIRE_FALSE(rise.clearsFloor);
    REQUIRE(rise.troughMargin < 0.0f);
}

TEST_CASE("AttackDetector: a shallow rise off a steady level is refused as not sharp",
          "[attack][detector]")
{
    AttackDetector d;
    d.prepare(48000.0);

    // Steady level (no fall, so nothing arms), then a dip, then a tiny rise.
    for (int i = 0; i < 50; ++i)
        d.classify(0.50f, static_cast<std::int64_t>(i) * kStep);
    d.classify(0.49f, 50 * kStep);                     // arms the window
    const auto rise = d.classify(0.50f, 51 * kStep);   // +2%: neither 15% nor 8%

    REQUIRE_FALSE(rise.accepted);
    REQUIRE(rise.blockedBy == Blocked::NoSharpRise);
    REQUIRE(rise.armed);
    REQUIRE(rise.aboveFloor);
    REQUIRE_FALSE(rise.sharpRise);
}

TEST_CASE("AttackDetector: a real edge held behind the min-interval gate is refused",
          "[attack][detector]")
{
    AttackDetector d;
    d.prepare(48000.0);

    d.classify(0.20f, 0);
    d.classify(0.10f, kStep);
    REQUIRE(d.classify(0.30f, 2 * kStep).accepted);     // first attack

    d.classify(0.05f, 2 * kStep + 500);                 // fall, re-arms
    const auto tooSoon = d.classify(0.40f, 2 * kStep + 1000);

    REQUIRE_FALSE(tooSoon.accepted);
    REQUIRE(tooSoon.blockedBy == Blocked::MinIntervalGate);
    REQUIRE(d.getDebug().accepted == 1);
}

TEST_CASE("AttackDetector: the decay-recency window is musical time, not block count",
          "[attack][detector]")
{
    constexpr double kSr = 48000.0;
    const auto fallAt = static_cast<std::int64_t>(0.15 * kSr);

    // A decay 0.19 s before the rise is inside the 0.2 s window → armed.
    AttackDetector inside;
    inside.prepare(kSr);
    inside.classify(0.35f, 0);
    inside.classify(0.10f, fallAt);
    const auto in = inside.classify(0.40f, fallAt + static_cast<std::int64_t>(0.19 * kSr));
    REQUIRE(in.accepted);

    // A decay 0.21 s before the rise is outside the window → not armed.
    AttackDetector outside;
    outside.prepare(kSr);
    outside.classify(0.35f, 0);
    outside.classify(0.10f, fallAt);
    const auto out = outside.classify(0.40f, fallAt + static_cast<std::int64_t>(0.21 * kSr));
    REQUIRE_FALSE(out.accepted);
    REQUIRE(out.blockedBy == Blocked::NoRecentFall);
    REQUIRE_FALSE(out.armed);
}

TEST_CASE("AttackDetector: the decay window scales with the sample rate",
          "[attack][detector]")
{
    AttackDetector at44;
    at44.prepare(44100.0);
    AttackDetector at48;
    at48.prepare(48000.0);

    REQUIRE(at44.getFallWindowSamples() == 8820);   // 0.2 s @ 44.1k
    REQUIRE(at48.getFallWindowSamples() == 9600);   // 0.2 s @ 48k
}

TEST_CASE("AttackDetector: matches the pre-extraction predicate (behaviour freeze)",
          "[attack][detector][freeze]")
{
    // The extraction moved the predicate into its own module. Decisions must be
    // identical for the same input stream, including the latched decay→rise edge
    // and the min-interval gate. This is the guard that the move changed shape
    // only — the mirror's behaviour is unchanged by it.
    constexpr double kSr = 48000.0;
    AttackDetector detector;
    detector.prepare(kSr);
    ReferenceDetector reference;
    reference.prepare(kSr);

    std::uint32_t s = 0x1234567u;
    auto rnd = [&s]() -> float
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 16777216.0f;
    };

    std::int64_t t = 0;
    int acceptedNew = 0, acceptedRef = 0;
    for (int i = 0; i < 20000; ++i)
    {
        // Chug-like pulse train riding on a noisy floor.
        const bool pulse = (i % 40) == 0;
        const float rms = pulse ? (0.04f + 0.36f * rnd()) : (0.001f + 0.03f * rnd());

        const auto v = detector.classify(rms, t);
        const bool r = reference.classify(rms, t);
        if (v.accepted != r)
        {
            INFO("divergence at block " << i << " rms=" << rms);
            REQUIRE(v.accepted == r);
        }
        if (v.accepted) ++acceptedNew;
        if (r) ++acceptedRef;
        t += 512;
    }

    REQUIRE(acceptedNew == acceptedRef);
    REQUIRE(acceptedNew > 0);
    REQUIRE(detector.getDebug().accepted == acceptedNew);
}
