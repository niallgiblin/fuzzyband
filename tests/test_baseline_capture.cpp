/**
 * T0.2 — Baseline MIDI capture of current (pre-fix) engine behaviour.
 *
 * Writes Standard MIDI + TSV event logs under `.artifacts/baseline/` so Phase 9
 * can A/B against this corpus. Not registered in CTest (slow; run explicitly):
 *
 *   ./build/MetalAccompanimentIntegrationTests "[baseline]"
 *
 * Scenarios (120 BPM, Metal, LOCK=4, TRANSITION=4):
 *   1. 4-bar Drop-C palm-mute into a fresh Record session at 128 / 512 / 2048
 *   2. The same riff with a 4-bar DAW loop wrapping the playhead
 *   3. Play mode through the default song form (one pass) at 512
 */

#include <catch2/catch_test_macros.hpp>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"
#include "fixtures/MidiProbe.h"
#include <cmath>
#include <fstream>
#include <string>

#if !defined(MA_REPO_ROOT)
#define MA_REPO_ROOT ""
#endif

namespace
{

constexpr double kSr = 48000.0;
constexpr float kBpm = 120.0f;
constexpr double kDropCHz = 65.406;  // C2 — drop-C low string

static std::string baselineDir()
{
    return std::string(MA_REPO_ROOT) + "/.artifacts/baseline";
}

struct TransportPlayHead final : public juce::AudioPlayHead
{
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm(static_cast<double>(kBpm));
        info.setIsPlaying(true);
        int64_t t = samples;
        if (loopLength > 0)
        {
            t = samples % loopLength;
            if (t < 0)
                t += loopLength;
        }
        info.setTimeInSamples(t);
        return info;
    }

    int64_t samples = 0;
    int64_t loopLength = 0;
};

void applyPhase0Session(AccompanimentProcessor& proc)
{
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // min = 4 bars
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(4.0f));
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(3.0f));  // Metal
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(kBpm));
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
            std::sin(2.0 * M_PI * kDropCHz * static_cast<double>(s) / kSr));
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.setSample(ch, i, x);
    }
}

std::vector<MidiProbe::Event> captureRecordSession(int blockSize, int64_t loopLength)
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, blockSize);
    proc.pauseBackgroundInferenceForTests();
    applyPhase0Session(proc);

    TransportPlayHead ph;
    ph.loopLength = loopLength;
    proc.setPlayHead(&ph);

    // Count-in (1 bar) + 4-bar capture + 4-bar lock hold, matching recordChugRiff.
    const int64_t span = static_cast<int64_t>(9.25 * 4.0 * 60.0 / kBpm * kSr);
    const int blocks = static_cast<int>((span + blockSize - 1) / blockSize);

    proc.requestRiffCaptureStart();

    auto events = MidiProbe::renderWith(blocks, blockSize, 0,
        [&](juce::MidiBuffer& midi, int n, int64_t pos)
        {
            ph.samples = pos;
            juce::AudioBuffer<float> buf(2, n);
            fillDropCPalmMute(buf, pos, n);
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
        });

    proc.setPlayHead(nullptr);
    proc.releaseResources();
    return events;
}

std::vector<MidiProbe::Event> capturePlayForm(int blockSize)
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, blockSize);
    proc.pauseBackgroundInferenceForTests();
    applyPhase0Session(proc);
    proc.setCustomSongForm("INTRO:4,VERSE:8,CHORUS:8,VERSE:8,CHORUS:8,OUTRO:4");
    proc.playActive.store(true, std::memory_order_release);

    TransportPlayHead ph;
    proc.setPlayHead(&ph);

    // Default form is 40 bars at 120 BPM.
    const int64_t span = static_cast<int64_t>(40.0 * 4.0 * 60.0 / kBpm * kSr);
    const int blocks = static_cast<int>((span + blockSize - 1) / blockSize);

    auto events = MidiProbe::renderWith(blocks, blockSize, 0,
        [&](juce::MidiBuffer& midi, int n, int64_t pos)
        {
            ph.samples = pos;
            juce::AudioBuffer<float> buf(2, n);
            fillDropCPalmMute(buf, pos, n);
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
        });

    proc.playActive.store(false, std::memory_order_release);
    proc.setPlayHead(nullptr);
    proc.releaseResources();
    return events;
}

bool writeCapture(const std::string& stem, const std::vector<MidiProbe::Event>& events)
{
    const std::string dir = baselineDir();
    juce::File(dir).createDirectory();
    const bool tsv = MidiProbe::writeTsv(dir + "/" + stem + ".tsv", events);
    const bool mid = MidiProbe::writeMidi(dir + "/" + stem + ".mid", events, kSr, kBpm);
    return tsv && mid && !events.empty();
}

} // namespace

