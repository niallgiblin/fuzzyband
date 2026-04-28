#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"
#include "inference/IStructureInference.h"
#include "inference/RuleBasedInference.h"
#include "inference/RuleStructureInference.h"
#include "midi/BassMidiValidator.h"
#if defined(MA_ENABLE_ONNX)
#include "inference/OnnxInference.h"
#include "inference/OnnxStructureInference.h"
#endif
#include <chrono>
#include <cmath>

namespace
{
constexpr float kMinPitchConfidence = 0.35f;

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

    juce::StringArray genreChoices;
    genreChoices.add("Metal");
    genreChoices.add("Metalcore");
    genreChoices.add("Death");
    genreChoices.add("Progressive");
    genreChoices.add("Black");
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "genre", 1 },
        "Genre",
        genreChoices,
        0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "intensity", 1 },
        "Intensity",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.001f },
        0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "variation", 1 },
        "Variation",
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
    energyAnalyser.prepare(sr, samplesPerBlock);
    structureTagger.prepare(sr);
    pitchEstimator.prepare(sr, samplesPerBlock);
    pitchEstimator.reset();
    heldPitchRootMidi = 40.0f;
    heldPitchConfidence = 0.0f;
    pitchHoldValid = false;
    pitchStableCounterSamples = 0;
    lastStablePitchMidi = 40.0f;
    lastBassUpdateSample = -1;
    lastDrumPatternChangeSample = -1;
    countInOnsetCount = 0;
    countInComplete = false;
    countInLastBeatSample = -1;

    patternPlayer.prepare(sr, samplesPerBlock);
    patternPlayer.reset();

    if (inference)
        inference->prepare(sr);

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
            // Midpoint blend (PUI-01): when ML is invalid, follow rules only; else t>=0.5 uses ML shadow state.
            if (!m.mlValid)
                effective = rule;
            else
                effective = (structureBlend >= 0.5f) ? m.smoothedState : rule;
        }
        patternFeatures.state = effective;

        const int idx = inference->selectPattern(patternFeatures);

        // 2-bar hold guard for drum pattern index: 8 beats * 60/bpm * sampleRate samples.
        const double srDrum = cachedSampleRate.load(std::memory_order_acquire);
        const float bpmNowDrum = latest.bpm > 0.0f ? latest.bpm : 120.0f;
        const int64_t twoBarsInSamplesDrum =
            static_cast<int64_t>(8.0 * 60.0 / static_cast<double>(bpmNowDrum) * srDrum);
        const bool drumHoldExpired =
            (lastDrumPatternChangeSample < 0) ||
            (latest.sampleTimestamp - lastDrumPatternChangeSample >= twoBarsInSamplesDrum);

        if (drumHoldExpired)
        {
            latestPatternIndex.store(idx, std::memory_order_release);
            lastDrumPatternChangeSample = latest.sampleTimestamp;
        }
        // Always update display so UI shows inference intent even during hold.
        displayPatternIndex.store(idx, std::memory_order_relaxed);

        int generativeMode = 0;
        if (auto* modeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("generativeBassMode")))
            generativeMode = modeParam->getIndex();

        if (bassInference)
        {
            bassInference->propose(latest);

            const bool silencePolicy =
                (latest.state == StructureState::SILENT) || (latest.rmsEnergy < 1.0e-6f);
            const auto& p = bassInference->getLastProposal();
            const bool valid = BassMidiValidator::validateBassProposal(
                p.rootMidi,
                p.durationBeats,
                latest.state,
                silencePolicy);

            constexpr float kMinGenerativeConfidence = 0.5f;
            constexpr float kLibraryBassScore = 0.45f;
            float scoreGen = 0.f;
            if (valid && p.margin >= 0.f && p.confidence >= kMinGenerativeConfidence)
                scoreGen = p.confidence;

            // 2-bar hold guard: 8 beats * 60/bpm * sampleRate samples.
            const double sr2 = cachedSampleRate.load(std::memory_order_acquire);
            const float bpmNow2 = latest.bpm > 0.0f ? latest.bpm : 120.0f;
            const int64_t twoBarsInSamples =
                static_cast<int64_t>(8.0 * 60.0 / static_cast<double>(bpmNow2) * sr2);
            const bool bassHoldExpired =
                (lastBassUpdateSample < 0) ||
                (latest.sampleTimestamp - lastBassUpdateSample >= twoBarsInSamples);

            bool useGen = false;
            if (generativeMode == 2)
            {
                useGenerativeBass.store(false, std::memory_order_release);
            }
            else if (generativeMode == 1)
            {
                useGen = valid;
                useGenerativeBass.store(useGen, std::memory_order_release);
                if (useGen && bassHoldExpired)
                {
                    generativeBassRootNote.store(
                        static_cast<int>(std::lround(static_cast<double>(p.rootMidi))),
                        std::memory_order_release);
                    generativeBassDurationBeats.store(p.durationBeats, std::memory_order_release);
                    lastBassUpdateSample = latest.sampleTimestamp;
                }
                else if (useGen)
                {
                    // Hold period not expired — keep existing root note.
                    useGenerativeBass.store(true, std::memory_order_release);
                }
            }
            else
            {
                useGen = scoreGen > kLibraryBassScore;
                useGenerativeBass.store(useGen, std::memory_order_release);
                if (useGen && bassHoldExpired)
                {
                    generativeBassRootNote.store(
                        static_cast<int>(std::lround(static_cast<double>(p.rootMidi))),
                        std::memory_order_release);
                    generativeBassDurationBeats.store(p.durationBeats, std::memory_order_release);
                    lastBassUpdateSample = latest.sampleTimestamp;
                }
                else if (useGen)
                {
                    // Hold period not expired — keep existing root note.
                    useGenerativeBass.store(true, std::memory_order_release);
                }
            }
        }
        else
        {
            useGenerativeBass.store(false, std::memory_order_release);
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
    const float centroid = energyAnalyser.getSpectralCentroid();
    const float hfFlux = energyAnalyser.getHighFreqFlux();
    const StructureState st = structureTagger.update(rms, centroid, hfFlux, numSamples, energyAnalyser.getPeakRms());

    pitchEstimator.process(in, numSamples);
    const float rawMidi = pitchEstimator.getMidiNote();
    const float rawConf = pitchEstimator.getConfidence();

    const bool digitalSilence = (rms < 1.0e-6f);
    const bool silencePolicy = (st == StructureState::SILENT) || digitalSilence;

    if (silencePolicy)
    {
        heldPitchRootMidi = 40.0f;
        heldPitchConfidence = 0.0f;
        pitchHoldValid = false;
    }
    else if (rawConf < kMinPitchConfidence)
    {
        if (!pitchHoldValid)
        {
            heldPitchRootMidi = 40.0f;
            heldPitchConfidence = 0.0f;
        }
    }
    else
    {
        heldPitchRootMidi = rawMidi;
        heldPitchConfidence = rawConf;
        pitchHoldValid = true;
    }

    // Count-in gate: require 4 beat-spaced onsets before drums start.
    // Onsets must be at least half a beat apart so rapid strumming doesn't
    // satisfy the count instantly — the guitarist has to establish tempo first.
    if (st == StructureState::SILENT)
    {
        onsetDetector.resetTempoLock();
        countInOnsetCount = 0;
        countInComplete = false;
        countInLastBeatSample = -1;
        lastDrumPatternChangeSample = -1;
    }
    else if (!countInComplete)
    {
        if (onsetDetector.onsetDetectedThisBlock())
        {
            const float bpmEst = onsetDetector.getCurrentBpm();
            const double sr = cachedSampleRate.load(std::memory_order_relaxed);
            // Require at least half a beat between counted onsets (generous — allows early 2x speed detection)
            const int64_t halfBeatSamples = static_cast<int64_t>(
                0.5 * 60.0 / static_cast<double>(bpmEst > 0.f ? bpmEst : 120.f) * sr);
            if (countInLastBeatSample < 0 ||
                (hostSampleTime - countInLastBeatSample) >= halfBeatSamples)
            {
                ++countInOnsetCount;
                countInLastBeatSample = hostSampleTime;
            }
        }
        if (countInOnsetCount >= 4)
        {
            countInComplete = true;
            patternPlayer.snapToBarStart();
        }
    }

    FeatureVector fv;
    fv.bpm = onsetDetector.getCurrentBpm();
    fv.rmsEnergy = rms;
    fv.spectralCentroid = centroid;
    fv.highFreqFlux = hfFlux;
    fv.state = st;
    fv.sampleTimestamp = hostSampleTime;
    fv.pitchRootMidi = heldPitchRootMidi;
    fv.pitchConfidence = heldPitchConfidence;

    if (auto* rawIntensity = apvts.getRawParameterValue("intensity"))
        fv.policyIntensity = rawIntensity->load();
    if (auto* rawVariation = apvts.getRawParameterValue("variation"))
        fv.policyVariation = rawVariation->load();

    if (fv.pitchConfidence <= 0.0f || fv.pitchConfidence < kMinPitchConfidence)
    {
        pitchStableCounterSamples = 0;
        patternPlayer.setBassSemitoneOffset(0);
    }
    else
    {
        const float currentMidi = fv.pitchRootMidi;
        // Compare pitch class so YIN octave flips (E2↔E3) don't reset the counter.
        const int currentPc = (static_cast<int>(std::round(currentMidi)) % 12 + 12) % 12;
        const int lastPc    = (static_cast<int>(std::round(lastStablePitchMidi)) % 12 + 12) % 12;
        if (currentPc == lastPc)
        {
            pitchStableCounterSamples += numSamples;
        }
        else
        {
            pitchStableCounterSamples = 0;
            lastStablePitchMidi = currentMidi;
        }

        // One beat duration in samples at current BPM.
        const float bpmNow = onsetDetector.getCurrentBpm();
        const int oneBeatSamples = (bpmNow > 0.0f)
            ? static_cast<int>((60.0f / bpmNow) * static_cast<float>(cachedSampleRate.load(std::memory_order_relaxed)))
            : static_cast<int>(cachedSampleRate.load(std::memory_order_relaxed) / 2);

        if (pitchStableCounterSamples >= oneBeatSamples)
        {
            // Map detected pitch class to nearest semitone offset from kBassRoot (MIDI 40 = E2),
            // clamped to ±6 so bass stays in register regardless of which octave the guitar played.
            const int rounded    = static_cast<int>(std::lround(static_cast<double>(currentMidi)));
            const int pc         = (rounded % 12 + 12) % 12;
            const int bassRootPc = 40 % 12; // 4 = E
            int delta = pc - bassRootPc;
            if (delta > 6)  delta -= 12;
            if (delta < -6) delta += 12;
            patternPlayer.setBassSemitoneOffset(delta);
        }
        // else: hold the previous offset — do not call setBassSemitoneOffset.
    }

    (void)featureQueue.try_enqueue(fv);

    const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
    patternPlayer.setBpm(onsetDetector.getCurrentBpm());
    patternPlayer.setPatternIndex(patternIdx);

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    const bool previewAudible = previewRem > 0;
    if (previewAudible)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);

    patternPlayer.setStructureSilent((st == StructureState::SILENT || !countInComplete) && !previewAudible);

    if (silencePolicy)
        patternPlayer.setGenerativeBassActive(false, 40, 1.0f);
    else if (useGenerativeBass.load(std::memory_order_acquire))
        patternPlayer.setGenerativeBassActive(
            true,
            generativeBassRootNote.load(std::memory_order_acquire),
            generativeBassDurationBeats.load(std::memory_order_acquire));
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

    displayBpm.store(onsetDetector.getCurrentBpm(), std::memory_order_relaxed);
    displayStateIndex.store(static_cast<int>(st), std::memory_order_relaxed);
    displayPatternIndex.store(patternIdx, std::memory_order_relaxed);
    displayRms.store(rms, std::memory_order_relaxed);
    displayCentroid.store(centroid, std::memory_order_relaxed);
    displayHfFlux.store(hfFlux, std::memory_order_relaxed);
}

void AccompanimentProcessor::bumpDebugPattern()
{
    const int n = patternLibrary.patternCount();
    const int next = (latestPatternIndex.load(std::memory_order_relaxed) + 1) % n;
    const int dur = juce::jmax(1, static_cast<int>(std::round(5.0 * cachedSampleRate.load(std::memory_order_acquire))));
    debugPreviewSamplesRemaining.store(dur, std::memory_order_release);
    latestPatternIndex.store(next, std::memory_order_release);
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
