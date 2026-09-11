/**
 * Phase 2 — clock and schedule correctness (T2.1–T2.2).
 *
 * Lock / transition durations must use the monotonic plugin clock so a DAW
 * loop wrap cannot freeze the hold or silence frozen bass (review §5.1).
 * A host seek re-latches bar phase while preserving remaining duration.
 */

#include <catch2/catch_test_macros.hpp>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

constexpr double kSr = 48000.0;
constexpr float kBpm = 120.0f;
constexpr int kBlock = 512;

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

void fillChugBlock(juce::AudioBuffer<float>& buf, int blockIdx, int block, double freq)
{
    constexpr int cycle = 24;
    const int pos = blockIdx % cycle;
    const bool loud = (pos >= cycle - 4);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* p = buf.getWritePointer(ch);
        const double t = static_cast<double>(blockIdx) * block / kSr;
        for (int i = 0; i < block; ++i)
        {
            const double tt = t + static_cast<double>(i) / kSr;
            p[i] = static_cast<float>((loud ? 0.5 : 0.08) * std::sin(2.0 * M_PI * freq * tt));
        }
    }
}

void applyLockSession(AccompanimentProcessor& proc, float lockBars, float transitionBars)
{
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(lockBars));
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(transitionBars));
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(kBpm));
}

int recordChugRiff(AccompanimentProcessor& proc, TransportPlayHead& ph, int& blockIdx, double freq)
{
    proc.requestRiffCaptureStart();
    const int n = static_cast<int>(5.25 * 4.0 * 60.0 / kBpm * kSr / kBlock);
    int bassDuring = 0;
    for (int b = 0; b < n; ++b)
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        fillChugBlock(buf, blockIdx, kBlock, freq);
        juce::MidiBuffer midi;
        ph.samples = static_cast<int64_t>(blockIdx) * kBlock;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
                ++bassDuring;
        }
        ++blockIdx;
        if (proc.isGrooveLocked())
            break;
    }
    return bassDuring;
}

struct NoteOn
{
    int note = 0;
    int channel = 0;
    int64_t sample = 0;
};

void collectNoteOns(const juce::MidiBuffer& midi, int64_t blockStart, std::vector<NoteOn>& out)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (!msg.isNoteOn() || msg.getVelocity() <= 0)
            continue;
        out.push_back({ msg.getNoteNumber(), msg.getChannel(),
                        blockStart + meta.samplePosition });
    }
}

} // namespace

TEST_CASE("T2.1 lock survives a 4-bar DAW loop and expires on the mono clock",
          "[integration][pipeline][phase2][t2.1][clock]")
{
    const double samplesPerBeat = 60.0 / kBpm * kSr;
    const int64_t samplesPerBar = static_cast<int64_t>(4.0 * samplesPerBeat);
    const int64_t loopLength = 4 * samplesPerBar;

    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, kBlock);
    proc.pauseBackgroundInferenceForTests();
    applyLockSession(proc, 16.0f, 4.0f);

    TransportPlayHead ph;
    proc.setPlayHead(&ph);

    int blockIdx = 0;
    recordChugRiff(proc, ph, blockIdx, 65.406);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getSectionPhase() == 2);
    const int occupied = proc.getRiffAOccupiedCount();
    REQUIRE(occupied >= 2);
    int onsets = 0;
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
        if (proc.getRiffASlotGate(s) > 0)
            ++onsets;
    if (onsets < 1)
        onsets = occupied;  // back-compat if gates were not stamped

    const int barAtLock = proc.getLockBarCurrent();
    REQUIRE(barAtLock >= 1);

    ph.loopLength = loopLength;

    std::vector<NoteOn> afterLoop;
    int64_t firstWrapSample = -1;
    int64_t wrapRiffOrigin = -1;
    const int lockBlocks = static_cast<int>(20.0 * samplesPerBar / kBlock);
    bool sawTransition = false;
    int maxLockBar = barAtLock;

    for (int b = 0; b < lockBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        fillChugBlock(buf, blockIdx, kBlock, 65.406);
        juce::MidiBuffer midi;
        const int64_t pos = static_cast<int64_t>(blockIdx) * kBlock;
        ph.samples = pos;
        if (firstWrapSample < 0)
        {
            firstWrapSample = pos;
            // Read the origin now: every return-to-A re-latches it, so reading it
            // after the run would give the last re-lock's origin, not this cycle's.
            wrapRiffOrigin = proc.getRiffAPlayOriginSample();
        }
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        collectNoteOns(midi, pos, afterLoop);
        maxLockBar = std::max(maxLockBar, proc.getLockBarCurrent());
        if (proc.isTransitionSectionActive())
        {
            sawTransition = true;
            break;
        }
        ++blockIdx;
    }

    REQUIRE(firstWrapSample >= 0);

    int bassAfterWrap = 0;
    int bassInFirstCycle = 0;
    int bassOnGrid = 0;
    // Count exactly one riff loop, measured from the processor's bar-locked riff
    // origin. `firstWrapSample` is a block boundary, so a block-aligned window can
    // clip one onset and make a full loop look one note short.
    const int64_t riffOrigin = wrapRiffOrigin;
    REQUIRE(riffOrigin >= 0);
    REQUIRE(firstWrapSample >= riffOrigin);
    const int64_t loopSamples = static_cast<int64_t>(std::llround(16.0 * samplesPerBeat));
    REQUIRE(loopSamples > 0);
    const int64_t cycleStart = riffOrigin
        + ((firstWrapSample - riffOrigin) / loopSamples + 1) * loopSamples;
    const int64_t cycleEnd = cycleStart + loopSamples;
    const int64_t sixteenth = static_cast<int64_t>(samplesPerBeat / 4.0);
    const int64_t gridTol = static_cast<int64_t>(0.020 * kSr);
    for (const auto& n : afterLoop)
    {
        if (n.sample < firstWrapSample || n.channel != 2)
            continue;
        ++bassAfterWrap;
        if (n.sample >= cycleStart && n.sample < cycleEnd)
            ++bassInFirstCycle;
        const int64_t into16 = n.sample % sixteenth;
        if (into16 <= gridTol || sixteenth - into16 <= gridTol)
            ++bassOnGrid;
    }

    REQUIRE(bassAfterWrap >= onsets);
    REQUIRE(bassInFirstCycle >= onsets);
    REQUIRE(bassOnGrid >= onsets);

    REQUIRE(maxLockBar > barAtLock);
    REQUIRE(sawTransition);
    REQUIRE(proc.getSectionPhase() == 3);

    proc.setPlayHead(nullptr);
    proc.releaseResources();
}

