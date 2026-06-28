#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"
#include "inference/RuleBasedInference.h"
#include "inference/pattern_rules.h"
#if defined(MA_ENABLE_ONNX)
#include "inference/OnnxInference.h"
#endif
#include <chrono>
#include <cmath>

namespace
{

std::unique_ptr<IInference> makeInference()
{
#if defined(MA_ENABLE_ONNX)
    auto onnx = std::make_unique<OnnxInference>();
    if (onnx->tryLoadModel())
        return onnx;
#endif
    return std::make_unique<RuleBasedInference>();
}

PatternPlayer::TransitionFillKind chooseTransitionFillKind(StructureState from,
                                                           StructureState to,
                                                           int targetPatternIndex) noexcept
{
    if (targetPatternIndex == 6)
        return PatternPlayer::TransitionFillKind::BreakdownOrImpact;
    if (from == StructureState::SILENT && to != StructureState::SILENT)
        return PatternPlayer::TransitionFillKind::Entry;
    if (from == StructureState::SOFT && to == StructureState::LOUD)
        return PatternPlayer::TransitionFillKind::BuildUp;
    if (from == StructureState::LOUD && to == StructureState::SOFT)
        return PatternPlayer::TransitionFillKind::Release;
    if (from == to && to != StructureState::SILENT)
        return PatternPlayer::TransitionFillKind::BreakdownOrImpact;
    return PatternPlayer::TransitionFillKind::None;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AccompanimentProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "outputGain", 1 },
        "Output Gain",
        juce::NormalisableRange<float>{ 0.0f, 2.0f, 0.01f },
        1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "intensity", 1 },
        "Intensity",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.001f },
        0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "bpm", 1 },
        "Tempo (BPM)",
        juce::NormalisableRange<float>{ 40.0f, 300.0f, 1.0f },
        120.0f));

    juce::StringArray songFormChoices;
    for (const auto& preset : StructureSequencer::getPresets())
        songFormChoices.add(preset.name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "songForm", 1 },
        "Song form",
        songFormChoices,
        0));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "loop", 1 },
        "Loop song form",
        true));

    return layout;
}

AccompanimentProcessor::AccompanimentProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::mono(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
    , inference(makeInference())
{
    activeInferenceName = inference ? inference->getName() : "None";
    patternPlayer.setPatternLibrary(&patternLibrary);
    inferenceRunning.store(true, std::memory_order_release);
    inferenceThread = std::thread([this] { inferenceLoop(); });
}

AccompanimentProcessor::~AccompanimentProcessor()
{
    inferenceRunning.store(false, std::memory_order_release);
    if (inferenceThread.joinable())
        inferenceThread.join();
}

bool AccompanimentProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return (mainIn == juce::AudioChannelSet::mono() && mainOut == juce::AudioChannelSet::stereo());
}

void AccompanimentProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    inferencePaused.store(true, std::memory_order_release);

    const double sr = (sampleRate > 0.0) ? sampleRate : 44100.0;
    cachedSampleRate.store(sr, std::memory_order_release);
    energyAnalyser.prepare(sr, samplesPerBlock);
    structureTagger.prepare(sr);
    lastDrumPatternChangeSample = -1;
    lastCommittedStructureState = StructureState::SILENT;
    playbackGate.reset();
    prevBlockRms = 0.0f;

    patternPlayer.prepare(sr, samplesPerBlock);
    patternPlayer.reset();

    if (inference)
        inference->prepare(sr);

    useGenerativeBass.store(false, std::memory_order_release);
    PatternPlayer::GrooveCommit staleCommit{};
    while (grooveCommitQueue.try_dequeue(staleCommit)) {}

    hostSampleTime = 0;
    latestPatternIndex.store(0, std::memory_order_relaxed);

    inferencePaused.store(false, std::memory_order_release);
}

void AccompanimentProcessor::releaseResources()
{
    inferencePaused.store(true, std::memory_order_release);
}

