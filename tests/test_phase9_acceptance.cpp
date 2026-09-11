/**
 * Phase 9 — verification and acceptance (T9.2 / T9.3 / review §5).
 *
 * T9.1 is "both binaries green" (CTest + the two executables). This file is the
 * listening-matrix gate: each row in IMPLEMENTATION_PLAN.md T9.3 is a MIDI
 * assertion, not a DAW listen. Lock / breath / frozen-riff / reactivity /
 * cut-short live in the pipeline suite and are listed in docs/PHASE9_ACCEPTANCE.md.
 *
 *   ./build/MetalAccompanimentIntegrationTests "[phase9]"
 */

#include <catch2/catch_test_macros.hpp>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"
#include "fixtures/MidiProbe.h"
#include "midi/GrooveTemplate.h"
#include "midi/MidiPatternLibrary.h"

#include <cmath>
#include <set>
#include <string>
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
    player.setBeatGridBassEnabled(true);
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

void fillDropCPalmMute(juce::AudioBuffer<float>& buf, int64_t startSample, int n)
{
    const double sixteenth = 0.25 * 60.0 / static_cast<double>(kBpm) * kSr;
    for (int i = 0; i < n; ++i)
    {
        const int64_t s = startSample + i;
        const double posIn16 = std::fmod(static_cast<double>(s), sixteenth);
        const float env = std::exp(static_cast<float>(-posIn16 / (0.08 * kSr)));
        const float x = env * 0.35f * static_cast<float>(
            std::sin(2.0 * M_PI * 65.406 * static_cast<double>(s) / kSr));
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.setSample(ch, i, x);
    }
}

void requireFingerprintsEqual(const std::vector<MidiProbe::Event>& a,
                              const std::vector<MidiProbe::Event>& b,
                              const char* label)
{
    INFO(label << " counts " << a.size() << " vs " << b.size());
    REQUIRE(a.size() == b.size());
    REQUIRE(MidiProbe::fingerprint(a) == MidiProbe::fingerprint(b));
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

int64_t crashHoldSamples(const std::vector<MidiProbe::Event>& ev)
{
    int64_t onSample = -1;
    for (const auto& e : ev)
    {
        if (e.channel != 10 || e.note != 49)
            continue;
        if (e.isNoteOn && onSample < 0)
            onSample = e.sample;
        else if (e.isNoteOff && onSample >= 0)
            return e.sample - onSample;
    }
    return -1;
}

} // namespace

// ── T9.2 buffer-size invariance (128 / 512 / 2048) ───────────────────────────

TEST_CASE("T9.2 PatternPlayer fingerprint is identical at 128/512/2048",
          "[phase9][t9.2][golden]")
{
    MidiPatternLibrary lib;
    PatternPlayer a, b, c;
    preparePlayer(a, lib, 128, 4, 3, Groove::SongSectionId::Chorus);
    preparePlayer(b, lib, 512, 4, 3, Groove::SongSectionId::Chorus);
    preparePlayer(c, lib, 2048, 4, 3, Groove::SongSectionId::Chorus);

    const int64_t span = 2048 * 188;  // ~4 bars, divisible by 128, 512, 2048
    const auto e128  = MidiProbe::render(a, blocksFor(span, 128),  128,  0);
    const auto e512  = MidiProbe::render(b, blocksFor(span, 512),  512,  0);
    const auto e2048 = MidiProbe::render(c, blocksFor(span, 2048), 2048, 0);

    REQUIRE_FALSE(e128.empty());
    requireFingerprintsEqual(e128, e512, "128 vs 512");
    requireFingerprintsEqual(e128, e2048, "128 vs 2048");
}

// ── T9.3 listening matrix ────────────────────────────────────────────────────

TEST_CASE("T9.3 cymbals ring: crash hold >= 1 beat at 128/512/2048",
          "[phase9][t9.3][cymbals]")
{
    MidiPatternLibrary lib;
    const int64_t spb = static_cast<int64_t>(std::llround(samplesPerBeat()));
    const int64_t span = 2048 * 188;
    const int sizes[] = { 128, 512, 2048 };
    for (int bs : sizes)
    {
        PatternPlayer player;
        preparePlayer(player, lib, bs, 4, 3, Groove::SongSectionId::Chorus);
        const auto events = MidiProbe::render(player, blocksFor(span, bs), bs, 0);
        const int64_t hold = crashHoldSamples(events);
        INFO("blockSize=" << bs << " crashHold=" << hold);
        REQUIRE(hold >= spb);
    }
}

