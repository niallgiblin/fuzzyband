/**
 * Integration tests for AccompanimentProcessor — full plugin pipeline.
 *
 * Uses the test-only control hooks (pause / flush / resume background inference)
 * to exercise the processor deterministically without real-time threading.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"
#include "inference/pattern_rules.h"
#include "midi/GrooveTemplate.h"

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace {

// Build a 2-channel (stereo) buffer filled with silence.
juce::AudioBuffer<float> makeSilenceBuffer(int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    buf.clear();
    return buf;
}

// Build a 2-channel buffer containing a sine wave at the given frequency and amplitude.
juce::AudioBuffer<float> makeSineBuffer(int numSamples, double freq, double sr, float amplitude)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* ptr = buf.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i)
            ptr[i] = amplitude * static_cast<float>(
                         std::sin(2.0 * M_PI * freq * i / sr));
    }
    return buf;
}

// Build a click-train buffer: unit impulse every `period` samples on both channels.
juce::AudioBuffer<float> makeClickBuffer(int numSamples, int period)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    buf.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        float* ptr = buf.getWritePointer(ch);
        for (int i = 0; i < numSamples; i += period)
            ptr[i] = 1.0f;
    }
    return buf;
}

// Feed numBlocks × blockSize samples of `audio` (wrapped circularly) through the processor.
void feedBlocks(AccompanimentProcessor& proc,
                const juce::AudioBuffer<float>& source,
                int blockSize,
                int numBlocks)
{
    juce::MidiBuffer midi;
    const int totalSrc = source.getNumSamples();
    int64_t pos = 0;
    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> block(2, blockSize);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const int srcIdx = static_cast<int>((pos + i) % totalSrc);
                block.setSample(ch, i, source.getSample(ch, srcIdx));
            }
        }
        proc.processBlock(block, midi);
        pos += blockSize;
    }
}

static void feedQuietBlocks(AccompanimentProcessor& proc, double sr, int block, int n)
{
    auto quiet = makeSineBuffer(block, 110.0, sr, 0.0005f);
    for (int i = 0; i < n; ++i)
    {
        juce::MidiBuffer midi;
        proc.processBlock(quiet, midi);
    }
}

static int blocksForBars(double sr, int block, double bars, double bpm = 120.0)
{
    return static_cast<int>(std::ceil(bars * 4.0 * 60.0 / bpm * sr / static_cast<double>(block)));
}

} // namespace

// ─── Silent pipeline ─────────────────────────────────────────────────────────

#if defined(MA_ENABLE_ONNX)
TEST_CASE("Processor pipeline: ONNX build loads MetalGrooveInference, not the rule-based fallback",
          "[integration][pipeline][onnx]")
{
    // T0.3: tryLoadModel() used to fail silently and the processor ran
    // RuleBasedInference under an ONNX-enabled binary. Fail loudly.
    AccompanimentProcessor proc;
    REQUIRE(proc.getActiveInferenceName() == "MetalGrooveInference");
    REQUIRE(proc.getOnnxErrorCount() == 0);
    proc.releaseResources();
}
#endif

TEST_CASE("Processor pipeline: silence produces pattern 0 with no note-on MIDI", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const int blockSize = 512;
    const int numBlocks = 20;

    juce::MidiBuffer accMidi;
    auto silence = makeSilenceBuffer(blockSize);

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> block = makeSilenceBuffer(blockSize);
        juce::MidiBuffer midi;
        proc.processBlock(block, midi);
        accMidi.addEvents(midi, 0, -1, 0);
    }

    proc.flushBackgroundInferenceForTests();

    // No note-on events should have been generated for pure silence
    bool anyNoteOn = false;
    for (const auto meta : accMidi)
        if (meta.getMessage().isNoteOn())
            anyNoteOn = true;
    REQUIRE_FALSE(anyNoteOn);

    // Pattern index should remain at 0 (SILENT state → base 0 → policy with genre 0 → 0)
    REQUIRE(proc.getDisplayPatternIndex() == 0);

    proc.releaseResources();
}

// ─── Active signal pipeline ───────────────────────────────────────────────────

TEST_CASE("Processor pipeline: sustained signal raises RMS and changes display state", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const double sr       = 48000.0;
    const int    block    = 512;
    const int    numBlocks = 300; // ~3.2 s of audio — enough for StructureTagger to leave SILENT

    // 1500 Hz sine at amplitude 0.5: rmsEnergy clamps to 1.0 (above kLoudRmsFloor 0.30) → LOUD
    auto sigSrc = makeSineBuffer(block, 1500.0, sr, 0.5f);
    feedBlocks(proc, sigSrc, block, numBlocks);

    proc.flushBackgroundInferenceForTests();

    // After enough signal, the displayed RMS must exceed the silent threshold
    REQUIRE(proc.getDisplayRms() > 0.05f);

    // State should have advanced beyond SILENT (index 0 = SILENT)
    REQUIRE(proc.getDisplayStateIndex() != 0);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: clean DI phrase gap does not immediately return to SILENT", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    const double sr = 48000.0;
    const int block = 512;

    // Low-level clean DI: raw sine amp 0.012 becomes scaled RMS around 0.034.
    auto cleanDi = makeSineBuffer(block, 110.0, sr, 0.012f);
    feedBlocks(proc, cleanDi, block, static_cast<int>(1.0 * sr) / block);
    proc.flushBackgroundInferenceForTests();
    REQUIRE(proc.getDisplayStateIndex() != static_cast<int>(StructureState::SILENT));

    auto silence = makeSilenceBuffer(block);
    feedBlocks(proc, silence, block, static_cast<int>(0.75 * sr) / block);
    proc.flushBackgroundInferenceForTests();

    REQUIRE(proc.getDisplayStateIndex() != static_cast<int>(StructureState::SILENT));

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: active signal opens accompaniment gate", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();
    // Arm the engine (Play) — it only starts producing once armed.
    proc.playActive.store(true, std::memory_order_release);

    const double sr = 48000.0;
    const int block = 512;
    auto activeSignal = makeSineBuffer(block, 1500.0, sr, 0.5f);

    feedBlocks(proc, activeSignal, block, static_cast<int>(3.0 * sr) / block);
    proc.flushBackgroundInferenceForTests();

    bool sawNoteOn = false;
    for (int i = 0; i < static_cast<int>(4.0 * sr) / block; ++i)
    {
        juce::AudioBuffer<float> audio = activeSignal;
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
        for (const auto meta : midi)
        {
            if (meta.getMessage().isNoteOn())
                sawNoteOn = true;
        }
    }

    REQUIRE(sawNoteOn);

    proc.releaseResources();
}

// ─── State serialization round-trip ─────────────────────────────────────────

TEST_CASE("Processor pipeline: getStateInformation / setStateInformation round-trip", "[integration][pipeline]")
{
    AccompanimentProcessor procA;
    procA.prepareToPlay(48000.0, 512);

    // Set non-default parameter values via AudioProcessorParameter::setValue.
    if (auto* p = procA.getApvts().getParameter("intensity"))
        p->setValue(0.8f);

    juce::MemoryBlock data;
    procA.getStateInformation(data);
    REQUIRE(data.getSize() > 0);

    AccompanimentProcessor procB;
    procB.prepareToPlay(48000.0, 512);
    procB.setStateInformation(data.getData(), static_cast<int>(data.getSize()));

    // Continuous parameters should be within floating-point tolerance
    if (auto* inten = dynamic_cast<juce::AudioParameterFloat*>(
            procB.getApvts().getParameter("intensity")))
        REQUIRE(std::abs(inten->get() - 0.8f) < 0.01f);

    procA.releaseResources();
    procB.releaseResources();
}

// ─── Thread pause / resume safety ────────────────────────────────────────────

TEST_CASE("Processor pipeline: pause/flush/resume cycle does not corrupt state", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    // Pause background thread
    proc.pauseBackgroundInferenceForTests();

    // Feed 50 blocks of silence with background paused
    auto silence = makeSilenceBuffer(512);
    for (int b = 0; b < 50; ++b)
    {
        juce::AudioBuffer<float> block = makeSilenceBuffer(512);
        juce::MidiBuffer midi;
        proc.processBlock(block, midi);
    }

    // Synchronously drain the feature queue
    proc.flushBackgroundInferenceForTests();

    // Resume thread
    proc.resumeBackgroundInferenceForTests();

    // Processor must still be in a sane state — pattern index in [0, 6]
    const int patIdx = proc.getDisplayPatternIndex();
    REQUIRE(patIdx >= 0);
    REQUIRE(patIdx <= 6);

    // BPM must be in valid range
    const float bpm = proc.getDisplayBpm();
    REQUIRE(bpm >= 40.0f);
    REQUIRE(bpm <= 300.0f);

    proc.releaseResources();
}

// ─── BPM display ─────────────────────────────────────────────────────────────

TEST_CASE("Processor pipeline: BPM display stays in [40, 300] at all times", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    // Feed a click-train at 120 BPM (period = 24000 samples at 48 kHz)
    const int period = 24000;
    auto clicks = makeClickBuffer(period * 12, period); // 12 clicks
    feedBlocks(proc, clicks, 512, (period * 12) / 512);

    proc.flushBackgroundInferenceForTests();

    const float bpm = proc.getDisplayBpm();
    REQUIRE(bpm >= 40.0f);
    REQUIRE(bpm <= 300.0f);

    proc.releaseResources();
}

// ─── Rejection signal ───────────────────────────────────────────────────────

TEST_CASE("Processor rejection changes displayPatternIndex", "[integration][pipeline]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    // Feed an active signal to establish a non-silent pattern. SILENT has only pattern 0
    // as a compatible class, so rejection cannot cycle to another pattern there.
    feedBlocks(proc, makeSineBuffer(512, 1500.0, 48000.0, 0.5f), 512, 300);
    proc.flushBackgroundInferenceForTests();
    const int idxBefore = proc.getLatestPatternIndex();
    REQUIRE(idxBefore > 0);

    // Feed one more block so the queue has a fresh feature for the next flush
    feedBlocks(proc, makeSineBuffer(512, 1500.0, 48000.0, 0.5f), 512, 1);

    // Trigger rejection — should exclude the current pattern for one cycle
    proc.patternRejectionCount.store(1, std::memory_order_release);

    proc.flushBackgroundInferenceForTests();
    const int idxAfter = proc.getLatestPatternIndex();

    REQUIRE(idxAfter != idxBefore);
    REQUIRE(idxAfter >= 1);
    REQUIRE(idxAfter <= 10);

    // Rejection should be consumed (single-shot)
    const int remainingRejection = proc.patternRejectionCount.load(std::memory_order_acquire);
    REQUIRE(remainingRejection == 0);

    proc.releaseResources();
}

// ─── Session backward-compat: pre-v0.4.0 XML with genre attribute ─────────────

TEST_CASE("Session XML round-trip: pre-v0.4.0 genre attribute ignored", "[integration][pipeline]")
{
    // Step 1: Get real serialized state as a ValueTree, then inject pre-v0.4.0 params.
    AccompanimentProcessor procSrc;
    procSrc.prepareToPlay(48000.0, 512);
    if (auto* p = procSrc.getApvts().getParameter("intensity"))
        p->setValue(0.6f);
    if (auto* bpmFloat = dynamic_cast<juce::AudioParameterFloat*>(
            procSrc.getApvts().getParameter("bpm")))
        *bpmFloat = 140.0f;

    // Force APVTS state tree to reflect setValue before serialising
    procSrc.getApvts().state.sendPropertyChangeMessage("intensity");
    procSrc.getApvts().state.sendPropertyChangeMessage("bpm");

    juce::MemoryBlock baseData;
    procSrc.getStateInformation(baseData);
    REQUIRE(baseData.getSize() > 0);

    // Parse the baseline ValueTree and inject pre-v0.4.0 unknown properties.
    // JUCE APVTS uses ValueTree::readFromData / writeToStream (binary, not XML).
    auto tree = juce::ValueTree::readFromData(baseData.getData(), baseData.getSize());
    REQUIRE(tree.isValid());

    // Inject pre-v0.4.0 properties that no longer exist in the APVTS.
    // JUCE silently ignores unknown ValueTree properties on deserialization.
    tree.setProperty("genre", "0.75", nullptr);
    tree.setProperty("variation", "0.2", nullptr);
    tree.setProperty("structureBlend", "0.5", nullptr);
    tree.setProperty("generativeBassMode", "0", nullptr);

    // Step 2: Serialise the tampered ValueTree back to a MemoryBlock.
    juce::MemoryBlock tamperedData;
    juce::MemoryOutputStream tamperedStream(tamperedData, false);
    tree.writeToStream(tamperedStream);

    // Step 3: Load tampered state into a fresh processor — must not crash.
    AccompanimentProcessor procDst;
    procDst.prepareToPlay(48000.0, 512);
    procDst.setStateInformation(tamperedData.getData(), static_cast<int>(tamperedData.getSize()));

    // Step 4: Surviving parameters must load correctly.
    if (auto* inten = dynamic_cast<juce::AudioParameterFloat*>(
            procDst.getApvts().getParameter("intensity")))
        REQUIRE(std::abs(inten->get() - 0.6f) < 0.01f);

    if (auto* bpmParam = dynamic_cast<juce::AudioParameterFloat*>(
            procDst.getApvts().getParameter("bpm")))
        REQUIRE(std::abs(bpmParam->get() - 140.0f) < 1.0f);

    // Step 5: Plugin must be in a consistent state (pattern index in valid range).
    REQUIRE(procDst.getDisplayPatternIndex() >= 0);
    REQUIRE(procDst.getDisplayPatternIndex() <= 6);

    procSrc.releaseResources();
    procDst.releaseResources();
}




TEST_CASE("Processor pipeline: hot-input noise floor reads SILENT with no bass", "[integration][pipeline][silent]")
{
    // Regression for the user's session: guitar volume up, not playing → RMS
    // ~0.048. The adaptive noise floor must learn this and hold SILENT, with
    // no bass (engine suppressed on pattern 0, phrase learner gated on SILENT).
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    const double totalSec = 15.0;          // enough for the floor to converge (~8s creep)
    const double quietSec = 3.0;           // last 3 s must be fully quiet
    const int n = static_cast<int>(totalSec * sr);
    int bassOnsLastQuiet = 0;

    for (int start = 0; start + block <= n; start += block)
    {
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
                p[i] = 0.017f * static_cast<float>(std::sin(2.0 * M_PI * 110.0 * (start + i) / sr));
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();

        const double t = static_cast<double>(start) / sr;
        if (t >= totalSec - quietSec)
        {
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                    ++bassOnsLastQuiet;
            }
        }
    }

    REQUIRE(proc.getDisplayStateIndex() == static_cast<int>(StructureState::SILENT));
    REQUIRE(bassOnsLastQuiet == 0);
    proc.releaseResources();
}


TEST_CASE("Processor pipeline: bass octave +12 shifts the bass up an octave", "[integration][pipeline]")
{
    // C2 guitar input (65.4 Hz) → bass root C2 = MIDI 36. With "+12 (up)" the
    // bass must play around C3 (48), not C2 (36). Also verifies the normalized
    // → choice-index mapping (round(normalized × (n-1))).
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    proc.playActive.store(true, std::memory_order_release);

    if (auto* p = proc.getApvts().getParameter("bassTranspose"))
        p->setValueNotifyingHost(1.0f);  // normalized 1.0 → choice index 2 = "+12 (up)"

    std::set<int> bassNotes;
    const int n = static_cast<int>(4.0 * sr);
    for (int start = 0; start + block <= n; start += block)
    {
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
            {
                const double t = static_cast<double>(start + i) / sr;
                p[i] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * 65.406 * t)
                                        + 0.06 * std::sin(2.0 * M_PI * 130.812 * t));
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
                bassNotes.insert(msg.getNoteNumber());
        }
    }

    // Transposed up: root C3 = 48 (plus fifth/octave harmonies 53/55). Nothing below 46.
    REQUIRE(bassNotes.count(48) > 0);
    for (int note : bassNotes)
        REQUIRE(note >= 46);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: genre mapping reaches later presets (Sludge)", "[integration][pipeline]")
{
    // Regression: round(normalized) collapsed multi-choice combos, so Punk/Metal/
    // Sludge were unreachable. Sludge (metal family) must route SOFT low-energy
    // into the metal/shared vocabulary (patterns 1-21), never the rock set
    // (22-27). Select Sludge by index (the choice value is normalised by list
    // length, so a hardcoded 1.0f now means the last genre, Grunge, not Sludge).
    // Style steering is now live, so the exact pattern may rotate within the
    // metal pool — the invariant is "metal, not rock".
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* genreParam = proc.getApvts().getParameter("genre"))
    {
        // Select Sludge by NAME: the choice value is normalised by list length,
        // so a hardcoded 1.0f now means the last genre (Grunge), not Sludge.
        int sludgeIndex = 0;
        for (int i = 0; i < Groove::presetCount(); ++i)
            if (juce::String(Groove::presetFor(i).name) == "Sludge") { sludgeIndex = i; break; }
        const int denom = juce::jmax(1, Groove::presetCount() - 1);
        genreParam->setValueNotifyingHost(static_cast<float>(sludgeIndex) / static_cast<float>(denom));
    }

    const int n = static_cast<int>(3.0 * sr);
    for (int start = 0; start + block <= n; start += block)
    {
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
                p[i] = 0.013f * static_cast<float>(std::sin(2.0 * M_PI * 400.0 * (start + i) / sr));
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
    }

    const int pat = proc.getLatestPatternIndex();
    // Sludge → metal routing: SOFT low-energy stays in the metal/shared set.
    REQUIRE(pat >= 1);
    REQUIRE(pat < 22);
    proc.releaseResources();
}

struct BassHit
{
    int midiNote = 36;
    double beatInLoop = 0.0;
};

struct BassHitCollector
{
    std::vector<BassHit> hits;
    int64_t originSample = -1;
};

static void appendBassHits(const juce::MidiBuffer& midi, int blockIdx, int block,
                           int64_t originSample, double samplesPerBeat,
                           std::vector<BassHit>& out)
{
    if (originSample < 0 || samplesPerBeat <= 0.0)
        return;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (!msg.isNoteOn() || msg.getChannel() != 2 || msg.getVelocity() <= 0)
            continue;
        const int64_t absSample = static_cast<int64_t>(blockIdx) * block + meta.samplePosition;
        double beat = std::fmod(static_cast<double>(absSample - originSample) / samplesPerBeat, 16.0);
        if (beat < 0.0)
            beat += 16.0;
        out.push_back({ msg.getNoteNumber(), beat });
    }
}

static void collectIfLocked(AccompanimentProcessor& proc, const juce::MidiBuffer& midi,
                            int blockIdx, int block, double samplesPerBeat,
                            BassHitCollector& col)
{
    if (col.originSample < 0 && proc.isGrooveLocked())
    {
        // Measure from the processor's bar-locked riff origin, not from the block
        // that detected the lock: the detecting block can straddle the bar line, so
        // the two differ by up to one block and every slot index would shift.
        const int64_t riffOrigin = proc.getRiffAPlayOriginSample();
        col.originSample = (riffOrigin >= 0) ? riffOrigin
                                             : static_cast<int64_t>(blockIdx) * block;
    }
    appendBassHits(midi, blockIdx, block, col.originSample, samplesPerBeat, col.hits);
}

static void slotMapsFromHits(const std::vector<BassHit>& hits, bool occupied[64], int midi[64])
{
    for (int i = 0; i < 64; ++i)
    {
        occupied[i] = false;
        midi[i] = -1;
    }
    for (const auto& h : hits)
    {
        int slot = static_cast<int>(std::floor(h.beatInLoop / 0.25)) % 64;
        if (slot < 0)
            slot += 64;
        occupied[slot] = true;
        midi[slot] = h.midiNote;
    }
}

static void fillSineAmp(juce::AudioBuffer<float>& buf, int blockIdx, int block,
                        double sr, double freq, float amp)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* p = buf.getWritePointer(ch);
        const double t0 = static_cast<double>(blockIdx) * block / sr;
        for (int i = 0; i < block; ++i)
        {
            const double tt = t0 + static_cast<double>(i) / sr;
            p[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * freq * tt));
        }
    }
}

static void fillChugBlock(juce::AudioBuffer<float>& buf, int blockIdx, int block,
                          double sr, double freq)
{
    constexpr int cycle = 24;
    const int pos = blockIdx % cycle;
    const bool loud = (pos >= cycle - 4);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* p = buf.getWritePointer(ch);
        const double t = static_cast<double>(blockIdx) * block / sr;
        for (int i = 0; i < block; ++i)
        {
            const double tt = t + static_cast<double>(i) / sr;
            p[i] = static_cast<float>((loud ? 0.5 : 0.08) * std::sin(2.0 * M_PI * freq * tt));
        }
    }
}

static void feedChugCollecting(AccompanimentProcessor& proc, double sr, int block,
                               double freq, int& blockIdx, double samplesPerBeat,
                               BassHitCollector& col, int64_t untilSample)
{
    while (static_cast<int64_t>(blockIdx) * block < untilSample && proc.isGrooveLocked())
    {
        juce::AudioBuffer<float> buf(2, block);
        fillChugBlock(buf, blockIdx, block, sr, freq);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        collectIfLocked(proc, midi, blockIdx, block, samplesPerBeat, col);
        ++blockIdx;
    }
}

// Arm a deterministic riff lock by Record riff: count-in 1 bar + record 4 bars
// of the given chug `freq`, which commits a grid take and engages the groove
// lock (playOn is off). Used instead of the removed auto-lock-by-listening,
// since the engine now only listens once armed.
static void recordChugRiff(AccompanimentProcessor& proc, double sr, int block,
                           double freq, int& blockIdx, BassHitCollector* col = nullptr,
                           double samplesPerBeat = 0.0)
{
    proc.requestRiffCaptureStart();
    const int n = static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * sr / block);
    for (int b = 0; b < n; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillChugBlock(buf, blockIdx, block, sr, freq);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        if (col != nullptr)
            collectIfLocked(proc, midi, blockIdx, block, samplesPerBeat, *col);
        ++blockIdx;
    }
}

TEST_CASE("Processor pipeline: groove lock freezes drums, expires into a transition, then returns to the riff", "[integration][pipeline][lock]")
{
    // The groove lock: a repeated riff (phrase learner) auto-freezes the drum
    // pattern while the guitarist expands/solos; the hold expires after
    // `lockBars` bars without the riff, and listening resumes.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    // Short hold: 4 bars (normalized 0.0 over [4,64]) so expiry is testable.
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);
    INFO("lockBars raw after set: " << proc.getApvts().getRawParameterValue("lockBars")->load()
         << " (expect ~0.0)");
    // Short post-lock transition (2 bars = 4s) so it completes within the test.
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(2.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    auto feed = [&](float amp, double freq, int numBlocks, int& blockIdx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };

    // Phase 1: Record a C2 riff (deterministic lock; auto-lock-by-listening is
    // gone — the engine only listens once armed).
    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());
    const int frozenPattern = proc.getLatestPatternIndex();

    // Phase 2a: sustained input (no attacks → not the riff) for ~4s while
    // locked — the committed drum pattern must NOT change, even though the
    // listener would normally re-evaluate.
    feed(0.05, 400.0, static_cast<int>(4.0 * sr / block), blockIdx);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getLatestPatternIndex() == frozenPattern);

    // Phase 2b: keep feeding past the 4-bar hold (8s) → lock expires and a
    // post-lock TRANSITION section engages (A5.2). The transition freezes
    // selection by design, so feed through it (2 bars = 4s).
    feed(0.05, 400.0, static_cast<int>(7.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());
    if (proc.isTransitionSectionActive())
        feed(0.05, 400.0, static_cast<int>(5.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isTransitionSectionActive());

    // Phase 3: the transition has firmly returned to the locked riff (A) — the
    // groove re-locks and the drum pattern is frozen again (A → B → A cycle),
    // so a pattern rejection no longer takes effect while the riff is held.
    REQUIRE(proc.isGrooveLocked());
    const int relockedPattern = proc.getLatestPatternIndex();
    proc.patternRejectionCount.store(1, std::memory_order_release);
    feed(0.05, 400.0, static_cast<int>(0.5 * sr / block), blockIdx);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getLatestPatternIndex() == relockedPattern);

    proc.releaseResources();
}

// ─── A5.2: post-lock transition grammar ───────────────────────────────────────

TEST_CASE("Processor pipeline: post-lock transition section engages, holds, then returns to the riff", "[integration][pipeline][transition]")
{
    // Capture a riff (deterministic lock — the auto-lock heuristic is the user's
    // in-progress work and is not relied on here), let the lock expire, and
    // verify the transition grammar: a contrast section engages, its countdown
    // runs, and after the configured hold it firmly returns to the locked riff.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Short lock (4 bars) and short transition (4 bars), 1 section so the
    // sequence is: lock → B(4 bars) → return to the riff.
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(4.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    auto feed = [&](float amp, double freq, int numBlocks, int& blockIdx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };

    // Phase 1: deterministic capture → lock engages (same path as the
    // record-riff test: count-in 1 bar + record 4 bars).
    proc.requestRiffCaptureStart();
    int blockIdx = 0;
    feed(0.4, 110.0, static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * sr / block), blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // Phase 2: sustained non-riff input past the 4-bar lock hold (8s @120bpm) →
    // lock expires and a transition section engages (not straight back to follow).
    feed(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getTransitionSectionNumber() >= 1);
    REQUIRE(proc.getTransitionBarsTotal() == 4);
    // The transition section must be a valid, non-empty section name.
    // (Family-contrast is verified by the dedicated A-B-A-B / A-B-A-C-A tests;
    // this test's job is to confirm the transition grammar fires correctly.)
    REQUIRE(proc.getTransitionSectionName() != nullptr);
    REQUIRE(std::string(proc.getTransitionSectionName()).size() > 0);

    // Phase 3: feed through the 4-bar transition hold (8s) → it firmly returns
    // to the locked riff (A): the groove re-locks instead of releasing to follow.
    feed(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: two transition sections return to the riff between contrasts (A-B-A-C-A)", "[integration][pipeline][transition]")
{
    // TRANSITION SECTIONS = 2 means A → B → A → C → A, not A → B → C → A.
    // Each contrast holds, then the locked riff re-engages before the next
    // contrast. C must differ from B.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock (8s)
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(2.0f));  // 2 bars (4s)
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(2.0f));

    auto feed = [&](float amp, double freq, int numBlocks, int& blockIdx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // Lock expires → B (must NOT skip A on the way to a second contrast).
    feed(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getTransitionSectionNumber() == 1);
    const std::string sectionB = proc.getTransitionSectionName();
    REQUIRE_FALSE(sectionB.empty());

    // B finishes → back to A (locked riff), not straight into C.
    feed(0.05, 400.0, static_cast<int>(5.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());

    // Next lock expiry → C, a different contrast from B.
    feed(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getTransitionSectionNumber() == 2);
    const std::string sectionC = proc.getTransitionSectionName();
    REQUIRE(sectionC != sectionB);

    // C finishes → back to A again.
    feed(0.05, 400.0, static_cast<int>(5.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());

    // Third lock expiry → wrap: slot 0 reused, so family == sectionB.
    // Lock has ~4s remaining; feed 5s to expire it and land 1s into the
    // 4s transition (short enough that the hold has not yet finished).
    feed(0.05, 400.0, static_cast<int>(5.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getTransitionSectionNumber() == 1);
    REQUIRE(std::string(proc.getTransitionSectionName()) == sectionB);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: one transition section repeats the same contrast (A-B-A-B)", "[integration][pipeline][transition]")
{
    // With TRANSITION SECTIONS = 1, every lock expiry must revisit the same
    // contrast family (B). The form is A-B-A-B, not A-B1-A-B2.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(2.0f));  // 2 bars
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));  // N=1

    auto feed = [&](float amp, double freq, int numBlocks, int& blockIdx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // First lock expiry → B (slot 0 first visit: new family chosen and pinned).
    feed(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getTransitionSectionNumber() == 1);
    const std::string sectionB1 = proc.getTransitionSectionName();
    REQUIRE_FALSE(sectionB1.empty());

    // B finishes → back to A (locked riff).
    feed(0.05, 400.0, static_cast<int>(5.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());

    // Second lock expiry → slot 0 is pinned, so must reuse the same family.
    feed(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getTransitionSectionNumber() == 1);
    REQUIRE(std::string(proc.getTransitionSectionName()) == sectionB1);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: bass returns to the recorded riff after the transition (record mode)", "[integration][pipeline][transition][bass]")
{
    // Regression: in record mode the bass is note-for-note ONLY while the groove
    // is locked on the recorded riff. After a transition (B) finishes, the bass
    // must come back to the recorded riff (note 36 for the C2 chug) — not stay on
    // the transition's section-harmony line and not go silent.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock (8s)
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(2.0f));  // 2 bars (4s)
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    // Feed a quiet non-riff signal, counting bass note-ons (ch 2) and the notes seen.
    auto feedCount = [&](float amp, double freq, int numBlocks, int& blockIdx,
                         int& bassOns, std::set<int>& notes) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                {
                    ++bassOns;
                    notes.insert(msg.getNoteNumber());
                }
            }
            ++blockIdx;
        }
    };

    // Phase 1: record a C2 riff (deterministic lock).
    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // Phase 2: while locked (A), the bass mirrors the recorded C2 riff (note 36).
    int aOns = 0;
    std::set<int> aNotes;
    feedCount(0.05, 400.0, static_cast<int>(1.0 * sr / block), blockIdx, aOns, aNotes);
    REQUIRE(aOns > 0);
    REQUIRE(aNotes.count(36) > 0);

    // Phase 3: expire the lock and play the transition through to its end.
    int tOns = 0;
    std::set<int> tNotes;
    feedCount(0.05, 400.0, static_cast<int>(7.0 * sr / block), blockIdx, tOns, tNotes);
    REQUIRE_FALSE(proc.isGrooveLocked());
    if (proc.isTransitionSectionActive())
        feedCount(0.05, 400.0, static_cast<int>(5.0 * sr / block), blockIdx, tOns, tNotes);
    REQUIRE_FALSE(proc.isTransitionSectionActive());

    // Phase 4: the bass must return to the recorded riff (note 36) — not stay on
    // the transition's harmony note and not drop out.
    REQUIRE(proc.isGrooveLocked());
    int backOns = 0;
    std::set<int> backNotes;
    feedCount(0.05, 400.0, static_cast<int>(1.0 * sr / block), blockIdx, backOns, backNotes);
    REQUIRE(backOns > 0);
    REQUIRE(backNotes.count(36) > 0);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: transition countdown updates live", "[integration][pipeline][transition]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    auto feedN = [&](float amp, double freq, int numBlocks, int& idx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(idx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++idx;
        }
    };

    // Deterministic capture → lock.
    proc.requestRiffCaptureStart();
    int blockIdx = 0;
    feedN(0.4, 110.0, static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * sr / block), blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // Feed past the lock hold (4 bars = 8s) into the transition; countdown
    // starts near the total (8-bar hold).
    feedN(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());
    const int initial = proc.getTransitionBarsRemaining();
    REQUIRE(initial >= 7);  // 8-bar hold, entered near the top of the countdown

    // Feed a couple more seconds — the countdown must have moved down.
    feedN(0.05, 400.0, static_cast<int>(1.0 * sr / block), blockIdx);
    const int later = proc.getTransitionBarsRemaining();
    REQUIRE(later < initial);

    proc.releaseResources();
}


// Feed a 24-block chug cycle (20 quiet + 4 loud) at `freq`, collecting ch.2
// note-ons into `bassNotes`. Advances `blockIdx` and flushes inference per block.
static void feedChugCollectBass(AccompanimentProcessor& proc, double sr, int block,
                                double freq, int numBlocks, int& blockIdx,
                                std::set<int>& bassNotes, bool& anyBass)
{
    constexpr int cycle = 24;
    for (int b = 0; b < numBlocks; ++b)
    {
        const int pos = blockIdx % cycle;
        const bool loud = (pos >= cycle - 4);
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            const double t = static_cast<double>(blockIdx) * block / sr;
            for (int i = 0; i < block; ++i)
            {
                const double tt = t + static_cast<double>(i) / sr;
                p[i] = static_cast<float>((loud ? 0.5 : 0.08) * std::sin(2.0 * M_PI * freq * tt));
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
            {
                bassNotes.insert(msg.getNoteNumber());
                anyBass = true;
            }
        }
        ++blockIdx;
    }
}

TEST_CASE("Processor pipeline: locked bass freezes to the learned note (R1)", "[integration][pipeline][lock]")
{
    // P0/R1: while the groove lock holds, the bass must play the learned riff
    // note-for-note. Lock on C2 (65.406 Hz → bass 36), then chug a *different*
    // root (E2, 82.407 Hz → bass 40); the bass must stay on 36, never 40.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    int blockIdx = 0;
    constexpr int cycle = 24;

    // Phase 1: Record a C2 riff (deterministic lock).
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // Phase 2: keep chugging C2 briefly and collect the frozen note.
    std::set<int> c2Notes, e2Notes;
    bool anyC2 = false, anyE2 = false;
    feedChugCollectBass(proc, sr, block, 65.406, 24, blockIdx, c2Notes, anyC2);
    REQUIRE(anyC2);
    REQUIRE(c2Notes.count(36) > 0);  // learned C2 riff

    // Phase 3: chug E2 while locked — the bass must NOT follow the new root.
    feedChugCollectBass(proc, sr, block, 82.407, 48, blockIdx, e2Notes, anyE2);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(anyE2);
    REQUIRE(e2Notes.count(36) > 0);  // still the frozen C2 note
    REQUIRE(e2Notes.count(40) == 0); // never the live E2 root

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: fixed hold releases even while the riff continues (R2+R4)", "[integration][pipeline][lock]")
{
    // P0/R2: returning to the riff must NOT extend the hold. P0/R4: at the fixed
    // expiry a transition crash fires on ch.10. With a 4-bar hold (~8 s at
    // 120 BPM) and the riff still chugging the whole time, a crash must fire —
    // the old behaviour would have extended the hold indefinitely and never
    // transitioned while the riff kept matching.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4 bars

    int blockIdx = 0;
    constexpr int cycle = 24;

    // Phase 1: Record a C2 riff (deterministic lock).
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // Phase 2: keep chugging the SAME riff for 12 s (> 8 s fixed hold) and watch
    // for a crash (MIDI 49) on ch.10 — the fixed-hold release transition.
    bool sawCrash = false;
    const int numBlocks = static_cast<int>(12.0 * sr / block);
    for (int b = 0; b < numBlocks; ++b)
    {
        const int pos = blockIdx % cycle;
        const bool loud = (pos >= cycle - 4);
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            const double t = static_cast<double>(blockIdx) * block / sr;
            for (int i = 0; i < block; ++i)
            {
                const double tt = t + static_cast<double>(i) / sr;
                p[i] = static_cast<float>((loud ? 0.5 : 0.08) * std::sin(2.0 * M_PI * 65.406 * tt));
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 49)
                sawCrash = true;
        }
        ++blockIdx;
    }
    REQUIRE(sawCrash);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: the live mirror folds the played root C2→36, E2→40, G2→43",
          "[integration][pipeline][bass][mirror]")
{
    // The bass mirror must fold every played root onto the C2–B2 register. This
    // is the mirror's pitch contract; the harmonic fallback's root mapping is
    // covered by the Play-mode authored-bass test in the phase-9 suite.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    proc.playActive.store(true, std::memory_order_release);

    int blockIdx = 0;
    const int maxWait = static_cast<int>(4.0 * sr / block);
    for (int i = 0; i < maxWait && proc.getSectionPhase() != 1; ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.12f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.getSectionPhase() == 1);

    auto collectRoot = [&](double freq)
    {
        std::set<int> notes;
        // A plucked cycle (loud burst + decay) gives the onset detector real
        // decay→rise edges; a steady tone has no attacks and would not mirror.
        const int n = static_cast<int>(4.0 * sr / block);
        for (int b = 0; b < n; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            fillChugBlock(buf, blockIdx, block, sr, freq);
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                    notes.insert(msg.getNoteNumber());
            }
            ++blockIdx;
        }
        return notes;
    };

    auto c2 = collectRoot(65.406);
    REQUIRE(c2.count(36) > 0);

    auto e2 = collectRoot(82.407);
    REQUIRE(e2.count(40) > 0);

    auto g2 = collectRoot(98.0);
    REQUIRE(g2.count(43) > 0);

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: RiffBListen mirrors the player (no grid, no holes)",
          "[integration][pipeline][bass][blisten][mirror]")
{
    // Oracle 2026-09-09: B/C had digital-silence holes at 26/29/32 s while the
    // guitar was loud. Listen mixer = attack mirror then beats 1/3 root grid.
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    const int maxWait = static_cast<int>(20.0 * sr / block);
    for (int i = 0; i < maxWait
         && !(proc.isTransitionSectionActive() && proc.getSectionPhase() == 3); ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        // Non-riff tone so T6.2 cannot cut the contrast short during the wait.
        fillSineAmp(buf, blockIdx, block, sr, 400.0, 0.12f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getSectionPhase() == 3);

    constexpr int kBars = 4;
    std::vector<int64_t> bassAbs;
    std::set<int> bassNotes;
    const int gridAtStart = proc.getGridBassNoteCount();
    const int learnedAtStart = proc.getLearnedBassNoteCount();
    const int collectBlocks = static_cast<int>(
        std::ceil(static_cast<double>(kBars) * 4.0 * samplesPerBeat / block));
    for (int b = 0; b < collectBlocks; ++b)
    {
        const int64_t abs0 = static_cast<int64_t>(blockIdx) * block;
        juce::AudioBuffer<float> buf(2, block);
        // Plucked 400 Hz phrases: audibly *playing*, but a different pitch and
        // rhythm from the frozen C2 riff.
        fillChugBlock(buf, blockIdx, block, sr, 400.0);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 2 || msg.getVelocity() <= 0)
                continue;
            bassAbs.push_back(abs0 + meta.samplePosition);
            bassNotes.insert(msg.getNoteNumber());
        }
        ++blockIdx;
    }

    INFO("BListen nHits=" << bassAbs.size()
         << " learned+" << (proc.getLearnedBassNoteCount() - learnedAtStart)
         << " grid+" << (proc.getGridBassNoteCount() - gridAtStart));

    // No digital-silence holes: while the guitarist is audible the bass sounds.
    REQUIRE(bassAbs.size() >= 4);
    // The mirror is the source, not the authored/harmonic grid.
    REQUIRE(proc.getLearnedBassNoteCount() > learnedAtStart);
    REQUIRE(proc.getGridBassNoteCount() == gridAtStart);
    // And it is following the player, not looping the frozen C2 riff.
    REQUIRE(bassNotes.count(36) == 0);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: custom song form loads and persists (Phase 2)", "[integration][pipeline][structure]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // Set a custom form and feed one block so processBlock loads it.
    proc.setCustomSongForm("VERSE:4,CHORUS:4,BREAKDOWN:2,OUTRO:2");
    {
        auto buf = makeSilenceBuffer(block);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
    }
    REQUIRE(proc.getSectionName().startsWith("VERSE"));
    REQUIRE(proc.getSectionName().contains("/4"));

    // Persist, then restore into a fresh processor and verify the form survives.
    juce::MemoryBlock data;
    proc.getStateInformation(data);
    REQUIRE(data.getSize() > 0);

    AccompanimentProcessor procB;
    procB.prepareToPlay(sr, block);
    procB.pauseBackgroundInferenceForTests();
    procB.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    {
        auto buf = makeSilenceBuffer(block);
        juce::MidiBuffer midi;
        procB.processBlock(buf, midi);
    }
    REQUIRE(procB.getSectionName().startsWith("VERSE"));
    REQUIRE(procB.getSectionName().contains("/4"));

    proc.releaseResources();
    procB.releaseResources();
}

TEST_CASE("Processor pipeline: record riff auto-locks a 4-bar metronome take", "[integration][pipeline][capture]")
{
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();
    proc.requestRiffCaptureStart();

    auto sig = makeSineBuffer(512, 110.0, 48000.0, 0.4f);
    // Count-in (1 bar) + record (4 bars) + a little headroom.
    const int blocks = static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * 48000.0 / 512.0);
    feedBlocks(proc, sig, 512, blocks);

    REQUIRE_FALSE(proc.isRiffCapturing());
    REQUIRE(proc.isGrooveLocked());

    // The lock hold progress is published for the UI: total = lockBars
    // (default 16), current bar starts at 1, remaining = total - current.
    const int tot = proc.getLockBarsTotal();
    const int cur = proc.getLockBarCurrent();
    const int rem = proc.getLockBarsRemaining();
    REQUIRE(tot == 16);
    REQUIRE(cur >= 1);
    REQUIRE(cur <= tot);
    REQUIRE(rem == tot - cur);

    // Advance ~2 bars into the hold and verify the progress moved.
    const int barsToAdvance = static_cast<int>(2.0 * 4.0 * 60.0 / 120.0 * 48000.0 / 512.0);
    feedBlocks(proc, sig, 512, barsToAdvance);
    REQUIRE(proc.getLockBarCurrent() >= 2);
    REQUIRE(proc.getLockBarCurrent() <= tot);
    REQUIRE(proc.getLockBarsRemaining() == tot - proc.getLockBarCurrent());

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: idle does not auto-lock; arming via Record riff locks", "[integration][pipeline][capture]")
{
    // Item: "only starts listening at Record riff / Play" — the engine is idle
    // (silent, not learning) until armed. Feeding a riff while idle must NOT
    // auto-lock; only an explicit Record riff (or Play) arms it.
    AccompanimentProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    proc.pauseBackgroundInferenceForTests();

    auto chug = [&](int totalBlocks, int& idx)
    {
        constexpr int cycle = 24;
        for (int b = 0; b < totalBlocks; ++b)
        {
            const int pos = idx % cycle;
            const bool loud = (pos >= cycle - 4);
            juce::AudioBuffer<float> buf(2, 512);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(idx) * 512.0 / 48000.0;
                for (int i = 0; i < 512; ++i)
                {
                    const double tt = t + static_cast<double>(i) / 48000.0;
                    p[i] = static_cast<float>((loud ? 0.5 : 0.08)
                                              * std::sin(2.0 * M_PI * 65.406 * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++idx;
        }
    };

    // Phase 1: chug for several bars in IDLE — the plugin must stay quiet and
    // not auto-lock (no always-listening).
    int blockIdx = 0;
    chug(static_cast<int>(4.5 * 4.0 * 60.0 / 120.0 * 48000.0 / 512.0), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE_FALSE(proc.hasLearnedRiff());

    // Phase 2: arm by Record riff → the same chug captures and locks. Feed a
    // little extra (the count-in waits for the next bar boundary after the idle
    // chug already advanced the host time).
    proc.requestRiffCaptureStart();
    chug(static_cast<int>(6.5 * 4.0 * 60.0 / 120.0 * 48000.0 / 512.0), blockIdx);
    REQUIRE_FALSE(proc.isRiffCapturing());
    REQUIRE(proc.hasLearnedRiff());
    REQUIRE(proc.isGrooveLocked());

    // Lock hold progress is published for the UI.
    const int tot = proc.getLockBarsTotal();
    const int cur = proc.getLockBarCurrent();
    REQUIRE(tot == 16);
    REQUIRE(cur >= 1);
    REQUIRE(cur <= tot);
    REQUIRE(proc.getLockBarsRemaining() == tot - cur);

    // Forget returns to idle.
    proc.requestRiffForget();
    {
        auto buf = makeSineBuffer(512, 110.0, 48000.0, 0.4f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
    }
    REQUIRE_FALSE(proc.hasLearnedRiff());
    REQUIRE_FALSE(proc.isGrooveLocked());

    proc.releaseResources();
}

TEST_CASE("Editor construction smoke test", "[integration][editor]")
{
    juce::MessageManager::getInstance();
    {
        AccompanimentProcessor proc;
        proc.prepareToPlay(48000.0, 512);
        juce::AudioProcessorEditor* ed = proc.createEditor();
        REQUIRE(ed != nullptr);
        // The default height is clamped to the display (see
        // fitEditorToScreen), so only the resize floor is guaranteed here.
        REQUIRE(ed->getHeight() >= 460);
        REQUIRE(ed->getWidth() >= 520);
        delete ed;
        proc.releaseResources();
    }
    juce::MessageManager::deleteInstance();
}

TEST_CASE("Editor panel fits a laptop-height window without clipping", "[integration][editor]")
{
    juce::MessageManager::getInstance();
    {
        AccompanimentProcessor proc;
        proc.prepareToPlay(48000.0, 512);
        // Pin the song form so the section list's height is deterministic.
        proc.setCustomSongForm("INTRO:4,VERSE:8,CHORUS:8,VERSE:8,CHORUS:8,OUTRO:4");

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        REQUIRE(ed != nullptr);

        // The size we open at must respect the limits we advertise to the host,
        // otherwise a host that clamps a restored window to those limits would
        // disagree with the size we started at.
        REQUIRE(ed->getConstrainer() != nullptr);
        CHECK(ed->getHeight() <= ed->getConstrainer()->getMaximumHeight());
        CHECK(ed->getHeight() >= ed->getConstrainer()->getMinimumHeight());
        CHECK(ed->getWidth()  <= ed->getConstrainer()->getMaximumWidth());
        CHECK(ed->getWidth()  >= ed->getConstrainer()->getMinimumWidth());

        // The panel is a single scrolling viewport; find it.
        juce::Viewport* panel = nullptr;
        for (auto* c : ed->getChildren())
            if (auto* v = dynamic_cast<juce::Viewport*>(c))
                panel = v;
        REQUIRE(panel != nullptr);
        auto* viewed = panel->getViewedComponent();
        REQUIRE(viewed != nullptr);

        // ~800px is what a 13" laptop leaves a DAW with the dock visible. At
        // this size the whole panel must fit: when the viewed component is
        // taller than the viewport the bottom of the UI is off-screen, which is
        // exactly the bug this guards.
        ed->setSize(520, 800);
        CHECK(viewed->getHeight() <= panel->getHeight());
        for (auto* c : viewed->getChildren())
            CHECK(c->getBottom() <= viewed->getHeight());

        // Shrinking further must scroll, not drop controls: the panel keeps its
        // content height and the viewport becomes scrollable.
        ed->setSize(520, 460);
        CHECK(viewed->getHeight() > panel->getHeight());
        CHECK(panel->canScrollVertically());
        for (auto* c : viewed->getChildren())
            CHECK(c->getBottom() <= viewed->getHeight());

        proc.releaseResources();
    }
    juce::MessageManager::deleteInstance();
}

TEST_CASE("T8.3 genre change notifies host of swing", "[integration][editor][swing]")
{
    juce::MessageManager::getInstance();
    {
        AccompanimentProcessor proc;
        proc.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        REQUIRE(ed != nullptr);

        auto* swing = proc.getApvts().getParameter("swing");
        REQUIRE(swing != nullptr);

        struct HostTap : juce::AudioProcessorParameter::Listener
        {
            std::atomic<int> n{ 0 };
            void parameterValueChanged(int, float) override { n.fetch_add(1); }
            void parameterGestureChanged(int, bool) override {}
        } tap;
        swing->addListener(&tap);

        juce::ComboBox* genreBox = nullptr;
        std::vector<juce::Component*> stack{ ed.get() };
        while (!stack.empty())
        {
            juce::Component* c = stack.back();
            stack.pop_back();
            if (auto* box = dynamic_cast<juce::ComboBox*>(c))
            {
                if (box->getNumItems() == Groove::presetCount())
                    genreBox = box;
            }
            for (auto* ch : c->getChildren())
                stack.push_back(ch);
        }
        REQUIRE(genreBox != nullptr);

        const float before = swing->getValue();
        genreBox->setSelectedItemIndex(1, juce::sendNotificationSync);  // Hard Rock
        const float expected = swing->convertTo0to1(Groove::presetFor(1).defaultSwing);
        REQUIRE(swing->getValue() == Catch::Approx(expected).margin(1.0e-4f));
        REQUIRE(swing->getValue() != before);
        REQUIRE(tap.n.load() >= 1);

        swing->removeListener(&tap);
        proc.releaseResources();
    }
    juce::MessageManager::deleteInstance();
}

// ─── Play-mode variety: phrased rotation, per-instance seed, energy fills ────
// Implements recommendations 2-4: pool rotation holds each groove for a 2-bar
// phrase (no more per-bar cycling), the rotation is seeded per section INSTANCE
// (the two VERSEs below diverge — seed(VERSE#1)=22 vs seed(VERSE#2)=51), the
// previous groove is never repeated, and the last bar of each section is an
// energy-sized fill (short on quiet input) instead of the always-big fill.
//
// Determinism notes:
// - Play starts at host sample 0 (the first play block also loads the custom
//   form), so the beat grid is aligned to bar boundaries and bar-quantized
//   pattern changes apply without a one-bar lag.
// - Signatures filter transition crashes (note 49) and ghost snares (vel < 50)
//   — the only velocity-variable events — and round each event to the NEAREST
//   16th (the data-derived microtiming, ±9 ms, plus bounded jitter, ±15 ms,
//   can otherwise push a floor-divided boundary event into the previous tick).
// - Verified rotation (rock VERSE pool {22,23,1,2}, barsPerGroove 2):
//   VERSE#1 (seed 22): slots [2, 22, 2] → bars 9-10 hold 1-bar pattern 2,
//   bar 11 = 22, bar 12 = fill. VERSE#2 (seed 51): slots [22, 1, 22] →
//   bar 13 = 22 (≠ VERSE#1's first groove), bar 15 = 1, bar 16 = fill.

// ─── Play hybrid drums: section pool + listen, no hash rotation ──────────────
// Slice 3: Play honors an in-pool pick, snaps outsiders, holds 2+ bars, and
// constrains immediately on section change. Hash rotation is gone.

TEST_CASE("Processor pipeline: play mode phrases grooves, re-seeds per section instance, and fills the last bar", "[integration][pipeline][play][variety]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    proc.setCustomSongForm("INTRO:9,VERSE:4,VERSE:4");
    proc.playActive.store(true, std::memory_order_release);

    auto quiet = makeSineBuffer(block, 110.0, sr, 0.0005f);

    constexpr int64_t kSamplesPerBar = 96000;
    constexpr int kBlocksPerBar = 188;
    constexpr int kTotalBars = 17;

    std::vector<int> barPat(static_cast<size_t>(kTotalBars), -1);
    int64_t blockStartSample = 0;
    constexpr double kSamplesPerBeat = 24000.0;
    auto isTom = [](int note) {
        return note == 41 || note == 43 || note == 45 || note == 47 || note == 48;
    };
    int lastIntroBar = -1;
    int firstVerseBar = -1;
    juce::String lastIntroDetail;
    struct TomHit { int bar; double beatInBar; juce::String name; };
    std::vector<TomHit> toms;

    for (int b = 0; b < kBlocksPerBar * kTotalBars; ++b)
    {
        juce::MidiBuffer midi;
        proc.processBlock(quiet, midi);
        const int bar = static_cast<int>(blockStartSample / kSamplesPerBar);
        if (bar >= 0 && bar < kTotalBars)
            barPat[static_cast<size_t>(bar)] = proc.getPlayedPatternIndex();
        const auto name = proc.getCurrentSectionName();
        if (name == "INTRO" && bar >= 0)
        {
            lastIntroBar = bar;
            lastIntroDetail = proc.getSectionName();
        }
        if (name == "VERSE" && firstVerseBar < 0 && bar >= 0)
            firstVerseBar = bar;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10 || !isTom(msg.getNoteNumber()))
                continue;
            const int64_t abs = blockStartSample + meta.samplePosition;
            const int hitBar = static_cast<int>(abs / kSamplesPerBar);
            double beatInBar = std::fmod(static_cast<double>(abs) / kSamplesPerBeat, 4.0);
            if (beatInBar < 0.0)
                beatInBar += 4.0;
            toms.push_back({ hitBar, beatInBar, name });
        }
        blockStartSample += block;
    }
    proc.playActive.store(false, std::memory_order_release);

    const auto intro = PatternRules::sectionPatternPoolForGenre("INTRO", 0);
    const auto verse = PatternRules::sectionPatternPoolForGenre("VERSE", 0);
    REQUIRE(intro.count > 0);
    REQUIRE(verse.count > 0);

    // Count-in occupies wall-clock bar 0. INTRO holds one pool member (no
    // hash rotation). Skip bar 1 (count-in → form apply).
    REQUIRE(poolContains(intro, barPat[2]));
    for (int bar = 3; bar <= 8; ++bar)
        REQUIRE(poolContains(intro, barPat[static_cast<size_t>(bar)]));

    REQUIRE(lastIntroBar >= 0);
    REQUIRE(firstVerseBar >= 0);
    bool lastIntroFill = false;
    int nBeat4 = 0;
    for (const auto& h : toms)
    {
        if (h.beatInBar >= 3.0 && h.beatInBar < 4.0)
            ++nBeat4;
        const bool onLastIntro = (h.bar == lastIntroBar && h.name == "INTRO")
            || (h.bar == lastIntroBar && firstVerseBar == lastIntroBar);
        if (onLastIntro && h.beatInBar >= 3.0 && h.beatInBar < 4.0)
            lastIntroFill = true;
    }
    INFO("lastIntroBar=" << lastIntroBar << " firstVerseBar=" << firstVerseBar
         << " nToms=" << toms.size() << " nBeat4=" << nBeat4
         << " lastIntroDetail=" << lastIntroDetail);
    REQUIRE(lastIntroFill);
    for (const auto& h : toms)
    {
        if (h.name != "VERSE")
            continue;
        if (h.bar == firstVerseBar)
            REQUIRE_FALSE((h.beatInBar >= 0.0 && h.beatInBar < 0.25));
        if (h.bar == lastIntroBar + 1)
            REQUIRE_FALSE((h.beatInBar >= 3.0 && h.beatInBar < 4.0));
    }

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: Record A/B last-bar fills do not overlay the incoming bar", "[integration][pipeline][fill]")
{
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    constexpr int64_t kSamplesPerBar = 96000;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(2.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    auto isTom = [](int note) {
        return note == 41 || note == 43 || note == 45 || note == 47 || note == 48;
    };

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    struct Hit { int64_t abs; double beatInBar; bool trans; int lockBar; int lockTotal; };
    std::vector<Hit> toms;
    int64_t transOrigin = -1;
    int64_t returnOrigin = -1;
    const int extra = blocksForBars(sr, block, 8.0);
    for (int i = 0; i < extra; ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.08f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        const int64_t blockStart = static_cast<int64_t>(blockIdx) * block;
        const bool trans = proc.isTransitionSectionActive();
        if (trans && transOrigin < 0)
            transOrigin = blockStart;
        if (transOrigin >= 0 && !trans && returnOrigin < 0)
            returnOrigin = blockStart;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10 || !isTom(msg.getNoteNumber()))
                continue;
            const int64_t abs = blockStart + meta.samplePosition;
            double beatInBar = std::fmod(static_cast<double>(abs) / samplesPerBeat, 4.0);
            if (beatInBar < 0.0)
                beatInBar += 4.0;
            toms.push_back({ abs, beatInBar, trans, proc.getLockBarCurrent(), proc.getLockBarsTotal() });
        }
        ++blockIdx;
        if (returnOrigin >= 0 && blockStart - returnOrigin >= kSamplesPerBar)
            break;
    }

    REQUIRE(transOrigin >= 0);
    REQUIRE(returnOrigin > transOrigin);
    INFO("transition bars ≈ " << (static_cast<double>(returnOrigin - transOrigin) / 96000.0));

    bool aLastFill = false;
    bool bLastFill = false;
    for (const auto& h : toms)
    {
        if (!h.trans && h.lockTotal > 0 && h.lockBar == h.lockTotal
            && h.beatInBar >= 2.0 && h.beatInBar < 4.0)
            aLastFill = true;
        const int64_t bLast0 = transOrigin + kSamplesPerBar;
        if (h.abs >= bLast0 && h.abs < bLast0 + kSamplesPerBar
            && h.beatInBar >= 2.0 && h.beatInBar < 4.0)
            bLastFill = true;
        if (h.abs >= transOrigin && h.abs < transOrigin + kSamplesPerBar)
        {
            REQUIRE_FALSE((h.beatInBar >= 0.0 && h.beatInBar < 0.25));
            REQUIRE_FALSE((h.beatInBar >= 3.0 && h.beatInBar < 4.0));
        }
        if (h.abs >= returnOrigin && h.abs < returnOrigin + kSamplesPerBar)
        {
            REQUIRE_FALSE((h.beatInBar >= 0.0 && h.beatInBar < 0.25));
            REQUIRE_FALSE((h.beatInBar >= 3.0 && h.beatInBar < 4.0));
        }
    }
    REQUIRE(aLastFill);
    REQUIRE(bLastFill);

    proc.releaseResources();
}

TEST_CASE("T7.2 Play last-bar fill lands inside the section at 1024/2048 and 120/240 BPM",
          "[integration][pipeline][fill][t7.2]")
{
    const double sr = 48000.0;
    auto isTom = [](int note) {
        return note == 41 || note == 43 || note == 45 || note == 47 || note == 48;
    };

    struct FakePlayHead final : public juce::AudioPlayHead
    {
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
        {
            juce::AudioPlayHead::PositionInfo info;
            info.setBpm(static_cast<double>(bpm));
            info.setIsPlaying(true);
            info.setTimeInSamples(samples);
            return info;
        }
        float bpm = 120.0f;
        int64_t samples = 0;
    };

    const int blocksizes[] = { 1024, 2048 };
    const float bpms[] = { 120.0f, 240.0f };
    for (int block : blocksizes)
    {
        for (float bpm : bpms)
        {
            INFO("block=" << block << " bpm=" << bpm);
            AccompanimentProcessor proc;
            proc.prepareToPlay(sr, block);
            proc.pauseBackgroundInferenceForTests();
            FakePlayHead ph;
            ph.bpm = bpm;
            proc.setPlayHead(&ph);
            proc.setCustomSongForm("INTRO:2,VERSE:2");
            proc.playActive.store(true, std::memory_order_release);

            const double samplesPerBeat = 60.0 / static_cast<double>(bpm) * sr;
            const int64_t samplesPerBar = static_cast<int64_t>(4.0 * samplesPerBeat);
            const int totalBlocks = static_cast<int>((8.0 * samplesPerBar + block - 1) / block);

            int lastIntroBar = -1;
            int firstVerseBar = -1;
            struct TomHit { int bar; double beatInBar; };
            std::vector<TomHit> toms;
            int64_t pos = 0;
            for (int b = 0; b < totalBlocks; ++b)
            {
                ph.samples = pos;
                auto buf = makeSineBuffer(block, 110.0, sr, 0.12f);
                juce::MidiBuffer midi;
                proc.processBlock(buf, midi);
                const int bar = static_cast<int>(pos / samplesPerBar);
                const auto name = proc.getCurrentSectionName();
                if (name == "INTRO" && bar >= 0)
                    lastIntroBar = bar;
                if (name == "VERSE" && firstVerseBar < 0 && bar >= 0)
                    firstVerseBar = bar;
                for (const auto meta : midi)
                {
                    const auto msg = meta.getMessage();
                    if (!msg.isNoteOn() || msg.getChannel() != 10 || !isTom(msg.getNoteNumber()))
                        continue;
                    const int64_t abs = pos + meta.samplePosition;
                    const int hitBar = static_cast<int>(abs / samplesPerBar);
                    double beatInBar = std::fmod(static_cast<double>(abs) / samplesPerBeat, 4.0);
                    if (beatInBar < 0.0)
                        beatInBar += 4.0;
                    toms.push_back({ hitBar, beatInBar });
                }
                pos += block;
            }

            REQUIRE(lastIntroBar >= 0);
            REQUIRE(firstVerseBar >= 0);
            bool lastIntroFill = false;
            for (const auto& h : toms)
            {
                const bool onLastIntro = (h.bar == lastIntroBar)
                    || (firstVerseBar == lastIntroBar && h.bar == lastIntroBar);
                if (onLastIntro && h.beatInBar >= 3.0 && h.beatInBar < 4.0)
                    lastIntroFill = true;
            }
            REQUIRE(lastIntroFill);
            for (const auto& h : toms)
            {
                if (h.bar == firstVerseBar && firstVerseBar != lastIntroBar)
                    REQUIRE_FALSE((h.beatInBar >= 0.0 && h.beatInBar < 0.25));
            }
            proc.playActive.store(false, std::memory_order_release);
            proc.setPlayHead(nullptr);
            proc.releaseResources();
        }
    }
}

TEST_CASE("Processor pipeline: play hybrid drums constrain to pool and hold", "[integration][pipeline][play][hybrid]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    const auto intro = PatternRules::sectionPatternPoolForGenre("INTRO", 0);
    const auto verse = PatternRules::sectionPatternPoolForGenre("VERSE", 0);
    REQUIRE(intro.count >= 2);
    REQUIRE(verse.count >= 2);

    proc.setCustomSongForm("INTRO:8,VERSE:4");
    proc.playActive.store(true, std::memory_order_release);

    feedQuietBlocks(proc, sr, block, blocksForBars(sr, block, 2.25));
    REQUIRE(proc.getSectionPhase() == 1);
    REQUIRE(proc.getCurrentSectionName() == "INTRO");
    REQUIRE(poolContains(intro, proc.getPlayedPatternIndex()));

    proc.injectDrumPatternForTests(8);
    feedQuietBlocks(proc, sr, block, blocksForBars(sr, block, 1.0));
    REQUIRE(proc.getPlayedPatternIndex() != 8);
    REQUIRE(poolContains(intro, proc.getPlayedPatternIndex()));

    int guard = 0;
    while (proc.getCurrentSectionName() != "VERSE" && guard++ < blocksForBars(sr, block, 6.0))
        feedQuietBlocks(proc, sr, block, 1);
    REQUIRE(proc.getCurrentSectionName() == "VERSE");
    REQUIRE(proc.getSectionPhase() == 1);
    feedQuietBlocks(proc, sr, block, blocksForBars(sr, block, 1.0));
    REQUIRE(proc.getPlayedPatternIndex() != 12);
    REQUIRE(poolContains(verse, proc.getPlayedPatternIndex()));

    proc.injectDrumPatternForTests(8);
    feedQuietBlocks(proc, sr, block, blocksForBars(sr, block, 1.0));
    REQUIRE(proc.getPlayedPatternIndex() != 8);
    REQUIRE(poolContains(verse, proc.getPlayedPatternIndex()));

    proc.releaseResources();
}

TEST_CASE("T4.1: Play form rotates the section pool without consecutive repeats", "[integration][pipeline][play][T4.1]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    const auto versePool = PatternRules::orderedSectionPatternPoolForGenre("VERSE", 0);
    const auto chorusPool = PatternRules::orderedSectionPatternPoolForGenre("CHORUS", 0);
    REQUIRE(versePool.count >= 2);
    REQUIRE(chorusPool.count >= 2);

    proc.setCustomSongForm("VERSE:8,CHORUS:8,VERSE:8");
    proc.playActive.store(true, std::memory_order_release);

    const int totalBlocks = blocksForBars(sr, block, 30.0);
    struct Hit { juce::String section; int barInSection; int pattern; };
    std::vector<Hit> hits;
    juce::String prevName;
    int lastSectionBar = -1;

    for (int i = 0; i < totalBlocks; ++i)
    {
        auto quiet = makeSineBuffer(block, 110.0, sr, 0.0005f);
        juce::MidiBuffer midi;
        proc.processBlock(quiet, midi);
        if (proc.getSectionPhase() != 1)
            continue;
        const auto name = proc.getCurrentSectionName();
        const int sb = proc.getSectionBar();
        if (name != prevName)
        {
            prevName = name;
            lastSectionBar = -1;
        }
        if (sb > 0 && sb != lastSectionBar)
        {
            lastSectionBar = sb;
            const int pat = proc.getPlayedPatternIndex();
            if (pat == 17 || pat == 18 || pat == 19 || pat == 0)
                continue;
            // Sample the last bar of each groove slot so PatternPlayer has
            // already applied the bar-quantized rotation pick.
            const int bpg = PatternRules::barsPerGrooveForSection(name.toRawUTF8());
            if (((sb - 1) % juce::jmax(1, bpg)) == juce::jmax(0, bpg - 1))
                hits.push_back({ name, sb - 1, pat });
        }
    }

    auto collectSection = [&](const juce::String& name, const PatternRules::SectionPatternPool& pool)
    {
        std::vector<int> phrases;
        const int bpg = PatternRules::barsPerGrooveForSection(name.toRawUTF8());
        int lastSlot = -1;
        int lastPat = -1;
        int lastBar = -1;
        for (const auto& h : hits)
        {
            if (h.section != name)
                continue;
            REQUIRE(poolContains(pool, h.pattern));
            if (lastBar >= 0 && h.barInSection < lastBar)
            {
                lastSlot = -1;
                lastPat = -1;
            }
            lastBar = h.barInSection;
            const int slot = h.barInSection / juce::jmax(1, bpg);
            if (slot != lastSlot)
            {
                if (lastPat >= 0 && pool.count > 1)
                    REQUIRE(h.pattern != lastPat);
                phrases.push_back(h.pattern);
                lastPat = h.pattern;
                lastSlot = slot;
            }
        }
        return phrases;
    };

    const auto versePhrases = collectSection("VERSE", versePool);
    const auto chorusPhrases = collectSection("CHORUS", chorusPool);
    auto dump = [](const std::vector<int>& v) {
        juce::String s;
        for (int p : v) s += juce::String(p) + " ";
        return s;
    };
    INFO("verse phrases=" << versePhrases.size() << " [" << dump(versePhrases)
         << "] chorus phrases=" << chorusPhrases.size() << " [" << dump(chorusPhrases) << "]");
    REQUIRE(versePhrases.size() >= 2);
    REQUIRE(chorusPhrases.size() >= 1);

    std::set<int> verseUnique(versePhrases.begin(), versePhrases.end());
    std::set<int> chorusUnique(chorusPhrases.begin(), chorusPhrases.end());
    REQUIRE(static_cast<int>(verseUnique.size()) >= juce::jmin(2, versePool.count));
    REQUIRE(static_cast<int>(chorusUnique.size()) >= juce::jmin(2, chorusPool.count));

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("T4.4: idle display pattern is 0; Play tracks the sounding index", "[integration][pipeline][T4.4]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    auto sig = makeSineBuffer(block, 1500.0, sr, 0.5f);
    feedBlocks(proc, sig, block, static_cast<int>(3.0 * sr) / block);
    proc.flushBackgroundInferenceForTests();

    REQUIRE(proc.getDisplayPatternIndex() == 0);
    REQUIRE(proc.getLatestPatternIndex() >= 0);

    proc.setCustomSongForm("VERSE:8");
    proc.playActive.store(true, std::memory_order_release);
    int guard = 0;
    while ((proc.getSectionPhase() != 1 || proc.getCurrentSectionName() != "VERSE"
            || proc.getSectionBar() < 2)
           && guard++ < blocksForBars(sr, block, 6.0))
        feedQuietBlocks(proc, sr, block, 1);
    REQUIRE(proc.getSectionPhase() == 1);
    REQUIRE(proc.getCurrentSectionName() == "VERSE");
    REQUIRE(proc.getDisplayPatternIndex() > 0);
    REQUIRE(proc.getDisplayPatternIndex() == proc.getPlayedPatternIndex());

    proc.playActive.store(false, std::memory_order_release);
    feedQuietBlocks(proc, sr, block, 4);
    REQUIRE(proc.getDisplayPatternIndex() == 0);

    proc.releaseResources();
}

TEST_CASE("T4.3: humanize parameter persists with the session", "[integration][pipeline][T4.3]")
{
    AccompanimentProcessor proc;
    auto* p = dynamic_cast<juce::AudioParameterFloat*>(proc.getApvts().getParameter("humanize"));
    REQUIRE(p != nullptr);
    REQUIRE(std::abs(p->get() - 0.35f) < 0.001f);
    p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    REQUIRE(p->get() < 0.01f);

    juce::MemoryBlock state;
    proc.getStateInformation(state);
    AccompanimentProcessor proc2;
    proc2.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    auto* p2 = dynamic_cast<juce::AudioParameterFloat*>(proc2.getApvts().getParameter("humanize"));
    REQUIRE(p2 != nullptr);
    REQUIRE(p2->get() < 0.01f);
}

TEST_CASE("Processor pipeline: play section does not freeze pattern inference", "[integration][pipeline][play][hybrid]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    proc.setCustomSongForm("INTRO:8");
    proc.playActive.store(true, std::memory_order_release);
    feedQuietBlocks(proc, sr, block, blocksForBars(sr, block, 2.25));
    REQUIRE(proc.getSectionPhase() == 1);
    REQUIRE(proc.getCurrentSectionName() == "INTRO");

    const int before = proc.getInferencePatternSelectCountForTests();
    proc.flushBackgroundInferenceForTests();
    REQUIRE(proc.getInferencePatternSelectCountForTests() > before);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: play mode stops when the song form completes", "[integration][pipeline][play]")
{
    // Play walks the Sections list once and returns to idle. It must not wrap
    // back to INTRO, and it must not fall into generative listen/auto-lock
    // accompaniment if the guitarist keeps playing after OUTRO.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    proc.setCustomSongForm("INTRO:2,OUTRO:2");
    proc.playActive.store(true, std::memory_order_release);

    auto feedChug = [&](int numBlocks)
    {
        int noteOns = 0;
        for (int i = 0; i < numBlocks; ++i)
        {
            // Pulsed low-E so the phrase learner sees attacks during the song
            // (the old bug: that leftover lock re-armed follow mode at OUTRO).
            constexpr int cycle = 24;
            const int pos = i % cycle;
            const bool loud = (pos >= cycle - 4);
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(i) * block / sr;
                for (int s = 0; s < block; ++s)
                {
                    const double tt = t + static_cast<double>(s) / sr;
                    p[s] = static_cast<float>((loud ? 0.5 : 0.08)
                                              * std::sin(2.0 * M_PI * 82.41 * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOn())
                    ++noteOns;
        }
        return noteOns;
    };

    // 4-bar form at 120 BPM = 8s; feed 10s so we are past the end.
    const int n = static_cast<int>(10.0 * sr / block);
    (void)feedChug(n);

    REQUIRE_FALSE(proc.playActive.load(std::memory_order_acquire));
    REQUIRE(proc.getCurrentSectionName() == "Complete");
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE_FALSE(proc.hasLearnedRiff());

    // Keep "playing" after the form — must stay idle (no drums/bass note-ons,
    // no auto-lock). All-notes-off on the first silent block is not a note-on.
    const int after = feedChug(static_cast<int>(4.0 * sr / block));
    REQUIRE(after == 0);
    REQUIRE_FALSE(proc.playActive.load(std::memory_order_acquire));
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE(proc.getCurrentSectionName() == "Complete");

    proc.playActive.store(true, std::memory_order_release);
    {
        juce::AudioBuffer<float> buf(2, block);
        buf.clear();
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
    }
    REQUIRE(proc.playActive.load(std::memory_order_acquire));
    REQUIRE(proc.getCurrentSectionName() == "INTRO");

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

// ── Post-lock transition bass: the bass must LEAVE the old riff ───────────────
TEST_CASE("Processor pipeline: the transition mirrors the player, then falls back to harmony",
          "[integration][pipeline][transition][lock][mirror]")
{
    // After a Record-riff lock expires into a contrast section, the bass must not
    // keep looping the recorded C2 riff. While the guitarist plays it mirrors
    // them; when they stop, the harmony fallback (in their last key) returns.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(16.0f));  // room for the stop tail
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);  // C2 riff → lock
    REQUIRE(proc.isGrooveLocked());

    // Feed sustained tone past the hold → transition engages and holds (a tone
    // has no attacks, so the riff does NOT re-appear and cut it short).
    auto feedTone = [&](float amp, double freq, int numBlocks, int& idx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(idx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++idx;
        }
    };
    feedTone(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());

    // While the guitarist is audibly playing (plucked E2, not the frozen C2),
    // the bass follows them — never the frozen riff note.
    std::set<int> bassNotes;
    bool anyBass = false;
    const int gridAtPlay = proc.getGridBassNoteCount();
    for (int b = 0; b < static_cast<int>(4.0 * sr / block); ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillChugBlock(buf, blockIdx, block, sr, 82.407);   // E2 plucks
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
            {
                bassNotes.insert(msg.getNoteNumber());
                anyBass = true;
            }
        }
        ++blockIdx;
    }
    REQUIRE(anyBass);                          // the bass still sounds
    REQUIRE(bassNotes.count(36) == 0);         // but NOT the frozen riff note C2
    REQUIRE(proc.getGridBassNoteCount() == gridAtPlay);   // no harmony under the player

    // The fallback-after-stop case (and its key memory) is covered in Play mode by
    // "play-mode bass holds a sustain; harmony only after a real stop": here the
    // transition's own auto-lock makes a stop ambiguous.

    proc.releaseResources();
}

// ── Record contrast (B/C/D/E) memory ──────────────────────────────────────────
TEST_CASE("Processor pipeline: a Record contrast riff is learned and replayed on the next visit",
          "[integration][pipeline][transition][lock][memory]")
{
    // User contract (docs/BASS_MIRRORING.md §17): the transition/B section must
    // be remembered like Riff A. First visit to a contrast slot mirrors the
    // player AND captures in parallel; every later visit to the same slot
    // replays the stored riff, so the bass keeps going when the player stops.
    // Before this the contrast only mirrored live, and went silent on a rest.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock, short test
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(4.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));  // always slot 0

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);   // C2 riff -> lock
    REQUIRE(proc.isGrooveLocked());

    auto feed = [&](bool play, double freq, int numBlocks)
    {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            if (play)
            {
                fillChugBlock(buf, blockIdx, block, sr, freq);
            }
            else
            {
                // "Stopped playing" is not digital silence: there is still DI
                // signal (room/amp noise). Digital zeros trip the plugin's
                // digitalSilence gate, which mutes everything by design — a
                // different behaviour from a guitarist resting.
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* p = buf.getWritePointer(ch);
                    const double t = static_cast<double>(blockIdx) * block / sr;
                    for (int i = 0; i < block; ++i)
                        p[i] = static_cast<float>(
                            0.0005 * std::sin(2.0 * M_PI * 110.0 * (t + i / sr)));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };
    auto waitForTransition = [&](bool play, double freq) -> bool
    {
        int guard = 0;
        while (!proc.isTransitionSectionActive() && guard < blocksForBars(sr, block, 40.0))
        {
            feed(play, freq, 1);
            ++guard;
        }
        return proc.isTransitionSectionActive();
    };

    // 1) First contrast visit: play a distinct riff (E2, not the C2 Riff A) for
    //    the whole contrast. It must mirror live, not replay anything frozen.
    REQUIRE(waitForTransition(true, 82.407));
    int frozenFirst = 0;
    {
        int last = proc.getBassProducerCount(BassVoice::Producer::Frozen);
        while (proc.isTransitionSectionActive())
        {
            feed(true, 82.407, 1);
            const int now = proc.getBassProducerCount(BassVoice::Producer::Frozen);
            frozenFirst += now - last;
            last = now;
        }
    }
    REQUIRE(frozenFirst == 0);

    // 2) Let Riff A re-engage, reach the SAME contrast slot again, and play
    //    NOTHING. The stored contrast riff must carry the bass.
    REQUIRE(waitForTransition(false, 0.0));
    // The memory holds the WHOLE contrast, not the few slots captured before
    // the learner auto-locked and truncated the grid listen (measured on a real
    // 8-bar DI: 4 slots -> 64 after the fix). A thin memory is what made the
    // contrast return sound like no bass at all.
    REQUIRE(proc.getTransitionRiffOccupiedCount(0) > 8);
    int frozenSecond = 0;
    {
        int last = proc.getBassProducerCount(BassVoice::Producer::Frozen);
        while (proc.isTransitionSectionActive())
        {
            feed(false, 0.0, 1);
            const int now = proc.getBassProducerCount(BassVoice::Producer::Frozen);
            frozenSecond += now - last;
            last = now;
        }
    }
    INFO("frozenFirst=" << frozenFirst << " frozenSecond=" << frozenSecond);
    REQUIRE(frozenSecond > 0);

    proc.releaseResources();
}

// ── Play-mode bass: reflects the guitarist's root in the song's key ───────────
TEST_CASE("Processor pipeline: play-mode bass mirrors the guitarist, harmony only fills gaps",
          "[integration][pipeline][play][bass][mirror]")
{
    // Mirror-primary contract: while the guitarist is picking, the monophonic
    // bass voice plays the mirrored notes. The authored/harmonic grid line is a
    // gap-filler, so it must NOT layer a root/fifth line underneath the mirror
    // (which made Play sound like root/harmony instead of a mirror).
    const double sr = 48000.0;
    const int block = 512;
    const double bpm = 120.0;
    const double samplesPerBeat = 60.0 / bpm * sr;
    const double eighth = samplesPerBeat / 2.0;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));  // Rock
    proc.setCustomSongForm("VERSE:32");
    proc.playActive.store(true, std::memory_order_release);

    int blockIdx = 0;
    const int maxWait = static_cast<int>(4.0 * sr / block);
    for (int i = 0; i < maxWait && proc.getSectionPhase() != 1; ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.12f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.getSectionPhase() == 1);

    // Arm the decay window before the riff starts.
    for (int i = 0; i < 16; ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.04f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }

    // 12 bars of 8th-note chugs alternating C2 (36) and G2 (43), each with a
    // plucked envelope so the RMS onset detector sees independent attacks.
    // Align the synthetic performance to the plugin's 16th grid. The mirror now
    // snaps to that grid (the learned capture/replay is 16th-quantised by
    // construction), so a riff whose origin is not grid-aligned is a fixture
    // artefact: every note would be shifted by up to half a 16th for no musical
    // reason. A guitarist playing to a click is on the grid.
    const int64_t sixteenthOrigin = static_cast<int64_t>(samplesPerBeat / 4.0);
    const int64_t origin = (static_cast<int64_t>(blockIdx) * block / sixteenthOrigin) * sixteenthOrigin;
    const int kBars = 12;
    const int64_t collectSamples = static_cast<int64_t>(kBars * 4.0 * samplesPerBeat);
    const int collectBlocks = static_cast<int>(std::ceil(
        static_cast<double>(collectSamples) / block));
    std::set<int> bassNotes;
    std::vector<int64_t> bassAbs;
    std::vector<int> bassAbsPitch;
    for (int b = 0; b < collectBlocks; ++b)
    {
        const int64_t abs0 = static_cast<int64_t>(blockIdx) * block;
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
            {
                const int64_t abs = abs0 + i;
                const double relToOrigin = static_cast<double>(abs - origin);
                const int eighthIdx = static_cast<int>(std::floor(relToOrigin / eighth));
                const double freq = (eighthIdx % 2 == 0) ? 65.406 : 98.0;
                const double intoNote = std::fmod(relToOrigin, eighth) / sr;
                const double env = std::exp(-intoNote * 9.0);
                p[i] = static_cast<float>(0.55 * env
                       * std::sin(2.0 * M_PI * freq * static_cast<double>(abs) / sr));
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 2 || msg.getVelocity() <= 0)
                continue;
            bassNotes.insert(msg.getNoteNumber());
            bassAbs.push_back(abs0 + meta.samplePosition);
            bassAbsPitch.push_back(msg.getNoteNumber());
        }
        ++blockIdx;
    }

    const int numAttacks = static_cast<int>(std::floor(collectSamples / eighth));
    // 1) The mirror pitches are present: both notes of the played contour.
    {
        std::string noteList;
        for (int n : bassNotes) noteList += std::to_string(n) + " ";
        INFO("bass notes: " << noteList);
    }
    REQUIRE(bassNotes.count(36) > 0);
    REQUIRE(bassNotes.count(43) > 0);

    // 2) Every guitar attack is mirrored within the documented LEARNING-PHASE
    //    latency budget. The live mirror waits ~40 ms for an onset window long
    //    enough to name a drop-tuned low note (a 16 ms window cannot even reach
    //    D2's lag), so the learning pass is deliberately ~40-56 ms behind the
    //    pick. It costs nothing once a riff is learned: Record capture is silent
    //    and frozen/section replay is placed on the absolute 16th grid with no
    //    pitch analysis at all (pinned by the T2.1 bar-phase and Step-2 exact-
    //    event tests). The old 30 ms bound was only reachable by analysing a
    //    window too short to identify the note — measured on real DIs, that cost
    //    ~45% of the mirrored pitches. See docs/BASS_MIRRORING.md §16.
    const int64_t kLearningMirrorLatency = static_cast<int64_t>(0.060 * sr);
    int mirrored = 0;
    for (int k = 0; k < numAttacks; ++k)
    {
        const int64_t attack = origin + static_cast<int64_t>(k * eighth);
        for (const int64_t hit : bassAbs)
            if (std::llabs(hit - attack) <= kLearningMirrorLatency) { ++mirrored; break; }
    }
    INFO("mirrored " << mirrored << "/" << numAttacks);
    REQUIRE(mirrored >= (numAttacks * 9) / 10);

    // 2b) No same-pitch double-trigger. The legato pitch-follow fires when the
    //     held note's pitch class changes, but a detected PICK is still queued
    //     (its onset-resolved pitch is flushed one onset-window later) — the hops
    //     in between compared the new note against the stale held note, hit the
    //     3-hop debounce and fired the SAME note again ~32 ms after the pick.
    //     On a real DI that produced 61 same-pitch note-ons within 48 ms (vs 6)
    //     and sounded like sloppy playing. Guard: at most a couple are allowed
    //     (a genuine re-pick), never one per pitch change.
    {
        std::vector<std::pair<int64_t, int>> ons;
        for (size_t i = 0; i < bassAbs.size(); ++i)
            ons.emplace_back(bassAbs[i], bassAbsPitch[i]);
        std::sort(ons.begin(), ons.end());
        int dups = 0;
        for (size_t i = 0; i < ons.size(); ++i)
            for (size_t j = i + 1; j < ons.size(); ++j)
            {
                if (ons[j].first - ons[i].first > static_cast<int64_t>(0.048 * sr))
                    break;
                if (ons[j].first > ons[i].first && ons[j].second % 12 == ons[i].second % 12)
                    ++dups;
            }
        INFO("same-pitch double-triggers within 48ms: " << dups);
        REQUIRE(dups <= 2);
    }

    // 3) No harmony layer: the bass is not ~4x denser than the guitar. With the
    //    grid muted under the mirror, one attack produces ~one bass note (plus
    //    occasional gap-fill), not a root/fifth line on every beat.
    INFO("bassOns=" << bassAbs.size() << " attacks=" << numAttacks);
    REQUIRE(bassAbs.size() <= static_cast<size_t>(numAttacks + numAttacks / 3));

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: play-mode bass holds a sustain; harmony only after a real stop",
          "[integration][pipeline][play][bass][mirror]")
{
    // The other half of the contract: the root/harmony line is the fallback, so
    // it must come back in the gaps — before a riff is learned and after the
    // guitarist stops — instead of the bass simply going silent.
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    proc.setCustomSongForm("VERSE:16");
    proc.playActive.store(true, std::memory_order_release);

    int blockIdx = 0;
    const int maxWait = static_cast<int>(4.0 * sr / block);
    for (int i = 0; i < maxWait && proc.getSectionPhase() != 1; ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.12f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.getSectionPhase() == 1);

    // A pick, then hold an audible sustained tone. A sustain is NOT a gap: the
    // mirror must own the bass (and hold its note), and the harmony line must
    // stay silent. The harmony returns only after the guitarist truly stops, and
    // it must keep the key they were playing (E2 → root 40), not default to C.
    constexpr double kPlayedFreq = 82.407;   // E2
    std::set<int> playNotes;
    auto feed = [&](float amp, int numBlocks)
    {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            fillSineAmp(buf, blockIdx, block, sr, kPlayedFreq, amp);
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                    playNotes.insert(msg.getNoteNumber());
            }
            ++blockIdx;
        }
    };

    feed(0.04f, 16);          // arm the decay window
    feed(0.55f, 8);           // the pick (one attack)
    const int gridAtPick = proc.getGridBassNoteCount();
    const int learnedAtPick = proc.getLearnedBassNoteCount();
    REQUIRE(learnedAtPick > 0);   // the pick mirrored
    REQUIRE(playNotes.count(40) > 0);   // ... at the played key (E2)

    // ~3 beats of audible sustain with no further attacks.
    feed(0.12f, static_cast<int>(std::ceil(3.0 * samplesPerBeat / block)));
    INFO("grid during sustain = " << (proc.getGridBassNoteCount() - gridAtPick));
    REQUIRE(proc.getGridBassNoteCount() == gridAtPick);   // no harmony under a sustain

    // Now actually stop (room-level signal, above digital silence so the player
    // is not muted — the tagger declares SILENT). The tagger refuses to call it
    // silence for 4 s after an attack (a ringing note is not a gap) and then
    // needs ~1 s of hysteresis, so hold the stop for ~6 s.
    std::set<int> stopNotes;
    for (int b = 0; b < static_cast<int>(std::ceil(24.0 * samplesPerBeat / block)); ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, kPlayedFreq, 0.002f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
                stopNotes.insert(msg.getNoteNumber());
        }
        ++blockIdx;
    }
    INFO("grid after stop = " << (proc.getGridBassNoteCount() - gridAtPick));
    REQUIRE(proc.getGridBassNoteCount() > gridAtPick);    // the fallback is back
    REQUIRE(stopNotes.count(40) > 0);                     // in the key just played

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: section-progress accessors track Play, Lock, and Transition", "[integration][pipeline][section-progress]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    auto feedTone = [&](float amp, double freq, int numBlocks, int& blockIdx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };

    int blockIdx = 0;

    // ── Riff lock (A) phase (fresh state). ─────────────────────────────────
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(4.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getSectionPhase() == 2);        // Lock
    REQUIRE(proc.getSectionBarsTotal() == 4);
    REQUIRE(proc.getSectionBar() >= 1);
    REQUIRE(proc.getSectionBar() <= 4);
    REQUIRE(proc.getSectionBarsRemaining() == proc.getSectionBarsTotal() - proc.getSectionBar());
    REQUIRE(proc.getSectionProgress() > 0.0f);

    // ── Post-lock transition (B) phase. ─────────────────────────────────────
    feedTone(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getSectionPhase() == 3);        // Transition
    REQUIRE(proc.getSectionBarsTotal() == 4);
    REQUIRE(proc.getSectionBar() >= 1);
    REQUIRE(proc.getSectionBar() <= 4);
    REQUIRE(proc.getSectionBarsRemaining() == proc.getSectionBarsTotal() - proc.getSectionBar());
    REQUIRE(proc.getSectionProgress() > 0.0f);

    // ── Forget → the loop is cancelled, engine idle. ────────────────────────
    proc.requestRiffForget();
    feedTone(0.05, 400.0, static_cast<int>(0.5 * sr / block), blockIdx);
    REQUIRE(proc.getSectionPhase() == 0);        // Idle

    // ── Play mode: the section countdown reports the song form's bar progress. ──
    proc.setCustomSongForm("VERSE:8");
    proc.playActive.store(true, std::memory_order_release);
    feedTone(0.05, 110.0, static_cast<int>(3.0 * sr / block), blockIdx);
    REQUIRE(proc.getSectionPhase() == 1);        // Play (after 1-bar count-in)
    REQUIRE(proc.getSectionBarsTotal() == 8);
    REQUIRE(proc.getSectionBar() >= 1);
    REQUIRE(proc.getSectionBar() <= 8);
    REQUIRE(proc.getSectionBarsRemaining() == proc.getSectionBarsTotal() - proc.getSectionBar());
    REQUIRE(proc.getSectionProgress() > 0.0f);
    REQUIRE(proc.getSectionProgress() <= 1.0f);
    proc.playActive.store(false, std::memory_order_release);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: Play publishes its 1-bar count-in for the UI",
          "[integration][pipeline][play]")
{
    // The editor shows the same amber "Count-in" status for Play as it does for
    // Record riff. SectionPhase stays Idle during the count-in (nothing is
    // playing yet), so the count-in needs its own flag: it must be raised on the
    // Play edge and cleared when the song form starts.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    REQUIRE_FALSE(proc.isPlayCountingIn());

    proc.playActive.store(true, std::memory_order_release);
    {
        juce::AudioBuffer<float> buf(2, block);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
    }
    REQUIRE(proc.isPlayCountingIn());
    REQUIRE(proc.getSectionPhase() == 0);   // Idle: the form has not started

    // Run out the count-in (plus the wait for the next bar line). The flag must
    // clear and the form must start.
    int blockIdx = 0;
    const int maxWait = static_cast<int>(4.0 * sr / block);
    for (int i = 0; i < maxWait && proc.isPlayCountingIn(); ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 110.0, 0.12f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        ++blockIdx;
    }
    REQUIRE_FALSE(proc.isPlayCountingIn());
    REQUIRE(proc.getSectionPhase() == 1);   // Play

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: deterministic riff loop never re-locks onto a NEW riff", "[integration][pipeline][transition][lock]")
{
    // A is stored at capture. B may lock a second riff. Returning to A plays
    // the original snapshot (not B, not a live rewind).
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(6.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(2.0f));

    auto feedChugFreq = [&](double freq, int numBlocks, int& idx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            fillChugBlock(buf, idx, block, sr, freq);
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++idx;
        }
    };
    auto feedTone = [&](float amp, double freq, int numBlocks, int& idx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(idx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++idx;
        }
    };

    int blockIdx = 0;
    BassHitCollector a1;
    recordChugRiff(proc, sr, block, 65.406, blockIdx, &a1, samplesPerBeat);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getSectionPhase() == 2);
    REQUIRE(a1.originSample >= 0);
    feedChugCollecting(proc, sr, block, 65.406, blockIdx, samplesPerBeat, a1,
                       a1.originSample + static_cast<int64_t>(std::ceil(16.0 * samplesPerBeat)));
    REQUIRE(proc.getRiffAOccupiedCount() >= 2);
    bool aSlots[64]{};
    int aMidi[64]{};
    int aGate[64]{};
    for (int s = 0; s < 64; ++s)
    {
        aSlots[s] = proc.getRiffASlotOccupied(s);
        aMidi[s] = proc.getRiffASlotMidi(s);
        aGate[s] = proc.getRiffASlotGate(s);
    }
    bool a1Occ[64]{};
    int a1Map[64]{};
    slotMapsFromHits(a1.hits, a1Occ, a1Map);

    const int cap = static_cast<int>(40.0 * sr / block);
    int guard = 0;
    while (!proc.isTransitionSectionActive() && guard++ < cap)
        feedTone(0.05, 400.0, 1, blockIdx);
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE(proc.getSectionPhase() == 3);

    feedChugFreq(98.0, static_cast<int>(3.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());

    BassHitCollector a2;
    const int64_t a2Need = static_cast<int64_t>(std::ceil(16.0 * samplesPerBeat));
    guard = 0;
    while (guard++ < cap)
    {
        const bool waiting = proc.isTransitionSectionActive();
        juce::AudioBuffer<float> buf(2, block);
        if (waiting)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(0.05 * std::sin(2.0 * M_PI * 400.0 * tt));
                }
            }
        }
        else
            fillChugBlock(buf, blockIdx, block, sr, 65.406);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        collectIfLocked(proc, midi, blockIdx, block, samplesPerBeat, a2);
        ++blockIdx;
        if (a2.originSample >= 0
            && static_cast<int64_t>(blockIdx) * block - a2.originSample >= a2Need)
            break;
    }
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.hasLearnedRiff());
    REQUIRE(proc.getSectionPhase() == 2);
    for (int s = 0; s < 64; ++s)
    {
        REQUIRE(proc.getRiffASlotOccupied(s) == aSlots[s]);
        REQUIRE(proc.getRiffASlotMidi(s) == aMidi[s]);
        REQUIRE(proc.getRiffASlotGate(s) == aGate[s]);
    }
    bool a2Occ[64]{};
    int a2Map[64]{};
    slotMapsFromHits(a2.hits, a2Occ, a2Map);
    for (int s = 0; s < 64; ++s)
    {
        REQUIRE(a2Occ[s] == a1Occ[s]);
        REQUIRE(a2Map[s] == a1Map[s]);
    }

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: capture keeps leading rest bars", "[integration][pipeline][capture][rests]")
{
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));

    proc.requestRiffCaptureStart();
    const int n = static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * sr / block);
    // 1 bar count-in + 2 silent record bars; guitar enters on record bars 3-4.
    // ceil so truncation cannot start the sine a fraction of a 16th early.
    const int silentBlocks = static_cast<int>(std::ceil(3.0 * 4.0 * 60.0 / 120.0 * sr / block)) + 2;
    BassHitCollector col;
    int blockIdx = 0;
    for (int b = 0; b < n; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        buf.clear();
        if (b >= silentBlocks)
        {
            const int pos = b % 24;
            const bool loud = (pos >= 20);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(b) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>((loud ? 0.5 : 0.08)
                                              * std::sin(2.0 * M_PI * 65.406 * tt));
                }
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        collectIfLocked(proc, midi, blockIdx, block, samplesPerBeat, col);
        ++blockIdx;
    }
    REQUIRE(proc.hasLearnedRiff());
    REQUIRE(proc.isGrooveLocked());
    for (int s = 0; s < 32; ++s)
        REQUIRE_FALSE(proc.getRiffASlotOccupied(s));
    REQUIRE(proc.getRiffAOccupiedCount() >= 2);
    REQUIRE(col.originSample >= 0);

    // T2.1: frozen-riff playback is bar-phase locked, not lock-time locked.
    // Measure the 16-beat loop from the processor's bar-locked origin so occupied
    // slots 32–63 (beats 8–16) are not scored as "early" when the lock engages
    // mid-bar. Rounding the detecting block boundary down is not equivalent: when
    // the block straddles the bar line that lands a whole bar early.
    col.hits.clear();
    col.originSample = proc.getRiffAPlayOriginSample();
    REQUIRE(col.originSample >= 0);
    const int64_t now = static_cast<int64_t>(blockIdx) * block;
    feedChugCollecting(proc, sr, block, 65.406, blockIdx, samplesPerBeat, col,
                       now + static_cast<int64_t>(std::ceil(16.0 * samplesPerBeat)));

    bool early = false;
    bool late = false;
    for (const auto& h : col.hits)
    {
        if (h.beatInLoop < 8.0)
            early = true;
        if (h.beatInLoop >= 8.0 && h.beatInLoop < 16.0)
            late = true;
    }
    REQUIRE_FALSE(early);
    REQUIRE(late);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: Play start wipes a prior Record riff", "[integration][pipeline][play][riff]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    int idx = 0;
    recordChugRiff(proc, sr, block, 65.406, idx);
    REQUIRE(proc.hasLearnedRiff());

    proc.setCustomSongForm("VERSE:4");
    proc.playActive.store(true, std::memory_order_release);
    auto quiet = makeSineBuffer(block, 110.0, sr, 0.0005f);
    juce::MidiBuffer midi;
    proc.processBlock(quiet, midi);
    REQUIRE_FALSE(proc.hasLearnedRiff());
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE(proc.getRiffAOccupiedCount() == 0);

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: guitar stop while locked does not kill the kit (T6.4)", "[integration][pipeline][gate][t6.4]")
{
    // T6.4: the lock accompanies independently of picking. Residual RMS below
    // the play floor must NOT all-notes-off or mute frozen bass/drums.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(16.0f));
    int idx = 0;
    recordChugRiff(proc, sr, block, 65.406, idx);
    REQUIRE(proc.isGrooveLocked());

    int bassOnsLate = 0;
    int drumOns = 0;
    const int silentBlocks = static_cast<int>(1.2 * sr / block);
    for (int b = 0; b < silentBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, idx, block, sr, 65.406, 0.0008f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
                ++bassOnsLate;
            if (msg.isNoteOn() && msg.getChannel() == 10)
                ++drumOns;
        }
        ++idx;
    }
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(drumOns > 0);
    REQUIRE(bassOnsLate > 0);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: Play is not gated by the guitar-stop timer",
          "[integration][pipeline][gate][play]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    proc.setCustomSongForm("VERSE:8");
    proc.playActive.store(true, std::memory_order_release);

    int blockIdx = 0;
    const int maxWait = static_cast<int>(4.0 * sr / block);
    for (int i = 0; i < maxWait && proc.getSectionPhase() != 1; ++i)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.12f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.getSectionPhase() == 1);

    bool sawAllOff = false;
    const int quietBlocks = static_cast<int>(1.2 * sr / block);
    for (int b = 0; b < quietBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.0008f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isAllNotesOff())
                sawAllOff = true;
        }
        ++blockIdx;
    }
    REQUIRE_FALSE(sawAllOff);

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: click and lock length follow 85 BPM not 120", "[integration][pipeline][tempo]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);

    struct FakePlayHead final : public juce::AudioPlayHead
    {
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
        {
            juce::AudioPlayHead::PositionInfo info;
            info.setBpm(85.0);
            info.setIsPlaying(true);
            info.setTimeInSamples(samples);
            return info;
        }
        int64_t samples = 0;
    };
    FakePlayHead ph;
    proc.setPlayHead(&ph);

    proc.requestRiffCaptureStart();
    std::vector<int64_t> kickOns;
    const int n = static_cast<int>(6.0 * 4.0 * 60.0 / 85.0 * sr / block);
    for (int b = 0; b < n; ++b)
    {
        auto buf = makeSilenceBuffer(block);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == 36)
                kickOns.push_back(ph.samples + meta.samplePosition);
        }
        ph.samples += block;
    }
    REQUIRE(kickOns.size() >= 2);
    const double expected85 = 4.0 * 60.0 / 85.0 * sr;
    const double expected120 = 4.0 * 60.0 / 120.0 * sr;
    const double period = static_cast<double>(kickOns[1] - kickOns[0]);
    REQUIRE(std::abs(period - expected85) < 1024.0);
    REQUIRE(std::abs(period - expected120) > 1024.0);

    proc.setPlayHead(nullptr);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: Record B listen rotates a contrast groove unlike A", "[integration][pipeline][riffb][transition][t6.3]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    auto feedTone = [&](float amp, double freq, int numBlocks, int& idx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(idx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++idx;
        }
    };

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getDrumA() == 1);

    const int cap = static_cast<int>(20.0 * sr / block);
    int guard = 0;
    while (!proc.isTransitionSectionActive() && guard++ < cap)
        feedTone(0.05, 400.0, 1, blockIdx);
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE_FALSE(proc.isRiffBLocked());

    const int drumB0 = proc.getDrumB0();
    REQUIRE(drumB0 != proc.getDrumA());
    REQUIRE((PatternRules::isStrongContrastPattern(drumB0)
             || !PatternRules::isVerseFeelNeighborhood(drumB0)));

    // T6.3: B-listen rotates the contrast pool instead of pinning drumB0.
    std::set<int> heard;
    const int listenBlocks = static_cast<int>(5.0 * sr / block);
    for (int b = 0; b < listenBlocks; ++b)
    {
        feedTone(0.05, 400.0, 1, blockIdx);
        heard.insert(proc.getDisplayPatternIndex());
    }
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE_FALSE(proc.isRiffBLocked());
    REQUIRE(heard.size() >= 2);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: Record B can lock a second riff without mutating A", "[integration][pipeline][riffb][lock]")
{
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    auto feedTone = [&](float amp, double freq, int numBlocks, int& idx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(idx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++idx;
        }
    };

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    bool aSlots[64]{};
    int aMidi[64]{};
    for (int s = 0; s < 64; ++s)
    {
        aSlots[s] = proc.getRiffASlotOccupied(s);
        aMidi[s] = proc.getRiffASlotMidi(s);
    }

    const int cap = static_cast<int>(40.0 * sr / block);
    int guard = 0;
    while (!proc.isTransitionSectionActive() && guard++ < cap)
        feedTone(0.05, 400.0, 1, blockIdx);
    REQUIRE(proc.isTransitionSectionActive());

    std::set<int> bBass;
    guard = 0;
    while (!proc.isRiffBLocked() && guard++ < cap && proc.isTransitionSectionActive())
    {
        juce::AudioBuffer<float> buf(2, block);
        fillChugBlock(buf, blockIdx, block, sr, 98.0);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        if (proc.isRiffBLocked())
        {
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                    bBass.insert(msg.getNoteNumber());
            }
        }
        ++blockIdx;
    }
    REQUIRE(proc.isRiffBLocked());
    REQUIRE(proc.getRiffBOccupiedCount() >= 2);
    for (int s = 0; s < 64; ++s)
    {
        REQUIRE(proc.getRiffASlotOccupied(s) == aSlots[s]);
        REQUIRE(proc.getRiffASlotMidi(s) == aMidi[s]);
    }

    for (int b = 0; b < static_cast<int>(8.0 * sr / block); ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillChugBlock(buf, blockIdx, block, sr, 98.0);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2)
                bBass.insert(msg.getNoteNumber());
        }
        ++blockIdx;
    }
    REQUIRE_FALSE(bBass.empty());
    const bool bNotJustC2 = (bBass.count(36) == 0) || (bBass.size() > 1);
    REQUIRE(bNotJustC2);

    BassHitCollector a2;
    const int64_t a2Need = static_cast<int64_t>(std::ceil(16.0 * samplesPerBeat));
    guard = 0;
    while (guard++ < cap)
    {
        const bool waiting = proc.isTransitionSectionActive();
        juce::AudioBuffer<float> buf(2, block);
        if (waiting)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(0.05 * std::sin(2.0 * M_PI * 400.0 * tt));
                }
            }
        }
        else
            fillChugBlock(buf, blockIdx, block, sr, 65.406);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        collectIfLocked(proc, midi, blockIdx, block, samplesPerBeat, a2);
        ++blockIdx;
        if (!waiting && a2.originSample >= 0
            && static_cast<int64_t>(blockIdx) * block - a2.originSample >= a2Need)
            break;
    }
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());
    for (int s = 0; s < 64; ++s)
    {
        REQUIRE(proc.getRiffASlotOccupied(s) == aSlots[s]);
        REQUIRE(proc.getRiffASlotMidi(s) == aMidi[s]);
    }

    proc.releaseResources();
}

TEST_CASE("T5.2: a sustained chord plays O(1) bass notes per chord, not eight 16ths",
          "[integration][pipeline][t5.2][bass]")
{
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));

    proc.requestRiffCaptureStart();
    const int n = static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * sr / block);
    int blockIdx = 0;
    for (int b = 0; b < n; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.40f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getRiffAOccupiedCount() >= 8);
    int maxGate = 0;
    int onsetCount = 0;
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
    {
        const int g = proc.getRiffASlotGate(s);
        if (g > maxGate)
            maxGate = g;
        if (g > 0)
            ++onsetCount;
    }
    INFO("occupied=" << proc.getRiffAOccupiedCount()
         << " maxGate=" << maxGate << " onsetCount=" << onsetCount
         << " slot0=" << proc.getRiffASlotGate(0));
    REQUIRE(maxGate >= 4);
    REQUIRE(onsetCount <= 16);

    int bassOns = 0;
    const int holdBlocks = static_cast<int>(16.0 * samplesPerBeat / block);  // one 4-bar loop
    for (int b = 0; b < holdBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fillSineAmp(buf, blockIdx, block, sr, 65.406, 0.40f);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2 && msg.getVelocity() > 0)
                ++bassOns;
        }
        ++blockIdx;
    }
    // One onset covering the whole 4-bar capture: one note per loop.
    REQUIRE(bassOns >= 1);
    REQUIRE(bassOns <= 4);

    proc.releaseResources();
}

TEST_CASE("T5.2: a 16th-note chug still emits one bass note per 16th",
          "[integration][pipeline][t5.2][bass]")
{
    const double sr = 48000.0;
    const int block = 512;
    const double samplesPerBeat = 60.0 / 120.0 * sr;
    const double sixteenth = 0.25 * samplesPerBeat;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(8.0f));

    auto fill16 = [&](juce::AudioBuffer<float>& buf, int idx)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* p = buf.getWritePointer(ch);
            for (int i = 0; i < block; ++i)
            {
                const double absS = static_cast<double>(idx) * block + i;
                const double pos = std::fmod(absS, sixteenth);
                const float amp = (pos < 0.35 * sixteenth) ? 0.5f : 0.008f;
                p[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * 65.406 * absS / sr));
            }
        }
    };

    proc.requestRiffCaptureStart();
    const int n = static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * sr / block);
    int blockIdx = 0;
    for (int b = 0; b < n; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fill16(buf, blockIdx);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.isGrooveLocked());
    int onsetSlots = 0;
    int gateOne = 0;
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
    {
        const int g = proc.getRiffASlotGate(s);
        if (g > 0)
            ++onsetSlots;
        if (g == 1)
            ++gateOne;
    }
    REQUIRE(onsetSlots >= 8);

    int bassOns = 0;
    const int holdBlocks = static_cast<int>(16.0 * samplesPerBeat / block);  // one 4-bar loop
    for (int b = 0; b < holdBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        fill16(buf, blockIdx);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn() && msg.getChannel() == 2 && msg.getVelocity() > 0)
                ++bassOns;
        }
        ++blockIdx;
    }
    INFO("occupied=" << proc.getRiffAOccupiedCount()
         << " onsetSlots=" << onsetSlots << " gateOne=" << gateOne
         << " bassOns=" << bassOns);
    // One note per onset (gate > 0) per 4-bar loop. gate==1 is a 16th; longer
    // coalesced chugs still count as one onset. Pre-T5.2 this was 64 retriggers.
    REQUIRE(bassOns >= juce::jmax(8, onsetSlots - 4));
    REQUIRE(bassOns <= onsetSlots + 8);

    proc.releaseResources();
}

TEST_CASE("T6.1: a large RMS step commits a Play groove change within 250 ms",
          "[integration][pipeline][t6.1][reactivity]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    proc.setCustomSongForm("VERSE:16");
    proc.playActive.store(true, std::memory_order_release);

    int guard = 0;
    while ((proc.getSectionPhase() != 1 || proc.getDisplayPatternIndex() <= 0)
           && guard++ < blocksForBars(sr, block, 6.0))
        feedQuietBlocks(proc, sr, block, 1);
    REQUIRE(proc.getSectionPhase() == 1);

    auto moderate = makeSineBuffer(block, 1500.0, sr, 0.08f);
    feedBlocks(proc, moderate, block, blocksForBars(sr, block, 2.0));
    proc.flushBackgroundInferenceForTests();
    const int before = proc.getDisplayPatternIndex();
    REQUIRE(before > 0);

    auto hot = makeSineBuffer(block, 1500.0, sr, 0.55f);
    const int window = static_cast<int>(0.250 * sr / block) + 2;
    bool changed = false;
    for (int i = 0; i < window; ++i)
    {
        juce::AudioBuffer<float> buf = hot;
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        if (proc.getDisplayPatternIndex() != before
            && proc.getDisplayPatternIndex() > 0)
        {
            changed = true;
            break;
        }
    }
    REQUIRE(changed);

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

TEST_CASE("T6.2: a different riff mid-transition does not cut it short",
          "[integration][pipeline][transition][lock][t6.2]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(6.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    auto feedTone = [&](float amp, double freq, int numBlocks, int& blockIdx) {
        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* p = buf.getWritePointer(ch);
                const double t = static_cast<double>(blockIdx) * block / sr;
                for (int i = 0; i < block; ++i)
                {
                    const double tt = t + static_cast<double>(i) / sr;
                    p[i] = static_cast<float>(amp * std::sin(2.0 * M_PI * freq * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());
    feedTone(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE(proc.isTransitionSectionActive());

    // Slow half-note pulses (IOI ~ 2 beats) — not the recorded 16th chug.
    const int pulseEvery = static_cast<int>(1.0 * sr / block);
    for (int b = 0; b < static_cast<int>(3.0 * sr / block); ++b)
    {
        const bool loud = (b % pulseEvery) == 0;
        juce::AudioBuffer<float> buf(2, block);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = buf.getWritePointer(ch);
            const double t = static_cast<double>(blockIdx) * block / sr;
            for (int i = 0; i < block; ++i)
            {
                const double tt = t + static_cast<double>(i) / sr;
                p[i] = static_cast<float>((loud ? 0.45f : 0.04f)
                                         * std::sin(2.0 * M_PI * 98.0 * tt));
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        ++blockIdx;
    }
    REQUIRE(proc.isTransitionSectionActive());
    REQUIRE_FALSE(proc.isGrooveLocked());

    proc.releaseResources();
}

TEST_CASE("T6.4: 6 s of silence mid-lock keeps drums and frozen bass going",
          "[integration][pipeline][lock][t6.4]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(16.0f));

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    int drumOns = 0;
    int bassOns = 0;
    const int silentBlocks = static_cast<int>(6.0 * sr / block);
    auto breath = makeSineBuffer(block, 110.0, sr, 0.0004f);
    for (int b = 0; b < silentBlocks; ++b)
    {
        juce::AudioBuffer<float> buf = breath;
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getVelocity() <= 0)
                continue;
            if (msg.getChannel() == 10)
                ++drumOns;
            else if (msg.getChannel() == 2)
                ++bassOns;
        }
        ++blockIdx;
    }
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(drumOns > 0);
    REQUIRE(bassOns > 0);

    proc.releaseResources();
}

// ══════════════════════════════════════════════════════════════════════════════
// Step 2: Play-mode per-section riff learning and recall
//
// First pass through a section: mirror live AND capture the riff in parallel.
// A return to the same section NAME: replay the captured riff (Producer::Frozen).
// Assertions are on emitted MIDI and BassVoice provenance, never on PhraseLearner
// internals (docs/BASS_MIRRORING.md §4.4, §7).
// ══════════════════════════════════════════════════════════════════════════════

namespace {

// An 8th-note plucked chug, phase-locked to the ABSOLUTE sample clock so the
// rendered pattern is identical at every host block size.
void fillPluck8th(juce::AudioBuffer<float>& buf, int64_t blockStartAbs, int block,
                  double sr, double freq, int eighthSamples)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* p = buf.getWritePointer(ch);
        for (int i = 0; i < block; ++i)
        {
            const int64_t abs = blockStartAbs + i;
            const int into = static_cast<int>(abs % eighthSamples);
            const double intoSec = static_cast<double>(into) / sr;
            const double env = std::exp(-intoSec * 9.0);
            p[i] = static_cast<float>(0.55 * env
                   * std::sin(2.0 * M_PI * freq * static_cast<double>(abs) / sr));
        }
    }
}

struct SectionRecall
{
    std::string name;
    int frozen = 0;
    int mirror = 0;
    std::vector<int64_t> frozenAbs;   // absolute samples of Frozen note-ons
    std::set<int> notes;
};

// Drive the real processor in Play mode over a short song form. The caller
// supplies the chug frequency per section name (so VERSE and CHORUS differ).
// Returns one stat record per section entry, in order.
struct PlayFormResult
{
    std::vector<SectionRecall> sections;
    int storedVerse = 0;
    int storedChorus = 0;
    int block = 512;
};

PlayFormResult runPlayForm(const char* form, int block, double seconds,
                           double verseFreq, double chorusFreq, bool sparseFirstVerse = false,
                           double secondVerseFreq = -1.0)
{
    const double sr = 48000.0;
    const double spb = 60.0 / 120.0 * sr;
    const int eighth = static_cast<int>(std::llround(0.5 * spb));

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(120.0f));
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    proc.setCustomSongForm(form);
    // Let the parsed form reach the audio thread once before arming Play.
    {
        juce::AudioBuffer<float> warm(2, block);
        warm.clear();
        juce::MidiBuffer midi;
        proc.processBlock(warm, midi);
    }
    proc.playActive.store(true, std::memory_order_release);

    PlayFormResult result;
    result.block = block;
    std::string cur;
    int prevFrozen = 0, prevMirror = 0;
    int64_t blockStart = static_cast<int64_t>(block);
    const int totalBlocks = static_cast<int>(seconds * sr / block);
    bool collecting = false;
    bool firstVerseDone = false;
    int blocksIntoFirstVerse = 0;
    int verseVisits = 0;
    for (int b = 0; b < totalBlocks; ++b)
    {
        const auto name = proc.getCurrentSectionName().toStdString();
        // Capture starts only once the count-in has finished (Play phase).
        if (proc.getSectionPhase() == 1)
        {
            collecting = true;
            if (name != cur && name != "Complete")
            {
                result.sections.push_back(SectionRecall{});
                result.sections.back().name = name;
                if (name == "VERSE")
                    ++verseVisits;
                cur = name;
            }
        }
        const double freq = (name == "CHORUS")
            ? chorusFreq
            : ((verseVisits >= 2 && secondVerseFreq > 0.0) ? secondVerseFreq : verseFreq);
        juce::AudioBuffer<float> buf(2, block);
        const bool inFirstVerse = collecting && name == "VERSE" && !firstVerseDone;
        if (inFirstVerse && name != "CHORUS")
            ++blocksIntoFirstVerse;
        if (name == "CHORUS")
            firstVerseDone = true;
        if (sparseFirstVerse && inFirstVerse)
        {
            // One short blip, then silence: fewer than two 16ths are occupied, so
            // there is no riff to remember.
            buf.clear();
            if (blocksIntoFirstVerse <= 1)
                fillPluck8th(buf, blockStart, block, sr, freq, eighth);
        }
        else
        {
            fillPluck8th(buf, blockStart, block, sr, freq, eighth);
        }
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 2 || msg.getVelocity() <= 0)
                continue;
            if (!collecting || result.sections.empty())
                continue;
            auto& s = result.sections.back();
            s.notes.insert(msg.getNoteNumber());
            if (proc.getLastBassProducer() == BassVoice::Producer::Frozen)
                s.frozenAbs.push_back(blockStart + meta.samplePosition);
        }
        const int f = proc.getBassProducerCount(BassVoice::Producer::Frozen);
        const int m = proc.getBassProducerCount(BassVoice::Producer::Mirror);
        if (collecting && !result.sections.empty())
        {
            result.sections.back().frozen += (f - prevFrozen);
            result.sections.back().mirror += (m - prevMirror);
        }
        prevFrozen = f;
        prevMirror = m;
        blockStart += block;
    }

    result.storedVerse = proc.getStoredSectionRiffOccupiedCount("VERSE");
    result.storedChorus = proc.getStoredSectionRiffOccupiedCount("CHORUS");
    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
    return result;
}

} // namespace

// §6.1 — Learn on the first pass, recall on the return.
// §6.3 — The first pass is NOT silent: it is the live mirror while capturing.
TEST_CASE("Step2: Play mirrors a section's first pass and replays it on return",
          "[integration][pipeline][step2][recall]")
{
    const auto r = runPlayForm("VERSE:1,CHORUS:1,VERSE:1", 512, 9.0, 65.406, 98.0);

    REQUIRE(r.sections.size() >= 3);
    // First VERSE: live mirror, not a replay.
    REQUIRE(r.sections[0].name == "VERSE");
    REQUIRE(r.sections[0].mirror > 0);
    REQUIRE(r.sections[0].frozen == 0);
    REQUIRE_FALSE(r.sections[0].notes.empty());
    // CHORUS: different material, also mirrored on its first pass.
    REQUIRE(r.sections[1].name == "CHORUS");
    REQUIRE(r.sections[1].mirror > 0);
    REQUIRE(r.sections[1].frozen == 0);
    // Second VERSE: replay of the stored VERSE riff.
    REQUIRE(r.sections[2].name == "VERSE");
    REQUIRE(r.sections[2].frozen > 0);
    REQUIRE(r.sections[2].mirror == 0);

    // Both sections stored their own riff.
    REQUIRE(r.storedVerse >= 2);
    REQUIRE(r.storedChorus >= 2);

    // The replay is the VERSE riff, not the CHORUS one.
    REQUIRE(r.sections[2].notes.count(36) > 0);
    REQUIRE(r.sections[2].notes.count(43) == 0);
}

// §6.2 — Sections do not cross-contaminate: CHORUS replays CHORUS, VERSE replays VERSE.
TEST_CASE("Step2: a returned section replays its own riff, not another section's",
          "[integration][pipeline][step2][isolation]")
{
    const auto r = runPlayForm("VERSE:1,CHORUS:1,VERSE:1,CHORUS:1", 512, 11.0, 65.406, 98.0);

    REQUIRE(r.sections.size() >= 4);
    REQUIRE(r.sections[2].name == "VERSE");
    REQUIRE(r.sections[3].name == "CHORUS");

    // VERSE replay is C (36); CHORUS replay is G (43). Neither bleeds into the other.
    REQUIRE(r.sections[2].frozen > 0);
    REQUIRE(r.sections[2].notes.count(36) > 0);
    REQUIRE(r.sections[2].notes.count(43) == 0);
    REQUIRE(r.sections[3].frozen > 0);
    REQUIRE(r.sections[3].notes.count(43) > 0);
    REQUIRE(r.sections[3].notes.count(36) == 0);
}

// §6.4 — The replay is buffer-size invariant (identical absolute event samples).
TEST_CASE("Step2: section replay is host-buffer-size invariant",
          "[integration][pipeline][step2][bufferinvariance]")
{
    const std::vector<int64_t>* ref = nullptr;
    std::vector<int64_t> refStore;
    for (int bs : { 128, 512, 2048 })
    {
        const auto r = runPlayForm("VERSE:1,CHORUS:1,VERSE:1", bs, 9.0, 65.406, 98.0);
        REQUIRE(r.sections.size() >= 3);
        REQUIRE(r.sections[2].name == "VERSE");
        REQUIRE(r.sections[2].frozen > 0);
        REQUIRE_FALSE(r.sections[2].frozenAbs.empty());
        if (ref == nullptr)
        {
            refStore = r.sections[2].frozenAbs;
            ref = &refStore;
        }
        else
        {
            INFO("block=" << bs << " ref=" << ref->size()
                          << " got=" << r.sections[2].frozenAbs.size());
            REQUIRE(r.sections[2].frozenAbs == *ref);
        }
    }
}

// §6.6 — A returning section keeps learning: the replay is authoritative while
//        it plays, but the pass re-captures, so the memory tracks the player.
TEST_CASE("Step2: a returning section re-learns (a later pass replaces the memory)",
          "[integration][pipeline][step2][relearn]")
{
    // VERSE pass 1 plays C2 (65.406); pass 2 plays E2 (82.407) while the stored C2
    // replay plays; pass 3 must replay the RELEARNED E2, not the stale C2. Before
    // this a section was frozen on its first pass forever.
    const auto r = runPlayForm("VERSE:1,CHORUS:1,VERSE:1,CHORUS:1,VERSE:1", 512, 15.0,
                               65.406, 98.0, /*sparseFirstVerse=*/false,
                               /*secondVerseFreq=*/82.407);
    REQUIRE(r.sections.size() >= 5);
    REQUIRE(r.sections[0].name == "VERSE");
    REQUIRE(r.sections[2].name == "VERSE");
    REQUIRE(r.sections[4].name == "VERSE");

    // Pass 1: live mirror of C2.
    REQUIRE(r.sections[0].mirror > 0);
    REQUIRE(r.sections[0].frozen == 0);

    // Pass 2: replay of the stored C2 while the E2 pass is captured.
    REQUIRE(r.sections[2].frozen > 0);
    REQUIRE(r.sections[2].notes.count(36) > 0);

    // Pass 3: the memory was replaced by pass 2, so it now replays E2 (40).
    REQUIRE(r.sections[4].frozen > 0);
    REQUIRE(r.sections[4].notes.count(40) > 0);
    REQUIRE(r.sections[4].notes.count(36) == 0);
}

