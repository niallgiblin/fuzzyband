#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"
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
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout AccompanimentProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "outputGain", 1 },
        "Output Gain",
        juce::NormalisableRange<float>{ 0.0f, 2.0f, 0.01f },
        1.0f));
    return layout;
}

AccompanimentProcessor::AccompanimentProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::mono(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
    , inference(makeInference())
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
    patternPlayer.prepare(sr, samplesPerBlock);
    patternPlayer.reset();

    if (inference)
        inference->prepare(sr);

    hostSampleTime = 0;
    latestPatternIndex.store(0, std::memory_order_relaxed);

    // D-03: Resume — the inference thread was started in the constructor and is still running.
    inferencePaused.store(false, std::memory_order_release);
}

void AccompanimentProcessor::releaseResources()
{
    inferencePaused.store(true, std::memory_order_release);
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
            const int idx = inference->selectPattern(latest);
            latestPatternIndex.store(idx, std::memory_order_release);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
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
    const StructureState st = structureTagger.update(rms, centroid, hfFlux, numSamples);

    FeatureVector fv;
    fv.bpm = onsetDetector.getCurrentBpm();
    fv.rmsEnergy = rms;
    fv.spectralCentroid = centroid;
    fv.highFreqFlux = hfFlux;
    fv.state = st;
    fv.sampleTimestamp = hostSampleTime;
    (void)featureQueue.try_enqueue(fv);

    const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
    patternPlayer.setBpm(onsetDetector.getCurrentBpm());
    patternPlayer.setPatternIndex(patternIdx);

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    const bool previewAudible = previewRem > 0;
    if (previewAudible)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);

    patternPlayer.setStructureSilent(st == StructureState::SILENT && !previewAudible);

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
