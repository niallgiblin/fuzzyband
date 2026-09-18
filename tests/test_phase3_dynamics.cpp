/**
 * Phase 3 — dynamics and humanisation (T3.1–T3.4).
 *
 * Velocity headroom, bidirectional guitar energy, centred microtiming, and
 * deterministic per-event humanisation (review §2.3, §2.10, §2.11 / §5.4).
 */

#include <catch2/catch_test_macros.hpp>

#include "fixtures/MidiProbe.h"
#include "midi/GrooveTemplate.h"
#include "midi/MidiPatternLibrary.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

constexpr double kSr = 48000.0;
constexpr float kBpm = 120.0f;

void preparePlayer(PatternPlayer& player, MidiPatternLibrary& lib, int blockSize,
                   int patternIndex, int genreId, Groove::SongSectionId section)
{
    player.setPatternLibrary(&lib);
    player.prepare(kSr, blockSize);
    player.setRandomSeed(0xC0FFEE);
    player.snapBpm(kBpm);
    player.setPatternIndex(patternIndex);
    player.setSection(section);
    player.setGenrePreset(genreId);
    player.setStructureSilent(false);
    player.setSwing(0.0f);
    player.setBeatGridBassEnabled(false);
    player.setBassParams(40, 2);
}

int blocksFor(int64_t span, int blockSize)
{
    return static_cast<int>((span + blockSize - 1) / blockSize);
}

double samplesPerBeat()
{
    return (60.0 / static_cast<double>(kBpm)) * kSr;
}

struct VelStats
{
    int n = 0;
    int n127 = 0;
    double mean = 0.0;
    double stddev = 0.0;
};

VelStats drumVelocityStats(const std::vector<MidiProbe::Event>& events)
{
    std::vector<int> vels;
    for (const auto& e : events)
    {
        if (e.isNoteOn && e.channel == 10)
            vels.push_back(e.velocity);
    }
    VelStats s;
    s.n = static_cast<int>(vels.size());
    if (s.n == 0)
        return s;
    double sum = 0.0;
    for (int v : vels)
    {
        sum += static_cast<double>(v);
        if (v >= 127)
            ++s.n127;
    }
    s.mean = sum / static_cast<double>(s.n);
    double var = 0.0;
    for (int v : vels)
    {
        const double d = static_cast<double>(v) - s.mean;
        var += d * d;
    }
    s.stddev = std::sqrt(var / static_cast<double>(s.n));
    return s;
}

int meanDrumVelocity(PatternPlayer& player, int bars)
{
    const int64_t span = static_cast<int64_t>(std::llround(samplesPerBeat() * 4.0 * bars));
    const auto events = MidiProbe::render(player, blocksFor(span, 512), 512, 0);
    const auto s = drumVelocityStats(events);
    return s.n == 0 ? 0 : static_cast<int>(std::lround(s.mean));
}

} // namespace

// ── T3.1 velocity headroom ───────────────────────────────────────────────────

TEST_CASE("T3.1 metal chorus at guitarEnergy 1.28 is not a wall of 127", "[midi][phase3][t3.1]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 128, 4, 3, Groove::SongSectionId::Chorus);  // Chorus Mid, Metal
    player.setGuitarEnergy(1.28f);  // clamps to 1.20 (T3.2 range); still a hot chorus

    const int64_t span = static_cast<int64_t>(std::llround(samplesPerBeat() * 16.0));  // 4 bars
    const auto events = MidiProbe::render(player, blocksFor(span, 128), 128, 0);
    const auto s = drumVelocityStats(events);
    REQUIRE(s.n >= 20);
    const double frac127 = static_cast<double>(s.n127) / static_cast<double>(s.n);
    REQUIRE(frac127 < 0.25);
    REQUIRE(s.stddev > 12.0);
}

TEST_CASE("T3.1 verse/chorus backbeat delta stays >= 15 after trim", "[midi][phase3][t3.1]")
{
    const auto& rock = Groove::presetFor(0);
    const float accent = Groove::rock().velocityMul[4];
    const float trim = PatternPlayer::kVelocityTrim;
    const float verse = 110.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Verse)
                      * accent * trim;
    const float chorus = 118.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Chorus)
                       * accent * trim;
    REQUIRE(chorus - verse >= 15.0f);
}

// ── T3.2 bidirectional guitar energy ─────────────────────────────────────────

TEST_CASE("T3.2 guitarEnergyFromRms spans both sides of unity", "[midi][phase3][t3.2]")
{
    const float quiet = PatternPlayer::guitarEnergyFromRms(0.0f);
    const float loud = PatternPlayer::guitarEnergyFromRms(1.0f);
    REQUIRE(quiet < 1.0f);
    REQUIRE(loud > 1.0f);
    REQUIRE(quiet >= 0.85f);
    REQUIRE(loud <= 1.20f);
}

TEST_CASE("T3.2 quiet passage renders softer than a loud one", "[midi][phase3][t3.2]")
{
    MidiPatternLibrary lib;
    PatternPlayer quiet, loud;
    preparePlayer(quiet, lib, 512, 4, 3, Groove::SongSectionId::Chorus);
    preparePlayer(loud, lib, 512, 4, 3, Groove::SongSectionId::Chorus);
    quiet.setGuitarEnergy(0.85f);
    loud.setGuitarEnergy(1.20f);

    const int q = meanDrumVelocity(quiet, 4);
    const int l = meanDrumVelocity(loud, 4);
    REQUIRE(q > 0);
    REQUIRE(l > q + 4);
}

