/**
 * Integration tests for AccompanimentProcessor — full plugin pipeline.
 *
 * Uses the test-only control hooks (pause / flush / resume background inference)
 * to exercise the processor deterministically without real-time threading.
 */

#include <catch2/catch_test_macros.hpp>
#include <cmath>

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

    // 1500 Hz sine at amplitude 0.5: rmsEnergy ≈ 0.283 (> kSilentRms 0.05, < kLoudRms 0.35) → SOFT
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
    // Sludge were unreachable. Sludge (index 4) must route SOFT low-energy to the
    // metal half-time (7), not the rock set (22/23).
    const double sr = 48000.0;
    const int block = 512;
    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    if (auto* genreParam = proc.getApvts().getParameter("genre"))
        genreParam->setValueNotifyingHost(1.0f);  // normalized 1.0 → choice index 4 (Sludge)

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
    // Sludge → metal routing: SOFT low-energy → 7 (Half-Time).
    REQUIRE(pat == 7);
    proc.releaseResources();
}

TEST_CASE("Processor pipeline: generative groove lock freezes drums, expires, then listens again", "[integration][pipeline][lock]")
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

    // Phase 1: chug riff (20 quiet + 4 loud blocks, C2) → learner locks →
    // groove locks. The loud run is 4 blocks so the RMS-window rise happens
    // while the audio is still loud (pitch confidence high at the attack).
    int blockIdx = 0;
    constexpr int cycle = 24;      // 20 quiet + 4 loud
    for (int b = 0; b < static_cast<int>(2.5 * sr / block) && !proc.isGrooveLocked(); ++b)
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
        ++blockIdx;
    }
    REQUIRE(proc.isGrooveLocked());
    const int frozenPattern = proc.getLatestPatternIndex();

    // Phase 2a: sustained input (no attacks → not the riff) for ~4s while
    // locked — the committed drum pattern must NOT change, even though the
    // listener would normally re-evaluate.
    feed(0.05, 400.0, static_cast<int>(4.0 * sr / block), blockIdx);
    REQUIRE(proc.isGrooveLocked());
    REQUIRE(proc.getLatestPatternIndex() == frozenPattern);

    // Phase 2b: keep feeding past the 4-bar hold (8s) → lock expires and the
    // stale learner must NOT re-engage (no riff activity for > grace).
    feed(0.05, 400.0, static_cast<int>(7.0 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());

    // Phase 3: listening again — the pattern is no longer frozen. A pattern
    // rejection now takes effect deterministically: excluding the *current*
    // pattern forces the next committed pattern to differ (exclusion applies
    // when the rule agrees, the rule result differs otherwise).
    const int preRejection = proc.getLatestPatternIndex();
    proc.patternRejectionCount.store(1, std::memory_order_release);
    feed(0.05, 400.0, static_cast<int>(0.5 * sr / block), blockIdx);
    REQUIRE(proc.getLatestPatternIndex() != preRejection);

    // Phase 4: silence keeps the lock released (and resets the learner).
    feed(0.0, 65.0, static_cast<int>(0.5 * sr / block), blockIdx);
    REQUIRE_FALSE(proc.isGrooveLocked());

    proc.releaseResources();
}

// ─── P0 groove-lock rework (R1/R2/R4) ─────────────────────────────────────────

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

    // Phase 1: chug C2 until the groove locks.
    for (int b = 0; b < static_cast<int>(3.0 * sr / block) && !proc.isGrooveLocked(); ++b)
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
        ++blockIdx;
    }
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

    // Phase 1: chug C2 until locked.
    for (int b = 0; b < static_cast<int>(3.0 * sr / block) && !proc.isGrooveLocked(); ++b)
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
        ++blockIdx;
    }
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

TEST_CASE("Editor construction smoke test", "[integration][editor]")
{
    juce::MessageManager::getInstance();
    {
        AccompanimentProcessor proc;
        proc.prepareToPlay(48000.0, 512);
        juce::AudioProcessorEditor* ed = proc.createEditor();
        REQUIRE(ed != nullptr);
        delete ed;
        proc.releaseResources();
    }
    juce::MessageManager::deleteInstance();
}