void AccompanimentProcessor::drainFeatureQueueAndRunInference()
{
    if (resetDrumHoldRequested.exchange(false, std::memory_order_acq_rel))
    {
        lastDrumPatternChangeSample = -1;
        lastCommittedStructureState = StructureState::SILENT;
        displayStateIndex.store(static_cast<int>(StructureState::SILENT), std::memory_order_relaxed);
    }

    FeatureVector latest{};
    bool got = false;

    while (true)
    {
        FeatureVector tmp{};
        if (!featureQueue.try_dequeue(tmp))
            break;
        latest = tmp;
        got = true;
    }

    if (got && inference && debugPreviewSamplesRemaining.load(std::memory_order_acquire) <= 0)
    {
        const double sr = cachedSampleRate.load(std::memory_order_acquire);

        // Use rule-based state directly — no shadow structure blending
        const StructureState effective = latest.state;
        FeatureVector patternFeatures = latest;

        // D-23-04/D-23-05: Rejection signal — single-shot exclusion with hold-guard bypass
        const int rejectionCount = patternRejectionCount.load(std::memory_order_acquire);
        const int currentPat = latestPatternIndex.load(std::memory_order_acquire);
        int excludeParam = -1;
        if (rejectionCount > 0)
        {
            patternRejectionCount.store(rejectionCount - 1, std::memory_order_release);
            excludeParam = currentPat;
        }

        const int idx = inference->selectPattern(patternFeatures, excludeParam);
        PatternPlayer::GrooveCommit commit{};
        commit.patternIndex = currentPat;
        bool hasGrooveCommit = false;
        bool acceptedDrumPattern = false;

        // 2-bar hold guard for drum pattern index
        const float bpmNowDrum = latest.bpm > 0.0f ? latest.bpm : 120.0f;
        const int64_t twoBarsInSamplesDrum =
            static_cast<int64_t>(8.0 * 60.0 / static_cast<double>(bpmNowDrum) * sr);
        const bool drumHoldExpired =
            (lastDrumPatternChangeSample < 0) ||
            (latest.sampleTimestamp - lastDrumPatternChangeSample >= twoBarsInSamplesDrum);

        const bool transitionEvent = std::abs(latest.rmsDelta) > 0.6f;

        const int64_t samplesPerBar =
            static_cast<int64_t>(4.0 * 60.0 / static_cast<double>(bpmNowDrum) * sr);
        const int64_t barsSinceChange =
            (lastDrumPatternChangeSample < 0 || samplesPerBar <= 0) ? 0
            : (latest.sampleTimestamp - lastDrumPatternChangeSample) / samplesPerBar;
        const bool autoChangeReady = (barsSinceChange >= 4);

        const int64_t samplesPerBarDiv = static_cast<int64_t>(4.0 * 60.0 / static_cast<double>(latest.bpm) * sr);
        const int barMod8 = (samplesPerBarDiv > 0) ? static_cast<int>((latest.sampleTimestamp / samplesPerBarDiv) % 8) : 0;
        const int diversifiedIdx = PatternRules::diversifyPattern(idx, latest, barMod8);

        if (drumHoldExpired || excludeParam >= 0 || transitionEvent || autoChangeReady)
        {
            commit.patternIndex = diversifiedIdx;
            commit.fillKind = chooseTransitionFillKind(lastCommittedStructureState, patternFeatures.state, diversifiedIdx);
            commit.hasBassFrame = false;  // bass handled by audio thread
            hasGrooveCommit = true;
            acceptedDrumPattern = true;
        }
        displayPatternIndex.store(diversifiedIdx, std::memory_order_relaxed);

        if (hasGrooveCommit && grooveCommitQueue.try_enqueue(commit))
        {
            if (acceptedDrumPattern)
            {
                latestPatternIndex.store(diversifiedIdx, std::memory_order_release);
                lastDrumPatternChangeSample = latest.sampleTimestamp;
                lastCommittedStructureState = patternFeatures.state;
            }
        }
    }
}

