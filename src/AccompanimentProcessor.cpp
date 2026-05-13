#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"
#include "inference/IStructureInference.h"
#include "inference/RuleBasedInference.h"
#include "inference/RuleStructureInference.h"
#include "inference/pattern_rules.h"
#if defined(MA_ENABLE_ONNX)
#include "inference/OnnxInference.h"
#include "inference/OnnxStructureInference.h"
#endif
#include <chrono>
#include <climits>
#include <cmath>

namespace
{

float foldTrackerAliasTowardOnset(float trackerBpm, float onsetBpm) noexcept
{
    float reconciled = juce::jlimit(80.0f, 220.0f, trackerBpm);
    const float anchor = juce::jlimit(80.0f, 220.0f, onsetBpm);

    if (anchor <= 0.0f)
        return reconciled;

    while (reconciled / anchor > 1.55f && reconciled * 0.5f >= 80.0f)
        reconciled *= 0.5f;

    while (reconciled / anchor < 0.65f && reconciled * 2.0f <= 220.0f)
        reconciled *= 2.0f;

    return reconciled;
}

void maBeatFluxSink(void* user, float flux, int64_t totalSamples) noexcept
{
    if (user != nullptr)
        static_cast<BeatTracker*>(user)->pushFluxSample(flux, totalSamples);
}

std::unique_ptr<IInference> makeInference()
{
#if defined(MA_ENABLE_ONNX)
    auto onnx = std::make_unique<OnnxInference>();
    if (onnx->tryLoadModel())
        return onnx;
#endif
    return std::make_unique<RuleBasedInference>();
}

std::unique_ptr<IStructureInference> makeStructureInference()
{
#if defined(MA_ENABLE_ONNX)
    auto onnx = std::make_unique<OnnxStructureInference>();
    if (onnx->tryLoadModel())
        return onnx;
#endif
    return std::make_unique<RuleStructureInference>();
}

#if defined(MA_ENABLE_ONNX)
std::unique_ptr<OnnxBassInference> makeBassInference()
{
    auto onnx = std::make_unique<OnnxBassInference>();
    if (onnx->tryLoadModel())
        return onnx;
    return nullptr;
}
#else
std::unique_ptr<OnnxBassInference> makeBassInference()
{
    return nullptr;
}
#endif
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
        juce::ParameterID{ "structureBlend", 1 },
        "Structure: blend ML vs rules",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.001f },
        0.5f));

    juce::StringArray bassModeChoices;
    bassModeChoices.add("Auto");
    bassModeChoices.add("On");
    bassModeChoices.add("Off");
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "generativeBassMode", 1 },
        "Generative bass",
        bassModeChoices,
        0));

    return layout;
}

AccompanimentProcessor::AccompanimentProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::mono(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
    , inference(makeInference())
    , structureInference(makeStructureInference())
    , bassInference(makeBassInference())
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
    // D-03: Pause inference so it doesn't read components mid-reprepare.
    inferencePaused.store(true, std::memory_order_release);

    const double sr = (sampleRate > 0.0) ? sampleRate : 44100.0;
    cachedSampleRate.store(sr, std::memory_order_release);
    onsetDetector.prepare(sr, samplesPerBlock);
    onsetDetector.setFluxSink(&beatTracker, maBeatFluxSink);
    beatTracker.prepare(sr, samplesPerBlock);
    energyAnalyser.prepare(sr, samplesPerBlock);
    structureTagger.prepare(sr);
    pitchEstimator.prepare(sr, samplesPerBlock);
    pitchEstimator.reset();
    stablePitchTracker.reset();
    lastBassUpdateSample = -1;
    lastDrumPatternChangeSample = -1;
    playbackGate.reset();
    tempoStabiliser.reset();
    prevBlockRms = 0.0f;
    prevStructureSilent = false;

    patternPlayer.prepare(sr, samplesPerBlock);
    patternPlayer.reset();

    if (inference)
        inference->prepare(sr);
    featureCapture.prepare(sr, samplesPerBlock, activeInferenceName);

    lastFeatureSampleTs = -1;
    if (structureInference)
        structureInference->prepare(sr);

    if (bassInference)
        bassInference->prepare(sr);
    useGenerativeBass.store(false, std::memory_order_release);

    hostSampleTime = 0;
    latestPatternIndex.store(0, std::memory_order_relaxed);

    // D-03: Resume — the inference thread was started in the constructor and is still running.
    inferencePaused.store(false, std::memory_order_release);
}