// §6.5 — A section with fewer than two occupied slots stores nothing and falls
//        back to live mirroring on the next visit.
TEST_CASE("Step2: a section with no real riff is not remembered",
          "[integration][pipeline][step2][sparse]")
{
    const auto r = runPlayForm("VERSE:1,CHORUS:1,VERSE:1", 512, 9.0, 65.406, 98.0,
                               /*silentFirstBar=*/true);
    REQUIRE(r.sections.size() >= 2);
    REQUIRE(r.sections[0].name == "VERSE");
    REQUIRE(r.storedVerse == 0);       // nothing worth remembering
    REQUIRE(r.storedChorus >= 2);      // the CHORUS still learned normally
    if (r.sections.size() >= 3)
    {
        REQUIRE(r.sections[2].name == "VERSE");
        REQUIRE(r.sections[2].frozen == 0);   // fell back to the live mirror
    }
}

// §6.6 — A form wrap re-enters a section and replays its stored riff.
//        Toggling the loop parameter restarts the form (setLooping resets the
//        sequencer), which exercises the `barsElapsedNow < lastSeenBarsElapsed`
//        re-entry path.
TEST_CASE("Step2: a form wrap re-enters a section and replays the stored riff",
          "[integration][pipeline][step2][wrap]")
{
    const double sr = 48000.0;
    const int block = 512;
    const double spb = 60.0 / 120.0 * sr;
    const int eighth = static_cast<int>(std::llround(0.5 * spb));

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("bpm"))
        p->setValueNotifyingHost(p->convertTo0to1(120.0f));
    if (auto* p = proc.getApvts().getParameter("genre"))
        p->setValueNotifyingHost(p->convertTo0to1(0.0f));
    proc.setCustomSongForm("VERSE:1,CHORUS:1,VERSE:1");
    proc.playActive.store(true, std::memory_order_release);

    int64_t blockStart = 0;
    int frozenDuringReentry = 0;
    bool sawChorus = false;
    bool loopToggled = false;
    bool reenteredVersePostWrap = false;
    int prevFrozen = 0;

    const int totalBlocks = static_cast<int>(13.0 * sr / block);
    for (int b = 0; b < totalBlocks; ++b)
    {
        const auto name = proc.getCurrentSectionName().toStdString();
        if (name == "CHORUS")
            sawChorus = true;

        // Once the CHORUS has been reached, restart the form (a wrap) by
        // switching the loop parameter on.
        if (sawChorus && !loopToggled)
        {
            if (auto* p = proc.getApvts().getParameter("loop"))
                p->setValueNotifyingHost(1.0f);
            loopToggled = true;
        }

        const double freq = (name == "CHORUS") ? 98.0 : 65.406;
        juce::AudioBuffer<float> buf(2, block);
        fillPluck8th(buf, blockStart, block, sr, freq, eighth);
        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();

        // After the wrap, the form is back at VERSE. Detect the re-entry and
        // count the Frozen notes it emits (read the count AFTER the block).
        if (loopToggled && name == "VERSE" && proc.isPlaySectionReplaying())
            reenteredVersePostWrap = true;
        const int frozenNow = proc.getBassProducerCount(BassVoice::Producer::Frozen);
        if (reenteredVersePostWrap && name == "VERSE")
            frozenDuringReentry += frozenNow - prevFrozen;
        prevFrozen = frozenNow;
        blockStart += block;
    }

    REQUIRE(sawChorus);
    REQUIRE(loopToggled);
    REQUIRE(reenteredVersePostWrap);
    REQUIRE(frozenDuringReentry > 0);

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}

// Step 3: the captured riff must keep the PICKED articulation, not merge fast
// re-picks into one long legato gate. The slot-peak/tail heuristic alone missed
// re-picks of the same note (their slot peaks are similar), so the locked bass
// played a sparse drone. The capture now also marks a 16th as an onset when the
// attack detector accepted a real pick inside it.
TEST_CASE("Processor pipeline: the captured riff keeps the picked articulation",
          "[integration][pipeline][lock][capture]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    int blockIdx = 0;
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    int onsets = 0, maxGate = 0, occupied = 0;
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
    {
        if (!proc.getRiffASlotOccupied(s))
            continue;
        ++occupied;
        const int g = proc.getRiffASlotGate(s);
        if (g > 0)
            ++onsets;
        maxGate = std::max(maxGate, g);
    }
    INFO("occupied=" << occupied << " onsets=" << onsets << " maxGate=" << maxGate);
    REQUIRE(occupied >= 8);
    // A 16th-note chug over 4 bars must be articulated as separate onsets, not a
    // handful of long gates.
    REQUIRE(onsets >= 12);

    proc.releaseResources();
}