void AccompanimentProcessor::inferenceLoop()
{
    while (inferenceRunning.load(std::memory_order_acquire))
    {
        if (inferencePaused.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(inferenceDrainMutex);
            drainFeatureQueueAndRunInference();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void AccompanimentProcessor::pauseBackgroundInferenceForTests()
{
    inferencePaused.store(true, std::memory_order_release);
}

void AccompanimentProcessor::flushBackgroundInferenceForTests()
{
    std::lock_guard<std::mutex> lock(inferenceDrainMutex);
    drainFeatureQueueAndRunInference();
}

void AccompanimentProcessor::resumeBackgroundInferenceForTests()
{
    inferencePaused.store(false, std::memory_order_release);
}

juce::String AccompanimentProcessor::getSectionName() const noexcept
{
    if (structureSequencer.isComplete())
        return "Complete";
    return juce::String(structureSequencer.getCurrentSectionName())
        + " | Bar " + juce::String(structureSequencer.getBarsElapsed() + 1)
        + "/" + juce::String(structureSequencer.getBarsInSection())
        + " | Pat " + juce::String(getDisplayPatternIndex());
}

uint64_t AccompanimentProcessor::getOnnxErrorCount() const noexcept
{
    uint64_t total = 0;
#if defined(MA_ENABLE_ONNX)
    if (auto* onnx = dynamic_cast<OnnxInference*>(inference.get()))
        total += onnx->getLoadErrorCount() + onnx->getRunErrorCount();
#endif
    return total;
}

void AccompanimentProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;
    float* in = buffer.getWritePointer(0);
    if (in == nullptr) return;

    // Scrub non-finite samples
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = in[i];
        if (!std::isfinite(s)) in[i] = 0.0f;
    }
    juce::FloatVectorOperations::clip(in, in, -2.0f, 2.0f, numSamples);

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;

    // ── 1. Energy analysis ──────────────────────────────────────────────────
    energyAnalyser.process(in, numSamples);
    audioRingBuffer.write(in, numSamples);

    const float rms = energyAnalyser.getRmsEnergy();
    const float rmsDelta = (rms - prevBlockRms) / std::max(prevBlockRms, 1.0e-6f);
    prevBlockRms = rms;
    const float centroid = energyAnalyser.getSpectralCentroid();
    const float hfFlux = energyAnalyser.getHighFreqFlux();
    structureTagger.setSubBassRatio(energyAnalyser.getSubBassRatio());
    const StructureState st = structureTagger.update(rms, centroid, hfFlux, numSamples, energyAnalyser.getPeakRms());

    const bool digitalSilence = (rms < 1.0e-6f);
    const double sr = cachedSampleRate.load(std::memory_order_relaxed);

    // ── 2. BPM (knob or DAW transport) ──────────────────────────────────────
    float bpmForPlayer = 120.0f;
    if (auto* rawBpm = apvts.getRawParameterValue("bpm"))
        bpmForPlayer = rawBpm->load();
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getBpm())
                bpmForPlayer = static_cast<float>(*t);
        }
    }

    // ── 3. Song form / loop change detection ────────────────────────────────
    {
        static int lastSongFormIdx = -1;
        if (auto* rawForm = apvts.getRawParameterValue("songForm"))
        {
            const int idx = static_cast<int>(std::round(rawForm->load()));
            if (idx != lastSongFormIdx)
            {
                structureSequencer.loadPreset(idx);
                lastSongFormIdx = idx;
            }
        }
    }
    {
        static bool lastLoopValue = true;
        if (auto* rawLoop = apvts.getRawParameterValue("loop"))
        {
            const bool newLoop = rawLoop->load() > 0.5f;
            if (newLoop != lastLoopValue)
            {
                structureSequencer.setLooping(newLoop);
                lastLoopValue = newLoop;
            }
        }
    }

    // ── 4. Playback gate ────────────────────────────────────────────────────
    const GateDecision gd = playbackGate.update(st, 0.0f, false, false, 0.0f, numSamples, sr);
    if (gd.resetTrackers)
    {
        playbackGate.reset();
        resetDrumHoldRequested.store(true, std::memory_order_release);
    }
    if (gd.snapBeatNow)
    {
        patternPlayer.snapBpm(bpmForPlayer);
        patternPlayer.snapToBarStart();
    }

    // ── 5. Enqueue FeatureVector ────────────────────────────────────────────
    FeatureVector fv;
    fv.bpm = bpmForPlayer;
    fv.rmsEnergy = rms;
    fv.spectralCentroid = centroid;
    fv.highFreqFlux = hfFlux;
    fv.state = st;
    fv.sampleTimestamp = hostSampleTime;
    fv.pitchRootMidi = 40.0f;   // fixed E2
    fv.pitchConfidence = 0.0f;
    fv.rmsDelta = rmsDelta;
    fv.subBassRatio = energyAnalyser.getSubBassRatio();
    if (auto* rawIntensity = apvts.getRawParameterValue("intensity"))
        fv.policyIntensity = rawIntensity->load();
    (void)featureQueue.try_enqueue(fv);

    // ── 6. Pattern playback ─────────────────────────────────────────────────
    const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
    patternPlayer.setBpm(bpmForPlayer);

    const bool playOn = playActive.load(std::memory_order_acquire);
    if (playOn && structureSequencer.isComplete())
        playActive.store(false, std::memory_order_release);

    int effectivePatternIdx = patternIdx;
    if (playOn)
    {
        structureSequencer.advance(numSamples, bpmForPlayer, sr);
        if (!structureSequencer.isComplete())
        {
            const auto* secName = structureSequencer.getCurrentSectionName();
            auto pool = PatternRules::sectionPatternPool(secName);
            if (pool.count > 0)
            {
                const int bar = structureSequencer.getGlobalBarCount();
                const bool isLast = structureSequencer.isLastBar();
                effectivePatternIdx = isLast
                    ? PatternRules::selectFillPattern(0)
                    : pool.indices[bar % pool.count];
            }
        }
    }
    patternPlayer.setPatternIndex(effectivePatternIdx);

    // ── 7. Silence gating ───────────────────────────────────────────────────
    const bool audioActive = (rms > 0.001f);
    const bool trulySilent = (!playOn && !audioActive) || digitalSilence;

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    if (previewRem > 0)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);
    patternPlayer.setStructureSilent(trulySilent && previewRem <= 0);

    // Snap to bar start when Play just toggled on
    {
        static bool wasOff = true;
        if (playOn && wasOff)
        {
            patternPlayer.snapBpm(bpmForPlayer);
            patternPlayer.snapToBarStart();
        }
        wasOff = !playOn;
    }

    // ── 8. Dequeue groove commit + bass ─────────────────────────────────────
    PatternPlayer::GrooveCommit commit{};
    bool gotCommit = false;
    while (grooveCommitQueue.try_dequeue(commit)) gotCommit = true;

    if (playOn && !gotCommit)
    {
        commit.patternIndex = effectivePatternIdx;
        gotCommit = true;
    }

    // ── 9. Bass generation (fixed root C2, section-aware) ───────────────────
    const float bassRoot = 36.0f;  // C2
    const char* bassSection = playOn ? structureSequencer.getCurrentSectionName() : "VERSE";
    PatternPlayer::GrooveCommit bassCommit = RuleBasedBass::generate(
        bassRoot, bassSection, playOn && structureSequencer.isFirstBar(), bpmForPlayer);

    if (gotCommit)
    {
        bassCommit.patternIndex = commit.patternIndex;
        bassCommit.fillKind = commit.fillKind;
    }
    else
    {
        bassCommit.patternIndex = effectivePatternIdx;
    }
    commit = bassCommit;
    gotCommit = true;

    useGenerativeBass.store(true, std::memory_order_release);
    if (gotCommit)
    {
        patternPlayer.setGenerativeBassActive(true, static_cast<int>(bassRoot), 1.0f);
        patternPlayer.queueGrooveCommit(commit);
    }

    // ── 10. Process MIDI ────────────────────────────────────────────────────
    int64_t hostPos = hostSampleTime;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getTimeInSamples())
                hostPos = *t;
        }
    }
    patternPlayer.process(midi, numSamples, hostPos);

    // ── 11. Gain passthrough ────────────────────────────────────────────────
    float gain = 1.0f;
    if (auto* raw = apvts.getRawParameterValue("outputGain"))
        gain = raw->load();
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = in[i] * gain;
        outL[i] = s;
        if (outR != outL) outR[i] = s;
    }

    hostSampleTime += numSamples;

    // Display updates
    displayBpm.store(bpmForPlayer, std::memory_order_relaxed);
    displayRms.store(rms, std::memory_order_relaxed);
    displayCentroid.store(centroid, std::memory_order_relaxed);
    displayHfFlux.store(hfFlux, std::memory_order_relaxed);
    displayStateIndex.store(static_cast<int>(st), std::memory_order_relaxed);
    displayPatternIndex.store(effectivePatternIdx, std::memory_order_relaxed);
}