TEST_CASE("T9.3 dynamics: Metal chorus is not a wall of 127; verse/chorus delta >= 15",
          "[phase9][t9.3][dynamics]")
{
    MidiPatternLibrary lib;
    PatternPlayer chorus;
    preparePlayer(chorus, lib, 128, 4, 3, Groove::SongSectionId::Chorus);
    chorus.setGuitarEnergy(1.28f);

    const int64_t span = static_cast<int64_t>(std::llround(samplesPerBeat() * 16.0));
    const auto events = MidiProbe::render(chorus, blocksFor(span, 128), 128, 0);
    const auto s = drumVelocityStats(events);
    REQUIRE(s.n >= 20);
    const double frac127 = static_cast<double>(s.n127) / static_cast<double>(s.n);
    REQUIRE(frac127 < 0.25);
    REQUIRE(s.stddev > 12.0);

    const auto& rock = Groove::presetFor(0);
    const float accent = Groove::rock().velocityMul[4];
    const float trim = PatternPlayer::kVelocityTrim;
    const float verse = 110.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Verse)
                      * accent * trim;
    const float chorusVel = 118.0f * rock.sectionVelocityMultiplier(Groove::SongSectionId::Chorus)
                          * accent * trim;
    REQUIRE(chorusVel - verse >= 15.0f);
}

TEST_CASE("T9.3 grid: straight pattern mean onset error vs 16th grid < 2 ms",
          "[phase9][t9.3][grid]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 2048, 8, 0, Groove::SongSectionId::Chorus);
    player.setBeatGridBassEnabled(false);

    const double spb = samplesPerBeat();
    const int64_t span = static_cast<int64_t>(std::llround(spb * 32.0));
    const auto events = MidiProbe::render(player, blocksFor(span, 2048), 2048, 0);

    double sum = 0.0;
    int n = 0;
    const double sixteenth = spb / 4.0;
    for (const auto& e : events)
    {
        if (!e.isNoteOn || e.channel != 10)
            continue;
        const double nearest = std::round(static_cast<double>(e.sample) / sixteenth) * sixteenth;
        sum += (static_cast<double>(e.sample) - nearest);
        ++n;
    }
    REQUIRE(n >= 32);
    const double meanMs = (sum / static_cast<double>(n)) / kSr * 1000.0;
    INFO("mean onset error ms=" << meanMs << " n=" << n);
    REQUIRE(std::abs(meanMs) < 2.0);
}

TEST_CASE("T9.3 Play variety: default form shows >= 3 distinct grooves",
          "[phase9][t9.3][variety]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, 512);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(3.0f));  // Metal
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(kBpm));
    proc.setCustomSongForm("INTRO:4,VERSE:8,CHORUS:8,VERSE:8,CHORUS:8,OUTRO:4");
    proc.playActive.store(true, std::memory_order_release);

    struct TransportPlayHead final : public juce::AudioPlayHead
    {
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
        {
            juce::AudioPlayHead::PositionInfo info;
            info.setBpm(static_cast<double>(kBpm));
            info.setIsPlaying(true);
            info.setTimeInSamples(samples);
            return info;
        }
        int64_t samples = 0;
    } ph;
    proc.setPlayHead(&ph);

    const int64_t span = static_cast<int64_t>(40.0 * 4.0 * 60.0 / kBpm * kSr);
    const int block = 512;
    const int blocks = static_cast<int>((span + block - 1) / block);
    std::set<int> patterns;
    for (int b = 0; b < blocks; ++b)
    {
        const int64_t pos = static_cast<int64_t>(b) * block;
        ph.samples = pos;
        juce::AudioBuffer<float> buf(2, block);
        fillDropCPalmMute(buf, pos, block);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        const int idx = proc.getDisplayPatternIndex();
        if (idx > 0)
            patterns.insert(idx);
    }

    proc.playActive.store(false, std::memory_order_release);
    proc.setPlayHead(nullptr);
    proc.releaseResources();

    INFO("distinct Play patterns=" << patterns.size());
    REQUIRE(patterns.size() >= 3);
}

TEST_CASE("T9.3 bass content: Play verse pattern 22 leaks authored intervals at E",
          "[phase9][t9.3][bass]")
{
    MidiPatternLibrary lib;
    PatternPlayer player;
    preparePlayer(player, lib, 512, 22, 0, Groove::SongSectionId::Verse);
    player.setBassParams(40, 2);  // E2

    const int64_t span = static_cast<int64_t>(std::llround(samplesPerBeat() * 16.0));
    const auto events = MidiProbe::render(player, blocksFor(span, 512), 512, 0);
    std::set<int> bassNotes;
    for (const auto& e : events)
    {
        if (e.isNoteOn && e.channel == 2)
            bassNotes.insert(e.note);
    }
    REQUIRE(bassNotes.count(40) > 0);  // authored root → E
    REQUIRE(bassNotes.count(45) > 0);  // authored +5 → E+5
}
