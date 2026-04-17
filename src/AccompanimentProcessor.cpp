#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"
#include "inference/IStructureInference.h"
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
        latestPatternIndex.store(idx, std::memory_order_release);

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

            bool useGen = false;
            if (generativeMode == 2)
            {
                useGenerativeBass.store(false, std::memory_order_release);
            }
            else if (generativeMode == 1)
            {
                useGen = valid;
                useGenerativeBass.store(useGen, std::memory_order_release);
                if (useGen)
                {
                    generativeBassRootNote.store(
                        static_cast<int>(std::lround(static_cast<double>(p.rootMidi))),
                        std::memory_order_release);
                    generativeBassDurationBeats.store(p.durationBeats, std::memory_order_release);
                }
            }
            else
            {
                useGen = scoreGen > kLibraryBassScore;
                useGenerativeBass.store(useGen, std::memory_order_release);
                if (useGen)
                {
                    generativeBassRootNote.store(
                        static_cast<int>(std::lround(static_cast<double>(p.rootMidi))),
                        std::memory_order_release);
                    generativeBassDurationBeats.store(p.durationBeats, std::memory_order_release);
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
    if (auto* genreParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("genre")))
        fv.policyGenreIndex = genreParam->getIndex();

    if (fv.pitchConfidence <= 0.0f || fv.pitchConfidence < kMinPitchConfidence)
        patternPlayer.setBassSemitoneOffset(0);
    else
    {
        const int rounded = static_cast<int>(std::lround(static_cast<double>(fv.pitchRootMidi)));
        patternPlayer.setBassSemitoneOffset(rounded - 40);
    }

    (void)featureQueue.try_enqueue(fv);

    const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
    patternPlayer.setBpm(onsetDetector.getCurrentBpm());
    patternPlayer.setPatternIndex(patternIdx);

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    const bool previewAudible = previewRem > 0;
    if (previewAudible)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);

    patternPlayer.setStructureSilent(st == StructureState::SILENT && !previewAudible);

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