TEST_CASE("T2.2 seek mid-lock preserves remaining duration and stays on the host grid",
          "[integration][pipeline][phase2][t2.2][clock]")
{
    const double samplesPerBeat = 60.0 / kBpm * kSr;
    const int64_t samplesPerBar = static_cast<int64_t>(4.0 * samplesPerBeat);

    AccompanimentProcessor proc;
    proc.prepareToPlay(kSr, kBlock);
    proc.pauseBackgroundInferenceForTests();
    applyLockSession(proc, 4.0f, 4.0f);

    TransportPlayHead ph;
    proc.setPlayHead(&ph);

    int blockIdx = 0;
    recordChugRiff(proc, ph, blockIdx, 65.406);
    REQUIRE(proc.isGrooveLocked());

    const int barsToFeed = 1;
    const int holdBlocks = static_cast<int>(static_cast<double>(barsToFeed) * samplesPerBar / kBlock);
    for (int b = 0; b < holdBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        fillChugBlock(buf, blockIdx, kBlock, 65.406);
        juce::MidiBuffer midi;
        ph.samples = static_cast<int64_t>(blockIdx) * kBlock;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.isGrooveLocked());
    REQUIRE_FALSE(proc.isTransitionSectionActive());

    const int64_t seekPos = static_cast<int64_t>(std::llround(1.7 * samplesPerBeat));
    ph.samples = seekPos;
    int64_t hostPos = seekPos;
    std::vector<NoteOn> afterSeek;
    const int remainingBlocks = static_cast<int>(4.0 * samplesPerBar / kBlock);
    bool sawTransition = false;

    for (int b = 0; b < remainingBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, kBlock);
        fillChugBlock(buf, blockIdx, kBlock, 65.406);
        juce::MidiBuffer midi;
        ph.samples = hostPos;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        collectNoteOns(midi, hostPos, afterSeek);
        if (proc.isTransitionSectionActive())
        {
            sawTransition = true;
            break;
        }
        hostPos += kBlock;
        ++blockIdx;
    }

    REQUIRE(sawTransition);

    int kicksOnGrid = 0;
    int kicks = 0;
    const int64_t gridTol = static_cast<int64_t>(0.020 * kSr);
    for (const auto& n : afterSeek)
    {
        if (!(n.channel == 10 && n.note == 36))
            continue;
        ++kicks;
        const int64_t intoBar = n.sample % samplesPerBar;
        const int64_t toEnd = samplesPerBar - intoBar;
        if (intoBar <= gridTol || toEnd <= gridTol)
            ++kicksOnGrid;
    }
    REQUIRE(kicks >= 1);
    REQUIRE(kicksOnGrid >= 1);

    proc.setPlayHead(nullptr);
    proc.releaseResources();
}