void AccompanimentProcessor::releaseResources()
{
    inferencePaused.store(true, std::memory_order_release);
}

void AccompanimentProcessor::drainFeatureQueueAndRunInference()
{
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
        double dtSec = 0.02;
        const double sr = cachedSampleRate.load(std::memory_order_acquire);
        if (lastFeatureSampleTs >= 0 && latest.sampleTimestamp >= lastFeatureSampleTs && sr > 0.0)
            dtSec = static_cast<double>(latest.sampleTimestamp - lastFeatureSampleTs) / sr;

        if (structureInference)
        {
            structureInference->process(latest, dtSec);
            lastFeatureSampleTs = latest.sampleTimestamp;
        }

        FeatureVector patternFeatures = latest;
        float structureBlend = 0.5f;
        if (auto* rawBlend = apvts.getRawParameterValue("structureBlend"))
            structureBlend = rawBlend->load();

        const StructureState rule = latest.state;
        StructureState effective = rule;
        if (structureInference)
        {
            const auto& m = structureInference->getLastShadowMetrics();
            // Rule silence owns the audio gate. ONNX can shape non-silent sections only when explicitly favored.
            if (!m.mlValid || rule == StructureState::SILENT || m.smoothedState == StructureState::SILENT)
                effective = rule;
            else
                effective = (structureBlend > 0.5f) ? m.smoothedState : rule;
        }
        patternFeatures.state = effective;

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
        const int ruleIdx = PatternRules::rulePatternForState(patternFeatures);
        const bool onnxAvailable = (activeInferenceName.find("ONNX") != std::string::npos);

        // 2-bar hold guard for drum pattern index: 8 beats * 60/bpm * sampleRate samples.
        const double srDrum = cachedSampleRate.load(std::memory_order_acquire);
        const float bpmNowDrum = latest.bpm > 0.0f ? latest.bpm : 120.0f;
        const int64_t twoBarsInSamplesDrum =
            static_cast<int64_t>(8.0 * 60.0 / static_cast<double>(bpmNowDrum) * srDrum);
        const bool drumHoldExpired =
            (lastDrumPatternChangeSample < 0) ||
            (latest.sampleTimestamp - lastDrumPatternChangeSample >= twoBarsInSamplesDrum);

        const bool transitionEvent = std::abs(latest.rmsDelta) > 0.6f;

        const int64_t samplesPerBar =
            static_cast<int64_t>(4.0 * 60.0 / static_cast<double>(bpmNowDrum) * srDrum);
        const int64_t barsSinceChange =
            (lastDrumPatternChangeSample < 0 || samplesPerBar <= 0) ? 0
            : (latest.sampleTimestamp - lastDrumPatternChangeSample) / samplesPerBar;
        const bool autoChangeReady = (barsSinceChange >= 8);

        if (drumHoldExpired || excludeParam >= 0 || transitionEvent || autoChangeReady)
        {
            latestPatternIndex.store(idx, std::memory_order_release);
            lastDrumPatternChangeSample = latest.sampleTimestamp;
        }
        // Always update display so UI shows inference intent even during hold.
        displayPatternIndex.store(idx, std::memory_order_relaxed);

        if (featureCapture.isCapturing())
        {
            FeatureCaptureRow row{};
            row.bpm = latest.bpm;
            row.rmsEnergy = latest.rmsEnergy;
            row.spectralCentroid = latest.spectralCentroid;
            row.highFreqFlux = latest.highFreqFlux;
            row.state = patternFeatures.state;
            row.sampleTimestamp = latest.sampleTimestamp;
            row.pitchRootMidi = latest.pitchRootMidi;
            row.pitchConfidence = latest.pitchConfidence;
            row.policyIntensity = latest.policyIntensity;
            row.rmsDelta = latest.rmsDelta;
            row.elapsedSeconds = (sr > 0.0) ? static_cast<double>(latest.sampleTimestamp) / sr : 0.0;
            row.rulePatternIndex = ruleIdx;
            row.activePatternIndex = idx;
            row.onnxPatternIndex = onnxAvailable ? idx : -1;
            row.onnxAvailable = onnxAvailable;
            row.rulePatternName = patternLibrary.getPattern(ruleIdx).name;
            row.activePatternName = patternLibrary.getPattern(idx).name;
            row.onnxPatternName = onnxAvailable ? patternLibrary.getPattern(idx).name : std::string{};
            row.modelMode = activeInferenceName;
            featureCapture.tryPush(row);
        }

        int generativeMode = 0;
        if (auto* modeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("generativeBassMode")))
            generativeMode = modeParam->getIndex();

        if (bassInference)
        {
            bassInference->propose(latest);
            const auto& p = bassInference->getLastProposal();

            const bool silencePolicy =
                (latest.state == StructureState::SILENT) || (latest.rmsEnergy < 1.0e-6f);

            // 2-bar hold guard for bass: 8 beats * 60/bpm * sampleRate samples.
            const double sr2 = cachedSampleRate.load(std::memory_order_acquire);
            const float bpmNow2 = latest.bpm > 0.0f ? latest.bpm : 120.0f;
            const int64_t twoBarsInSamples =
                static_cast<int64_t>(8.0 * 60.0 / static_cast<double>(bpmNow2) * sr2);
            const bool bassHoldExpired =
                (lastBassUpdateSample < 0) ||
                (latest.sampleTimestamp - lastBassUpdateSample >= twoBarsInSamples);

            bool useGen = false;
            if (generativeMode == 2) // Off
            {
                useGenerativeBass.store(false, std::memory_order_release);
                genBassStepsReady.store(false, std::memory_order_release);
            }
            else if (p.valid && !silencePolicy && bassHoldExpired)
            {
                // Deliver piano-roll steps to audio thread
                for (int i = 0; i < 16; ++i)
                {
                    genBassPitchOffsets[i] = p.pitchOffset[i];
                    genBassVelocities[i] = p.velocity[i];
                }
                genBassRootMidi = latest.pitchRootMidi;
                genBassStepsReady.store(true, std::memory_order_release);
                useGenerativeBass.store(true, std::memory_order_release);
                lastBassUpdateSample = latest.sampleTimestamp;
                useGen = true;
            }
            else if (p.valid && !silencePolicy)
            {
                // Hold period not expired — keep existing steps active
                useGenerativeBass.store(true, std::memory_order_release);
                useGen = true;
            }
            else
            {
                useGenerativeBass.store(false, std::memory_order_release);
                genBassStepsReady.store(false, std::memory_order_release);
            }

            if (generativeMode == 1) // On — force on regardless of validity
            {
                useGenerativeBass.store(useGen, std::memory_order_release);
                if (!useGen)
                    genBassStepsReady.store(false, std::memory_order_release);
            }
        }
        else
        {
            useGenerativeBass.store(false, std::memory_order_release);
            genBassStepsReady.store(false, std::memory_order_release);
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

void AccompanimentProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    midi.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    float* in = buffer.getWritePointer(0);
    if (in == nullptr)
        return;

    // Scrub non-finite samples first; SIMD clip() alone does not reliably remove NaN.
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = in[i];
        if (!std::isfinite(s))
            in[i] = 0.0f;
    }
    juce::FloatVectorOperations::clip(in, in, -2.0f, 2.0f, numSamples);

    // in and outL alias channel 0; gain pass-through reads/writes the same buffer.
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;

    onsetDetector.process(in, numSamples);
    energyAnalyser.process(in, numSamples);

    const float rms = energyAnalyser.getRmsEnergy();
    const float rmsDelta = (rms - prevBlockRms) / std::max(prevBlockRms, 1.0e-6f);
    prevBlockRms = rms;
    const float centroid = energyAnalyser.getSpectralCentroid();
    const float hfFlux = energyAnalyser.getHighFreqFlux();
    const StructureState st = structureTagger.update(rms, centroid, hfFlux, numSamples, energyAnalyser.getPeakRms());

    pitchEstimator.process(in, numSamples);
    const float rawMidi = pitchEstimator.getMidiNote();
    const float rawConf = pitchEstimator.getConfidence();

    const bool digitalSilence = (rms < 1.0e-6f);
    const bool silencePolicy = (st == StructureState::SILENT) || digitalSilence;

    const bool beatTrackerLocked = beatTracker.isLocked();
    const float onsetBpm = onsetDetector.getCurrentBpm();
    const float tempoCandidateBpm = beatTrackerLocked
        ? foldTrackerAliasTowardOnset(beatTracker.getBpm(), onsetBpm)
        : onsetBpm;
    const double sr = cachedSampleRate.load(std::memory_order_relaxed);
    const float stablePlaybackBpm = tempoStabiliser.update(tempoCandidateBpm, playbackGate.isGateOpen(),
                                                            numSamples, sr);
    const float bpmForPlayer = stablePlaybackBpm;

    const GateDecision gd = playbackGate.update(
        st,
        beatTracker.getBeatPhase01(),
        beatTracker.isLocked(),
        onsetDetector.isTempoLocked(),
        beatTracker.getConfidence(),
        numSamples,
        sr);

    if (gd.armCrash)
        patternPlayer.armTransitionCrash();
    if (gd.resetTrackers)
    {
        onsetDetector.resetTempoLock();
        beatTracker.reset();
        tempoStabiliser.reset();
        lastDrumPatternChangeSample = -1;
    }
    if (gd.snapBeatNow)
    {
        patternPlayer.snapBpm(bpmForPlayer);
        patternPlayer.snapToBarStart();
    }

    FeatureVector fv;
    fv.bpm = bpmForPlayer;
    fv.rmsEnergy = rms;
    fv.spectralCentroid = centroid;
    fv.highFreqFlux = hfFlux;
    fv.state = st;
    fv.sampleTimestamp = hostSampleTime;
    fv.pitchRootMidi = rawMidi;
    fv.pitchConfidence = rawConf;
    fv.rmsDelta = rmsDelta;

    if (auto* rawIntensity = apvts.getRawParameterValue("intensity"))
        fv.policyIntensity = rawIntensity->load();

    {
        const int semitoneOffset = stablePitchTracker.update(
            rawMidi, rawConf, bpmForPlayer, numSamples, sr, silencePolicy);
        if (semitoneOffset != INT_MIN)
            patternPlayer.setBassSemitoneOffset(semitoneOffset);
        else if (silencePolicy)
            patternPlayer.setBassSemitoneOffset(0);
    }

    (void)featureQueue.try_enqueue(fv);

    const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
    patternPlayer.setBpm(bpmForPlayer);
    patternPlayer.setPatternIndex(patternIdx);

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    const bool previewAudible = previewRem > 0;
    if (previewAudible)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);

    const bool trulySilent = !gd.gateOpen;
    patternPlayer.setStructureSilent(trulySilent && !previewAudible);

    if (silencePolicy)
        patternPlayer.setGenerativeBassActive(false, 40, 1.0f);
    else if (genBassStepsReady.load(std::memory_order_acquire))
    {
        patternPlayer.setGenerativeBassSteps(
            genBassPitchOffsets,
            genBassVelocities,
            genBassRootMidi);
        genBassStepsReady.store(false, std::memory_order_release);
    }
    else if (useGenerativeBass.load(std::memory_order_acquire))
        patternPlayer.setGenerativeBassActive(true,
            genBassRootMidi > 0.0f ? static_cast<int>(std::lround(genBassRootMidi)) : 40,
            1.0f);
    else
        patternPlayer.setGenerativeBassActive(false, 40, 1.0f);

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

    float gain = 1.0f;
    if (auto* raw = apvts.getRawParameterValue("outputGain"))
        gain = raw->load();
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = in[i] * gain;
        outL[i] = s;
        if (outR != outL)
            outR[i] = s;
    }

    hostSampleTime += static_cast<int64_t>(numSamples);
    prevStructureSilent = (st == StructureState::SILENT);

    displayBpm.store(bpmForPlayer, std::memory_order_relaxed);
    displayBeatConfidence.store(beatTracker.getConfidence(), std::memory_order_relaxed);
    displayStateIndex.store(static_cast<int>(st), std::memory_order_relaxed);
    displayPatternIndex.store(patternIdx, std::memory_order_relaxed);
    displayRms.store(rms, std::memory_order_relaxed);
    displayCentroid.store(centroid, std::memory_order_relaxed);
    displayHfFlux.store(hfFlux, std::memory_order_relaxed);
}

void AccompanimentProcessor::bumpDebugPattern()
{
    const int dur = juce::jmax(1, static_cast<int>(std::round(5.0 * cachedSampleRate.load(std::memory_order_acquire))));
    debugPreviewSamplesRemaining.store(dur, std::memory_order_release);
    // D-23-04: drive rejection signal instead of directly cycling index
    patternRejectionCount.fetch_add(1, std::memory_order_release);
}

void AccompanimentProcessor::setFeatureCaptureEnabled(bool enabled)
{
    if (enabled)
        (void)featureCapture.start();
    else
        featureCapture.stop();
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
    // D-04: Flush MIDI on soft bypass; pass dry guitar through like processBlock.
    midi.clear();
    for (int ch = 1; ch <= 16; ++ch)
        midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
    patternPlayer.reset();
    beatTracker.reset();
    tempoStabiliser.reset();
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
