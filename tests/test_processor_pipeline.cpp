/**
 * Integration tests for AccompanimentProcessor — full plugin pipeline.
 *
 * Uses the test-only control hooks (pause / flush / resume background inference)
 * to exercise the processor deterministically without real-time threading.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

#include <JuceHeader.h>
#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"

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

} // namespace

// ─── Silent pipeline ─────────────────────────────────────────────────────────

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
    const int idxBefore = proc.getDisplayPatternIndex();
    REQUIRE(idxBefore > 0);

    // Feed one more block so the queue has a fresh feature for the next flush
    feedBlocks(proc, makeSineBuffer(512, 1500.0, 48000.0, 0.5f), 512, 1);

    // Trigger rejection — should exclude the current pattern for one cycle
    proc.patternRejectionCount.store(1, std::memory_order_release);

    proc.flushBackgroundInferenceForTests();
    const int idxAfter = proc.getDisplayPatternIndex();

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

    const int pat = proc.getDisplayPatternIndex();
    // Sludge → metal routing: SOFT low-energy stays in the metal/shared set.
    REQUIRE(pat >= 1);
    REQUIRE(pat < 22);
    proc.releaseResources();
}

// Arm a deterministic riff lock by Record riff: count-in 1 bar + record 4 bars
// of the given chug `freq`, which commits a grid take and engages the groove
// lock (playOn is off). Used instead of the removed auto-lock-by-listening,
// since the engine now only listens once armed.
static void recordChugRiff(AccompanimentProcessor& proc, double sr, int block,
                           double freq, int& blockIdx)
{
    proc.requestRiffCaptureStart();
    constexpr int cycle = 24;  // 20 quiet + 4 loud blocks → attack pulses
    const int n = static_cast<int>(5.25 * 4.0 * 60.0 / 120.0 * sr / block);
    for (int b = 0; b < n; ++b)
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
    // The transition section must not be the riff's own family's section name —
    // the grammar guarantees contrast (picked against pattern family).
    REQUIRE(std::string(proc.getTransitionSectionName()) != "VERSE");

    // Phase 3: feed through the 4-bar transition hold (8s) → it firmly returns
    // to the locked riff (A): the groove re-locks instead of releasing to follow.
    feed(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());

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

TEST_CASE("Processor pipeline: riff re-appearing mid-transition cuts it short and re-locks", "[integration][pipeline][transition][lock]")
{
    // A5.2 regression: if the guitarist keeps playing the recorded riff right
    // up to the lock expiry, the transition must still ENGAGE (not be skipped)
    // and then be cut short as soon as the riff genuinely re-appears mid-B,
    // re-locking the riff (A) immediately. Before the fix the stale "riff fresh"
    // check cancelled the transition on the very first block after expiry.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock hold (8s) for a short test
    // Long transition (16 bars) so the cut fires well before it would complete.
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(16.0f));
    if (auto* p = proc.getApvts().getParameter("transitionSections"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(1.0f));

    // Chug cycle: 20 quiet + 4 loud blocks of a C2 sine → real attack pulses.
    constexpr int cycle = 24;
    auto feedChug = [&](int numBlocks, int& blockIdx) {
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
                    p[i] = static_cast<float>((loud ? 0.5 : 0.08)
                                              * std::sin(2.0 * M_PI * 65.406 * tt));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            ++blockIdx;
        }
    };
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
    // Phase 1: Record a C2 riff (deterministic lock).
    recordChugRiff(proc, sr, block, 65.406, blockIdx);
    REQUIRE(proc.isGrooveLocked());

    // Phase 2: sustained non-riff tone past the 4-bar hold → transition engages.
    feedTone(0.05, 400.0, static_cast<int>(9.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());
    REQUIRE(proc.isTransitionSectionActive());

    // Phase 3: the riff re-appears (feed the chug again) → the transition is cut
    // short and the groove re-locks, returning firmly to the riff (A).
    feedChug(static_cast<int>(1.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isTransitionSectionActive());
    REQUIRE(proc.isGrooveLocked());

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

TEST_CASE("Processor pipeline: bass root maps C2→36, E2→40, G2→43", "[integration][pipeline]")
{
    // Regression #1: the fallback harmonic bass root must fold onto the correct
    // pitch class (not anchor to E). Sustained tones (no riff) exercise the
    // root-following fallback when the phrase learner is not locked.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    proc.playActive.store(true, std::memory_order_release);

    auto collectRoot = [&](double freq)
    {
        std::set<int> notes;
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
                    p[i] = static_cast<float>(0.15 * std::sin(2.0 * M_PI * freq * t)
                                            + 0.06 * std::sin(2.0 * M_PI * freq * 2.0 * t));
                }
            }
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                    notes.insert(msg.getNoteNumber());
            }
        }
        return notes;
    };

    auto c2 = collectRoot(65.406);
    REQUIRE(c2.count(36) > 0);

    auto e2 = collectRoot(82.407);
    REQUIRE(e2.count(40) > 0);

    auto g2 = collectRoot(98.0);
    REQUIRE(g2.count(43) > 0);

    proc.releaseResources();
}

TEST_CASE("Processor pipeline: sparse picking drives the bass instead of a fixed groove", "[integration][pipeline][bass]")
{
    // The bass has two Play-mode personalities. When the guitarist is picked
    // (audible attacks), the bass mirrors those attacks ("play along"); when they
    // hold a sustained tone the bass falls back to a fixed beat-groove. This guards
    // that a picked figure keeps the bass attached to the guitar — leaner, attack-
    // driven onsets — instead of piling the steady groove on top. It compares the
    // SAME 7-bar signal rendered as (a) a sustained tone and (b) a sparse picked
    // figure, and checks the picked figure yields fewer bass onsets than the
    // sustained grid.
    const double sr = 48000.0;
    const int block = 512;
    const int bars = 7;
    const int barSamples = 4 * static_cast<int>(60.0 / 120.0 * sr);  // 96000 @ 120 BPM
    const int total = barSamples * bars;

    auto buildSource = [&](bool picked) {
        // Use sr/block (≈93.75 Hz): exactly one cycle per 512-sample block, so the
        // per-block RMS is constant and the quiet base produces no false attacks.
        const double f = sr / block;
        juce::AudioBuffer<float> src(2, total);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* p = src.getWritePointer(ch);
            for (int i = 0; i < total; ++i)
                p[i] = 0.04f * static_cast<float>(std::sin(2.0 * M_PI * f * i / sr));
        }
        if (picked)
        {
            // A short, clearly-rhythmical loud→quiet→loud pick at beat 2 of bar 0.
            // "picked" is only used to flip the input; this is a behaviour guard,
            // not a strict single-attack discriminator (the 100 ms RMS window
            // smooths edges so exact attack counts are not atomic).
            const int s = 24000;
            const auto fill = [&](int from, int to, float amp) {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = from; i < to && i < total; ++i)
                        src.setSample(ch, i, amp * static_cast<float>(
                            std::sin(2.0 * M_PI * f * i / sr)));
            };
            fill(s, s + 20 * block, 0.45f);
            fill(s + 20 * block, s + 28 * block, 0.04f);
            fill(s + 28 * block, s + 48 * block, 0.45f);
        }
        return src;
    };

    auto runAndCount = [&](bool picked) {
        AccompanimentProcessor proc;
        proc.prepareToPlay(sr, block);
        proc.pauseBackgroundInferenceForTests();
        proc.playActive.store(true, std::memory_order_release);
        auto src = buildSource(picked);
        const int totalBlocks = total / block;
        int count = 0;
        for (int b = 0; b < totalBlocks; ++b)
        {
            juce::AudioBuffer<float> buf(2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample(ch, i, src.getSample(ch, (b * block + i) % total));
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            proc.flushBackgroundInferenceForTests();
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 2)
                    ++count;
            }
        }
        proc.releaseResources();
        return count;
    };

    const int sustainedCount = runAndCount(false);
    const int pickedCount = runAndCount(true);
    INFO("sustained (grid) bass onsets = " << sustainedCount);
    INFO("picked (mirrored) bass onsets = " << pickedCount);
    REQUIRE(pickedCount < sustainedCount);  // the guitarist drives the bass, not the grid
    REQUIRE(pickedCount >= 1);              // the listening mirror actually fired
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
        REQUIRE(ed->getHeight() >= 760);
        REQUIRE(ed->getWidth() >= 520);
        delete ed;
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

TEST_CASE("Processor pipeline: play mode phrases grooves, re-seeds per section instance, and fills the last bar", "[integration][pipeline][play][variety]")
{
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    // INTRO:9 then two VERSE sections. The first play block loads the form AND
    // starts the grid at host sample 0 (beat 0 = bar boundary), so changes are
    // applied without lag. Entry-bar seed: VERSE#1 = 9 ^ (1*31) = 22,
    // VERSE#2 = 13 ^ (2*31) = 51.
    proc.setCustomSongForm("INTRO:9,VERSE:4,VERSE:4");
    proc.playActive.store(true, std::memory_order_release);

    // Quiet (non-digital-silence) audio so play-mode drums are not gated.
    auto quiet = makeSineBuffer(block, 110.0, sr, 0.0005f);

    // 17 bars at 120 BPM = 2 s/bar = 96000 samples/bar = 187.5 blocks/bar.
    constexpr int64_t kSamplesPerBar = 96000;
    constexpr int kBlocksPerBar = 188;
    constexpr int kTotalBars = 17;

    // Per-bar signature: {(note, 16th-tick)} on the drum channel.
    constexpr int kSnare = 38;
    constexpr int kCrash = 49;
    constexpr int kHatClosed = 42;
    constexpr int kHatOpen = 46;
    std::vector<std::set<std::pair<int, int>>> barSigs(kTotalBars);
    int64_t blockStartSample = 0;
    for (int b = 0; b < kBlocksPerBar * kTotalBars; ++b)
    {
        juce::MidiBuffer midi;
        proc.processBlock(quiet, midi);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (!msg.isNoteOn() || msg.getChannel() != 10) continue;
            const int note = msg.getNoteNumber();
            if (note == kCrash) continue;                       // transition crash
            if (note == kSnare && msg.getVelocity() < 50) continue;  // ghost snare
            // Tier-0 openHat ornamentation varies the closed hat (42) -> open
            // hat (46) per bar in the verse, so the hat voices are excluded from
            // the "same groove" signature. Ride/bell stay: they distinguish
            // patterns (e.g. Rock Backbeat vs Verse Groove) and are never
            // ornamented in a verse section.
            if (note == kHatClosed || note == kHatOpen) continue;
            // Round to the NEAREST 16th: the data-derived groove microtiming
            // (timingMs up to ±9 ms) plus bounded jitter (±15 ms) can pull an
            // event scheduled at a 16th boundary into the previous tick under
            // a floor division — rounding makes the signature deterministic.
            const int64_t rounded = blockStartSample + meta.samplePosition + 3000;
            const int bar = static_cast<int>(rounded / kSamplesPerBar);
            const int tick = static_cast<int>((rounded % kSamplesPerBar) / 6000);
            if (bar >= 0 && bar < kTotalBars && tick >= 0 && tick < 16)
                barSigs[bar].insert({ note, tick });
        }
        blockStartSample += block;
    }
    proc.playActive.store(false, std::memory_order_release);

    for (int bar = 0; bar < kTotalBars; ++bar)
        REQUIRE_FALSE(barSigs[bar].empty());

    // Phrase hold (item 3): VERSE#1's first groove (1-bar pattern 2) is held
    // for exactly 2 bars — no per-bar cycling.
    REQUIRE(barSigs[9] == barSigs[10]);

    // Rotation (item 2): the second phrase of VERSE#1 differs (22 vs 2)...
    REQUIRE(barSigs[11] != barSigs[9]);
    // ...and the previous groove is never repeated (VERSE#2 rotates too).
    REQUIRE(barSigs[15] != barSigs[13]);

    // Per-instance seed (item 3): VERSE#2 (entry bar 13) differs from VERSE#1
    // (entry bar 9) — a repeated section is NOT the same bars again.
    REQUIRE(barSigs[13] != barSigs[9]);

    // Last bar of each section is a fill (item 4) — differs from the groove.
    REQUIRE(barSigs[12] != barSigs[11]);
    REQUIRE(barSigs[16] != barSigs[15]);

    proc.releaseResources();
}

// ── Post-lock transition bass: the bass must LEAVE the old riff ───────────────
TEST_CASE("Processor pipeline: bass leaves the locked riff during a post-lock transition", "[integration][pipeline][transition][lock]")
{
    // After a Record-riff lock expires into a contrast section, the bass must move
    // WITH the drums to the new section (play its harmony), not keep looping the
    // recorded riff. Regression: the learner stayed Locked and autonomously
    // looped the riff, so the bass emitted only the frozen riff note (C2=36) over
    // the transition drums.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();
    if (auto* p = proc.getApvts().getParameter("lockBars"))
        p->setValueNotifyingHost(0.0f);  // 4-bar lock
    if (auto* p = proc.getApvts().getParameter("transitionBars"))
        p->setValueNotifyingHost(p->getNormalisableRange().convertTo0to1(4.0f));
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

    // While the guitarist plays a sustained tone (different pitch to the frozen
    // C2 riff), the bass plays the NEW section's harmony — it must NOT keep
    // emitting the frozen riff note 36.
    std::set<int> bassNotes;
    bool anyBass = false;
    for (int b = 0; b < static_cast<int>(4.0 * sr / block); ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
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
    REQUIRE(anyBass);                      // the section bass still sounds
    REQUIRE(bassNotes.count(36) == 0);     // but NOT the frozen riff note C2

    proc.releaseResources();
}

// ── Play-mode bass: reflects the guitarist's root in the song's key ───────────
TEST_CASE("Processor pipeline: play-mode bass follows the guitarist's root", "[integration][pipeline][play][bass]")
{
    // In Play mode the bass must stay anchored to the guitarist's root (so it
    // sounds in-key and follows the guitar), even when the learned riff is held.
    // Regression: when the learner locks a riff, the bass played the riff's notes
    // note-for-note, which could pull it out of the song's key.
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    proc.setCustomSongForm("VERSE:8");
    proc.playActive.store(true, std::memory_order_release);

    int blockIdx = 0;
    constexpr int cycle = 24;
    std::set<int> bassNotes;
    bool anyBass = false;
    const int totalBlocks = static_cast<int>(8.0 * 96000.0 / block);
    for (int b = 0; b < totalBlocks; ++b)
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
            if (msg.isNoteOn() && msg.getChannel() == 2)
            {
                bassNotes.insert(msg.getNoteNumber());
                anyBass = true;
            }
        }
        ++blockIdx;
    }

    // Bass present and anchored on the guitarist's C2 root (36), in the song's key.
    REQUIRE(anyBass);
    REQUIRE(bassNotes.count(36) > 0);

    proc.playActive.store(false, std::memory_order_release);
    proc.releaseResources();
}