TEST_CASE("Baseline: capture current MIDI at 128/512/2048, loop wrap, and Play form",
          "[baseline][capture]")
{
    juce::File(baselineDir()).createDirectory();

    {
        std::ofstream man(baselineDir() + "/MANIFEST.txt");
        man << "version " << MA_PLUGIN_VERSION << "\n"
            << "sampleRate " << kSr << "\n"
            << "bpm " << kBpm << "\n"
            << "genre Metal (index 3)\n"
            << "lockBars 4\n"
            << "transitionBars 4\n"
            << "riff Drop-C palm-mute (C2 65.406 Hz, 16th envelope)\n"
            << "purpose T3.1 re-baseline after velocity/humanise retune; "
               "pre-Phase-3 corpus is .artifacts/baseline/pre-phase3/\n";
    }

    const int sizes[] = { 128, 512, 2048 };
    for (int bs : sizes)
    {
        const auto events = captureRecordSession(bs, 0);
        REQUIRE(writeCapture("record_dropc_" + std::to_string(bs), events));
        REQUIRE_FALSE(MidiProbe::noteOns(events).empty());
    }

    const int64_t fourBars = static_cast<int64_t>(4.0 * 4.0 * 60.0 / kBpm * kSr);
    for (int bs : sizes)
    {
        const auto events = captureRecordSession(bs, fourBars);
        REQUIRE(writeCapture("record_dropc_loop4_" + std::to_string(bs), events));
    }

    const auto playEvents = capturePlayForm(512);
    REQUIRE(writeCapture("play_default_form_512", playEvents));
    REQUIRE_FALSE(MidiProbe::noteOns(playEvents).empty());
}

TEST_CASE("T9.2 pipeline render is buffer-size invariant (count-in + capture)",
          "[baseline][golden][t9.2]")
{
    // Review §5.2 / T9.2: the same musical span must render identical absolute
    // event samples at any host block size. Note placement and ornament selection
    // are absolute (PatternPlayer::placeEvent / per-event-bar ornaments).
    //
    // Scope: the 1-bar count-in plus the 4-bar capture. The lock's own onset is
    // still block-quantised (the capture is detected finishing on a block
    // boundary), as are fill arming and the transition start — that is T7.2 and
    // the sample-accurate state-machine work in Phases 6-7, out of scope here.
    // Note placement itself is covered end-to-end by the PatternPlayer golden.
    const int64_t span = static_cast<int64_t>(4.8 * 4.0 * 60.0 / kBpm * kSr);

    auto renderSession = [&](int blockSize) {
        AccompanimentProcessor proc;
        proc.prepareToPlay(kSr, blockSize);
        proc.pauseBackgroundInferenceForTests();
        applyPhase0Session(proc);

        TransportPlayHead ph;
        proc.setPlayHead(&ph);
        proc.requestRiffCaptureStart();

        const int blocks = static_cast<int>((span + blockSize - 1) / blockSize);
        auto events = MidiProbe::renderWith(blocks, blockSize, 0,
            [&](juce::MidiBuffer& midi, int n, int64_t pos)
            {
                ph.samples = pos;
                juce::AudioBuffer<float> buf(2, n);
                fillDropCPalmMute(buf, pos, n);
                proc.processBlock(buf, midi);
                proc.flushBackgroundInferenceForTests();
            });

        proc.setPlayHead(nullptr);
        proc.releaseResources();
        return events;
    };

    auto within = [&](const std::vector<MidiProbe::Event>& ev) {
        std::vector<MidiProbe::Event> out;
        for (const auto& e : ev)
            if (e.sample < span)
                out.push_back(e);
        return out;
    };
    const auto ea = within(renderSession(128));
    const auto eb = within(renderSession(2048));

    REQUIRE_FALSE(ea.empty());
    INFO("events: 128=" << ea.size() << " 2048=" << eb.size());
    REQUIRE(ea.size() == eb.size());

    for (size_t i = 0; i < ea.size(); ++i)
    {
        INFO("event " << i << " at " << ea[i].sample << " vs " << eb[i].sample
             << " (note " << (int) ea[i].note << ")");
        REQUIRE(ea[i].sample == eb[i].sample);
        REQUIRE(ea[i].note == eb[i].note);
        REQUIRE(ea[i].channel == eb[i].channel);
        REQUIRE(ea[i].velocity == eb[i].velocity);
        REQUIRE(ea[i].isNoteOn == eb[i].isNoteOn);
        REQUIRE(ea[i].isNoteOff == eb[i].isNoteOff);
    }
}