// ── T3.3 centred microtiming ─────────────────────────────────────────────────

TEST_CASE("T3.3 mean onset error vs the 16th grid is under 2 ms", "[midi][phase3][t3.3]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    // Pattern 8 (Blast Beat) hits 16ths including the laid-back 'e' cells, so
    // the centred template's residual shape is audible. 2048-sample blocks keep
    // ±2.5σ jitter inside the block (128-sample buffers clamp early hits to 0).
    preparePlayer(player, lib, 2048, 8, 0, Groove::SongSectionId::Chorus);
    // T3.3 measures the BAKED BASE template's residual shape. Phase 37 C1 adds a
    // per-pattern feel on top (tightening dense patterns); that layer is tested
    // separately, so pin this to humanize=0 (base template, as calibrated).
    player.setHumanize(0.0f);

    const double spb = samplesPerBeat();
    const int64_t span = static_cast<int64_t>(std::llround(spb * 32.0));  // 8 bars
    const auto events = MidiProbe::render(player, blocksFor(span, 2048), 2048, 0);

    std::vector<double> errMs;
    for (const auto& e : events)
    {
        if (!e.isNoteOn || e.channel != 10)
            continue;
        const double beat = static_cast<double>(e.sample) / spb;
        const double grid = std::round(beat * 4.0) / 4.0;
        const double ms = (beat - grid) * (60000.0 / static_cast<double>(kBpm));
        if (std::abs(ms) > 25.0)  // drop 32nd micro-fill pickups
            continue;
        errMs.push_back(ms);
    }
    REQUIRE(errMs.size() >= 32);

    double sum = 0.0;
    for (double x : errMs)
        sum += x;
    const double mean = sum / static_cast<double>(errMs.size());
    double var = 0.0;
    for (double x : errMs)
    {
        const double d = x - mean;
        var += d * d;
    }
    const double stddev = std::sqrt(var / static_cast<double>(errMs.size()));

    REQUIRE(std::abs(mean) < 2.0);
    REQUIRE(stddev >= 3.0);
    REQUIRE(stddev <= 15.0);
}

TEST_CASE("T3.3 baked templates have |mean(timingMs)| < 1.5 ms", "[midi][phase3][t3.3]")
{
    REQUIRE(std::abs(Groove::timingMeanMs(Groove::rock().timingMs)) < 1.5f);
    REQUIRE(std::abs(Groove::timingMeanMs(Groove::metal().timingMs)) < 1.5f);
    REQUIRE(std::abs(Groove::timingMeanMs(Groove::punk().timingMs)) < 1.5f);
    REQUIRE(Groove::rock().timingJitterMs <= 3.0f);
    REQUIRE(Groove::metal().timingJitterMs <= 2.0f);
}

// ── T3.4 deterministic humanisation ──────────────────────────────────────────

TEST_CASE("T3.4 same 8 bars render identically twice", "[midi][phase3][t3.4]")
{
    MidiPatternLibrary lib;
    PatternPlayer a, b;
    preparePlayer(a, lib, 512, 4, 3, Groove::SongSectionId::Chorus);
    preparePlayer(b, lib, 512, 4, 3, Groove::SongSectionId::Chorus);

    const int64_t span = static_cast<int64_t>(std::llround(samplesPerBeat() * 32.0));
    const auto ea = MidiProbe::render(a, blocksFor(span, 512), 512, 0);
    const auto eb = MidiProbe::render(b, blocksFor(span, 512), 512, 0);
    REQUIRE(MidiProbe::fingerprint(ea) == MidiProbe::fingerprint(eb));
}

TEST_CASE("T3.4 humanisation is block-size invariant", "[midi][phase3][t3.4]")
{
    MidiPatternLibrary lib;
    PatternPlayer a, b;
    preparePlayer(a, lib, 128, 4, 3, Groove::SongSectionId::Chorus);
    preparePlayer(b, lib, 2048, 4, 3, Groove::SongSectionId::Chorus);

    const int64_t span = 2048 * 188;  // ~4 bars, multiple of both sizes
    auto e128 = MidiProbe::noteOns(MidiProbe::render(a, blocksFor(span, 128), 128, 0));
    auto e2048 = MidiProbe::noteOns(MidiProbe::render(b, blocksFor(span, 2048), 2048, 0));
    REQUIRE(e128.size() == e2048.size());
    REQUIRE_FALSE(e128.empty());

    // Hash-keyed draws must not depend on block size (the old stream RNG did).
    // Sample positions can still differ by a clamp into the current block when
    // an early/late offset crosses a 128-sample boundary; the (note, vel) bag
    // must not.
    auto byNoteVel = [](const MidiProbe::NoteOn& x, const MidiProbe::NoteOn& y)
    {
        if (x.channel != y.channel) return x.channel < y.channel;
        if (x.note != y.note) return x.note < y.note;
        return x.velocity < y.velocity;
    };
    std::sort(e128.begin(), e128.end(), byNoteVel);
    std::sort(e2048.begin(), e2048.end(), byNoteVel);
    for (size_t i = 0; i < e128.size(); ++i)
    {
        REQUIRE(e128[i].channel == e2048[i].channel);
        REQUIRE(e128[i].note == e2048[i].note);
        REQUIRE(e128[i].velocity == e2048[i].velocity);
    }
}