void AccompanimentProcessor::bumpDebugPattern()
{
    const int dur = juce::jmax(1, static_cast<int>(std::round(5.0 * cachedSampleRate.load(std::memory_order_acquire))));
    debugPreviewSamplesRemaining.store(dur, std::memory_order_release);
    // D-23-04: drive rejection signal instead of directly cycling index
    patternRejectionCount.fetch_add(1, std::memory_order_release);
}

void AccompanimentProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Ensure parameters set via setValue() (e.g. by a host during automation) are
    // reflected in the APVTS state tree. JUCE's APVTS only tracks changes made via
    // setValueNotifyingHost(), so we re-notify here before serialising.
    for (auto* param : getParameters())
        param->setValueNotifyingHost(param->getValue());

    juce::MemoryOutputStream mos(destData, true);
    apvts.copyState().writeToStream(mos);
}

void AccompanimentProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ValueTree tree = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (tree.isValid())
        apvts.replaceState(tree);
}

void AccompanimentProcessor::processBlockBypassed(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();
    for (int ch = 1; ch <= 16; ++ch)
        midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
    patternPlayer.reset();
    playbackGate.reset();

    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() >= 2 && n > 0)
    {
        const float* l = buffer.getReadPointer(0);
        float* r = buffer.getWritePointer(1);
        juce::FloatVectorOperations::copy(r, l, n);
    }
}

juce::AudioProcessorEditor* AccompanimentProcessor::createEditor()
{
    return new AccompanimentEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AccompanimentProcessor();
}
