#include "AccompanimentProcessor.h"
#include "AccompanimentEditor.h"
#include "inference/RuleBasedInference.h"
#include "inference/pattern_rules.h"
#if defined(MA_ENABLE_ONNX)
#include "inference/MetalGrooveInference.h"
#endif
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>

namespace
{

std::unique_ptr<IInference> makeInference()
{
#if defined(MA_ENABLE_ONNX)
    // Mel-CNN is the sole production model; rule-based is the only fallback.
    auto groove = std::make_unique<MetalGrooveInference>();
    if (groove->tryLoadModel())
        return groove;
#endif
    return std::make_unique<RuleBasedInference>();
}

/** @brief Consecutive same-style windows required before style steering engages
 *  (~150 ms at the ~50 Hz drain). The 2-bar commit hold does the real smoothing. */
constexpr int kStyleStableWindows = 3;

PatternPlayer::TransitionFillKind chooseTransitionFillKind(StructureState from,
                                                           StructureState to,
                                                           int targetPatternIndex,
                                                           float rmsEnergy,
                                                           float rmsDelta) noexcept
{
    if (targetPatternIndex == 6)
        return PatternPlayer::TransitionFillKind::BreakdownOrImpact;
    if (from == StructureState::SILENT && to != StructureState::SILENT)
        return PatternPlayer::TransitionFillKind::Entry;

    // Energy-aware fills (Phase 2): a sudden loud hit wants an impact fill, a
    // gradual rise a build-up; same-state transitions get a subtle impact only
    // when genuinely loud (keeps reactive mode's embellishment "only a little").
    if (from == StructureState::SOFT && to == StructureState::LOUD)
    {
        if (rmsDelta > 0.8f || rmsEnergy > 0.5f)
            return PatternPlayer::TransitionFillKind::BreakdownOrImpact;
        return PatternPlayer::TransitionFillKind::BuildUp;
    }
    if (from == StructureState::LOUD && to == StructureState::SOFT)
        return PatternPlayer::TransitionFillKind::Release;
    if (from == to && to != StructureState::SILENT)
        return (rmsEnergy > 0.35f)
            ? PatternPlayer::TransitionFillKind::BreakdownOrImpact
            : PatternPlayer::TransitionFillKind::None;
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
        juce::ParameterID{ "bpm", 1 },
        "Tempo (BPM)",
        juce::NormalisableRange<float>{ 40.0f, 300.0f, 1.0f },
        120.0f));

    juce::StringArray genreChoices;
    for (int i = 0; i < Groove::presetCount(); ++i)
        genreChoices.add(Groove::presetFor(i).name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "genre", 1 },
        "Genre",
        genreChoices,
        0));  // Rock-first default (B1)

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "swing", 1 },
        "Swing",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.01f },
        0.0f));

    // Bass register: shift the bass MIDI an octave to fit range-limited bass
    // VSTs. MIDI 36 = C2 in standard pitch (some VSTs display it as C1 with the
    // middle-C=C3 convention), so a "too low" bass is usually the instrument's
    // range, not the plugin — this control compensates.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "bassTranspose", 1 },
        "Bass octave",
        juce::StringArray{ "-12 (down)", "0 (normal)", "+12 (up)" },
        1));  // default: normal

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

    // Generative groove lock: how many bars the auto-lock holds after the last
    // moment the riff was being played (returning to the riff extends it).
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "lockBars", 1 },
        "Groove lock (bars)",
        4, 64, 16));

    // Post-lock transition grammar (A5.2): how long each transition section
    // holds, and how many distinct sections to visit before returning to the riff.
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "transitionBars", 1 },
        "Transition (bars)",
        2, 32, 8));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "transitionSections", 1 },
        "Transition sections",
        1, 4, 2));

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
    pitchEstimator.prepare(sr, samplesPerBlock);
    stablePitchTracker.reset();
    phraseLearner.prepare(sr);
    lastDrumPatternChangeSample = -1;
    lastCommittedStructureState = StructureState::SILENT;
    playbackGate.reset();
    prevBlockRms = 0.0f;
    riffCapturePhase = RiffCapturePhase::Idle;
    riffCountInStartBeat = 0.0;
    liveGridPhase = LiveGridPhase::Idle;
    liveGridStartBeat = 0.0;
    riffCaptureActive.store(false, std::memory_order_relaxed);
    riffCaptureNoteCount.store(0, std::memory_order_relaxed);
    riffCaptureBar.store(0, std::memory_order_relaxed);
    liveListenActive.store(false, std::memory_order_relaxed);
    liveListenBar.store(0, std::memory_order_relaxed);
    liveListenNotes.store(0, std::memory_order_relaxed);
    riffHeld.store(false, std::memory_order_relaxed);

    // A5.2 post-lock transition state + display scope.
    postLockPhase = PostLockPhase::Idle;
    transitionStartSample = -1;
    transitionEndSample = -1;
    transitionSectionNumberLocal = 0;
    transitionSectionActive.store(false, std::memory_order_relaxed);
    transitionSectionName.store("VERSE", std::memory_order_relaxed);
    transitionBarsRemaining.store(0, std::memory_order_relaxed);
    transitionBarsTotal.store(0, std::memory_order_relaxed);
    transitionSectionNumber.store(0, std::memory_order_relaxed);
    scopeWriteIndex.store(0, std::memory_order_relaxed);
    playheadFraction.store(0.0f, std::memory_order_relaxed);

    patternPlayer.prepare(sr, samplesPerBlock);
    patternPlayer.reset();

    // Pre-size the mel-window scratch buffer here (off the audio thread) so the
    // first ready window in processBlock() can never trigger a heap allocation on
    // the real-time path.
    audioRingBuffer.reset();
    melScratch.assign(static_cast<size_t>(audioRingBuffer.getWindowSize()), 0.0f);

    if (inference)
        inference->prepare(sr);

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

        // Generative groove lock: while the riff is locked, the drum pattern is
        // frozen — no selection, no commits. Queues still drain upstream (stale
        // features are dropped), so nothing blocks or accumulates unbounded.
        if (grooveLocked.load(std::memory_order_acquire)
            || transitionSectionActive.load(std::memory_order_acquire))
            return;

        // Use rule-based state directly — no shadow structure blending
        const StructureState effective = latest.state;
        FeatureVector patternFeatures = latest;

        // ── Mel-driven inference (v0.8.0) ───────────────────────────
        const int rejectionCount = patternRejectionCount.load(std::memory_order_acquire);
        const int currentPat = latestPatternIndex.load(std::memory_order_acquire);
        int excludeParam = -1;
        if (rejectionCount > 0)
        {
            patternRejectionCount.store(rejectionCount - 1, std::memory_order_release);
            excludeParam = currentPat;
        }

        int idx = 0;
        bool usedMelPath = false;
        int styleNow = 4;  // perception class; 4 = silence (no steering)
#if defined(MA_ENABLE_ONNX)
        {
            MelWindow latestMel{};
            bool gotMel = false;
            while (true)
            {
                MelWindow tmp{};
                if (!melQueue.try_dequeue(tmp)) break;
                latestMel = tmp;
                gotMel = true;
            }

            if (gotMel)
            {
                if (auto* groove = dynamic_cast<MetalGrooveInference*>(inference.get()))
                {
                    // Seed variety by the current bar number so the preferred
                    // groove stays dominant but the drums vary bar-to-bar (the
                    // ML "alters slightly" the best fit).
                    const double bpmSafe = latest.bpm > 0.0f ? latest.bpm : 120.0f;
                    const double spbSeed = 4.0 * 60.0 / bpmSafe * sr;
                    const int melSeed = (spbSeed > 0.0)
                        ? static_cast<int>(latest.sampleTimestamp / spbSeed) : 0;
                    idx = groove->selectPatternFromMel(latestMel.data.data(), excludeParam, melSeed);
                    styleNow = groove->classifyStyle(latestMel.data.data());
                    displayStyle.store(styleNow, std::memory_order_relaxed);
                    usedMelPath = true;
                }
            }
        }
#endif

        if (usedMelPath && !PatternRules::isPatternCompatibleWithState(idx, latest.state))
        {
            // The Mel-CNN maps arbitrary audio (including silence and unseen
            // timbres) to the nearest centroid, which can be structurally wrong
            // (e.g. "Chorus Blast" while SILENT). Only honor the mel result when
            // it is compatible with the current structure state; otherwise fall
            // back to the rule-based selection.
            usedMelPath = false;
        }

        if (!usedMelPath)
        {
            // Fallback: scalar-feature inference or rule-based
            idx = inference->selectPattern(patternFeatures, excludeParam);
        }
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
        int genreId = 0;
        if (auto* rawGenre = apvts.getRawParameterValue("genre"))
            genreId = juce::jlimit(0, Groove::presetCount() - 1,
                                   static_cast<int>(std::lround(rawGenre->load())));
        const int diversifiedIdx = PatternRules::diversifyPatternForGenre(idx, latest, barMod8, genreId);

        // Style steering (perception-layer wiring, A4.1): when the style head
        // has classified a *stable* non-silence articulation, route the groove
        // family through the style pool so how you play (chug → half-time/
        // breakdown, open chord → chorus, single-note → fast, sustain → sparse)
        // drives selection. Stability gate: kStyleStableWindows consecutive
        // windows of the same class; the 2-bar commit hold below does the rest
        // of the smoothing, so steering only biases the next eligible commit.
        int finalIdx = diversifiedIdx;
        if (styleNow >= 0 && styleNow <= 3)
        {
            if (styleNow == lastStyleIndex)
                ++styleStabilityCount;
            else
            {
                lastStyleIndex = styleNow;
                styleStabilityCount = 1;
            }
            if (styleStabilityCount >= kStyleStableWindows)
                finalIdx = PatternRules::diversifyPatternForStyle(
                    diversifiedIdx, styleNow, barMod8, latest.state, genreId);
        }
        else
        {
            lastStyleIndex = 4;
            styleStabilityCount = 0;
        }

        if (drumHoldExpired || excludeParam >= 0 || transitionEvent || autoChangeReady)
        {
            commit.patternIndex = finalIdx;
            commit.fillKind = chooseTransitionFillKind(lastCommittedStructureState, patternFeatures.state,
                                                       finalIdx, patternFeatures.rmsEnergy, patternFeatures.rmsDelta);
            hasGrooveCommit = true;
            acceptedDrumPattern = true;
        }
        displayPatternIndex.store(finalIdx, std::memory_order_relaxed);

        if (hasGrooveCommit && grooveCommitQueue.try_enqueue(commit))
        {
            if (acceptedDrumPattern)
            {
                latestPatternIndex.store(finalIdx, std::memory_order_release);
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
    if (auto* groove = dynamic_cast<MetalGrooveInference*>(inference.get()))
        total += groove->getLoadErrorCount() + groove->getRunErrorCount();
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

    // ── 0. Display scope feed (audio thread → UI, decimated) ────────────────
    // Every 8th input sample is pushed into a small rolling ring so the editor
    // can draw a live waveform. Decimation keeps the audio-thread cost trivial.
    {
        const float* p = in;
        const int n = numSamples;
        for (int i = 0; i < n; i += 8)
        {
            const int idx = scopeWriteIndex.fetch_add(1, std::memory_order_relaxed);
            scopeSamples[static_cast<size_t>(idx % kScopeSize)] = p[i];
        }
    }

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;

    // ── 1. Energy analysis ──────────────────────────────────────────────────
    energyAnalyser.process(in, numSamples);
    pitchEstimator.process(in, numSamples);
    audioRingBuffer.write(in, numSamples);

    // ── 1b. Mel spectrogram extraction (when window ready) ─────────────────
    if (audioRingBuffer.isWindowReady())
    {
        // melScratch is pre-sized in prepareToPlay(); no allocation on the audio thread.
        if (audioRingBuffer.readWindow(melScratch.data()) > 0)
        {
            MelWindow mw{};
            if (melExtractor.process(melScratch.data(), mw.data.data()))
                melQueue.try_enqueue(mw);
        }
    }

    const float rms = energyAnalyser.getRmsEnergy();
    // Normalised RMS delta with a floor guard: prevents the ratio exploding when
    // emerging from silence (prevBlockRms ≈ 0). Silence→sound transitions are
    // handled by drum-hold expiry + SILENT→non-SILENT fill selection, not rmsDelta.
    const float rmsDelta = (prevBlockRms > 1.0e-3f)
        ? (rms - prevBlockRms) / prevBlockRms
        : 0.0f;
    prevBlockRms = rms;
    const float centroid = energyAnalyser.getSpectralCentroid();
    const float hfFlux = energyAnalyser.getHighFreqFlux();
    const float subBassRatio = energyAnalyser.getSubBassRatio();
    structureTagger.setSubBassRatio(subBassRatio);
    const StructureState st = structureTagger.update(rms, centroid, hfFlux, numSamples, energyAnalyser.getPeakRms());

    const bool digitalSilence = (rms < 1.0e-6f);
    const double sr = cachedSampleRate.load(std::memory_order_relaxed);

    // ── 2. BPM (DAW transport only) ─────────────────────────────────────────
    // The DAW transport is the single source of tempo. The manual knob is used
    // only as a fallback when the host provides no transport BPM (e.g. the
    // standalone build, which has no playhead). Audio-derived tempo is not used.
    float bpmForPlayer = 120.0f;
    int64_t rawHostPos = hostSampleTime;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getBpm())
                bpmForPlayer = static_cast<float>(*t);
            if (auto ts = pos->getTimeInSamples())
                rawHostPos = *ts;
        }
    }
    if (!(bpmForPlayer > 0.0f) || bpmForPlayer > 1000.0f)
    {
        if (auto* rawBpm = apvts.getRawParameterValue("bpm"))
            bpmForPlayer = rawBpm->load();
    }
    if (!(bpmForPlayer > 0.0f) || bpmForPlayer > 1000.0f)
        bpmForPlayer = 120.0f;

    // ── 3. Song form / loop change detection ────────────────────────────────
    // Phase 2: the editable form arrives as a parsed SongForm handed off by
    // setCustomSongForm (message thread). The audio thread observes the version
    // bump and loads it; this replaces the old preset-index polling.
    {
        const int v = songFormVersion.load(std::memory_order_acquire);
        if (v != loadedSongFormVersion)
        {
            loadedSongFormVersion = v;
            if (auto form = std::atomic_load(&pendingSongForm))
                structureSequencer.loadForm(*form);
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
    const GateDecision gd = playbackGate.update(st, numSamples, sr);
    if (gd.resetTrackers)
    {
        playbackGate.reset();
        resetDrumHoldRequested.store(true, std::memory_order_release);
    }
    if (gd.armCrash)
        patternPlayer.armTransitionCrash();

    // ── 5. Enqueue FeatureVector ────────────────────────────────────────────
    FeatureVector fv;
    fv.bpm = bpmForPlayer;
    fv.rmsEnergy = rms;
    fv.spectralCentroid = centroid;
    fv.highFreqFlux = hfFlux;
    fv.state = st;
    fv.sampleTimestamp = hostSampleTime;
    fv.pitchRootMidi = pitchEstimator.getMidiNote();
    fv.pitchConfidence = pitchEstimator.getConfidence();
    fv.rmsDelta = rmsDelta;
    fv.policyIntensity = 0.5f;
    fv.subBassRatio = subBassRatio;
    (void)featureQueue.try_enqueue(fv);

    // ── 6. Pattern playback ─────────────────────────────────────────────────
    const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
    patternPlayer.setBpm(bpmForPlayer);

    // Genre preset (B1) + swing knob (A2.3).
    int genreId = 0;
    if (auto* rawGenre = apvts.getRawParameterValue("genre"))
        genreId = juce::jlimit(0, Groove::presetCount() - 1,
                               static_cast<int>(std::lround(rawGenre->load())));
    patternPlayer.setGenrePreset(genreId);

    float swing = 0.0f;
    if (auto* rawSwing = apvts.getRawParameterValue("swing"))
        swing = juce::jlimit(0.0f, 1.0f, rawSwing->load());
    patternPlayer.setSwing(swing);

    // Bass octave transposition: -12 / 0 / +12 semitones on the bass output.
    // The raw APVTS value is the choice index (0, 1, 2) for "-12 / 0 / +12".
    int bassTranspose = 0;
    if (auto* rawTranspose = apvts.getRawParameterValue("bassTranspose"))
    {
        constexpr int kNumChoices = 3;  // -12, 0, +12
        const int idx = juce::jlimit(0, kNumChoices - 1,
                                     static_cast<int>(std::lround(rawTranspose->load())));
        bassTranspose = (idx - 1) * 12;
    }
    patternPlayer.setBassSemitoneOffset(bassTranspose);

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
            auto pool = PatternRules::sectionPatternPoolForGenre(secName, genreId);
            if (pool.count > 0)
            {
                const int secIndex = structureSequencer.getCurrentSectionIndex();
                const int barsElapsedNow = structureSequencer.getBarsElapsed();
                // Reseed on section change, play start, and loop/restart (bars
                // elapsed wrapping), so each section instance — including each
                // repeat of a looped form — starts a different rotation.
                if (secIndex != lastSectionIndex || !wasPlayOn
                    || barsElapsedNow < lastSeenBarsElapsed)
                {
                    lastSectionIndex = secIndex;
                    sectionEntryBar = structureSequencer.getGlobalBarCount();
                    lastPlayedPoolPattern = -1;
                    lastRotationSlot = -1;
                    cachedPoolPick = -1;
                }
                lastSeenBarsElapsed = barsElapsedNow;
                const int bar = structureSequencer.getGlobalBarCount();
                const bool isLast = structureSequencer.isLastBar();
                const unsigned seed = static_cast<unsigned>(sectionEntryBar)
                                    ^ static_cast<unsigned>(secIndex * 31u);
                if (isLast)
                {
                    // Last bar of the section: an energy-sized, seed-varied fill
                    // (item 4) instead of the always-big fill.
                    effectivePatternIdx = PatternRules::selectFillPattern(0, rms, seed);
                }
                else
                {
                    // Phrased rotation: hold each groove barsPerGroove bars, pick
                    // per seed+phrase slot, never repeat the previous groove.
                    // Computed ONCE per phrase slot (this branch runs every
                    // block) so both bars of a phrase share the pick and the
                    // exclusion only fires between phrases.
                    const int barsPerGroove = PatternRules::barsPerGrooveForSection(secName);
                    const int grooveSlot = (bar - sectionEntryBar) / barsPerGroove;
                    if (grooveSlot != lastRotationSlot)
                    {
                        const int picked = PatternRules::pickPoolPattern(
                            pool, seed, grooveSlot, lastPlayedPoolPattern);
                        if (picked >= 0)
                        {
                            cachedPoolPick = picked;
                            lastPlayedPoolPattern = picked;
                        }
                        lastRotationSlot = grooveSlot;
                    }
                    if (cachedPoolPick >= 0)
                        effectivePatternIdx = cachedPoolPick;
                }
            }
        }
    }
    else if (postLockPhase == PostLockPhase::TransitionHold)
    {
        // A5.2: during a post-lock transition, rotate through the chosen
        // section's pool on bar boundaries (like play-mode pools, but driven by
        // the transition grammar instead of the song form).
        if (transitionPool.count > 0)
        {
            const int64_t samplesPerBarT =
                static_cast<int64_t>(4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
            const int64_t barsElapsed = (samplesPerBarT > 0 && transitionStartSample >= 0)
                ? (hostSampleTime - transitionStartSample) / samplesPerBarT
                : 0;
            // Pick once per phrase slot (this branch runs every block during
            // the hold) so the exclusion only fires between phrases.
            const int barsPerGrooveT = PatternRules::barsPerGrooveForSection(transitionSectionNameStr);
            const int grooveSlotT = static_cast<int>(barsElapsed) / barsPerGrooveT;
            if (grooveSlotT != lastTransitionSlot)
            {
                const int pickedT = PatternRules::pickPoolPattern(
                    transitionPool, 0u, grooveSlotT, lastPlayedPoolPattern);
                if (pickedT >= 0)
                {
                    cachedTransitionPick = pickedT;
                    lastPlayedPoolPattern = pickedT;
                }
                lastTransitionSlot = grooveSlotT;
            }
            if (cachedTransitionPick >= 0)
                effectivePatternIdx = cachedTransitionPick;
        }
    }
    wasPlayOn = playOn;
    patternPlayer.setPatternIndex(effectivePatternIdx);

    // Section for velocity contrast (A3.1) and bass harmony (A1.2).
    // Reactive mode maps loud playing to CHORUS so dynamics track the guitarist.
    // A5.2: during a post-lock transition, the transition section drives the
    // velocity profile so the hold sounds like a real section, not follow mode.
    const char* section = playOn ? structureSequencer.getCurrentSectionName() : "VERSE";
    if (!playOn && postLockPhase == PostLockPhase::TransitionHold)
        section = transitionSectionNameStr;
    else if (!playOn && st == StructureState::LOUD)
        section = "CHORUS";
    patternPlayer.setSection(Groove::sectionIdFromName(section));

    // ── 7. Silence gating ───────────────────────────────────────────────────
    const bool audioActive = (rms > 0.001f);
    // Item: "only starts listening at Record riff / Play" — the plugin is idle
    // (silent, not learning) until the user arms it via Play or a riff capture.
    // Once armed (song form playing, riff captured/locked, or a post-lock
    // transition running) it behaves as before. Otherwise it stays quiet.
    const bool armActive = playOn
        || riffCaptureActive.load(std::memory_order_acquire)
        || riffCaptureStart.load(std::memory_order_acquire)
        || grooveLocked.load(std::memory_order_acquire)
        || transitionSectionActive.load(std::memory_order_acquire);
    const bool trulySilent = ((!playOn && !audioActive) || digitalSilence || !armActive);

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    if (previewRem > 0)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);
    const bool capturingRiff = riffCaptureActive.load(std::memory_order_acquire)
        || riffCaptureStart.load(std::memory_order_acquire);
    patternPlayer.setClickTrack(capturingRiff);
    patternPlayer.setStructureSilent((trulySilent && previewRem <= 0) && !capturingRiff);

    // ── 8. Dequeue groove commit ───────────────────────────────────────────
    // Inference → audio thread handoff for bar-quantized pattern changes with
    // transition fills. Honored only when the sequencer is not overriding
    // pattern selection (i.e. when the Play button is off).
    PatternPlayer::GrooveCommit commit{};
    bool gotCommit = false;
    while (grooveCommitQueue.try_dequeue(commit)) gotCommit = true;

    // A queued commit is honored only when the sequencer is not overriding
    // pattern selection AND the groove is not locked (frozen drums).
    if (gotCommit && !playOn && !grooveLockActive)
        patternPlayer.queueGrooveCommit(commit);

    // ── 9. Phrase-learning bass ─────────────────────────────────────────────
    // Bass learns guitarist's riff pattern (rhythm + melody), then mirrors it.
    // Gated on the structure state: during SILENT the learner must not lock
    // onto the noise floor and mirror it endlessly (its internal rms gate is
    // far below the adaptive silence threshold).
    {
        const bool silentNow = (st == StructureState::SILENT) || digitalSilence;

        int lockBars = 16;
        if (auto* rawLockBars = apvts.getRawParameterValue("lockBars"))
        {
            lockBars = juce::jlimit(4, 64, static_cast<int>(std::lround(rawLockBars->load())));
        }
        const int64_t samplesPerBar = static_cast<int64_t>(
            4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
        const int64_t lockDuration = static_cast<int64_t>(lockBars) * samplesPerBar;
        const int64_t reEngageGrace = 2 * samplesPerBar;

        // Track the guitarist's root pitch class first so captured/mirrored
        // bass notes use the stable class, not raw YIN (which octave-flips).
        const int semitoneOffset = stablePitchTracker.update(
            pitchEstimator.getMidiNote(),
            pitchEstimator.getConfidence(),
            bpmForPlayer, numSamples, sr,
            silentNow && !riffCaptureActive.load(std::memory_order_acquire)
                      && !riffCaptureStart.load(std::memory_order_acquire));
        const int pcForBass = (semitoneOffset != INT_MIN)
            ? semitoneOffset
            : stablePitchTracker.getLastPitchClassOffset();

        const int64_t clockSample = patternPlayer.previewResolvedHostSample(rawHostPos, numSamples);
        const double samplesPerBeat = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
        const double beatStart = static_cast<double>(clockSample) / samplesPerBeat;
        const double beatEnd = beatStart + static_cast<double>(numSamples) / samplesPerBeat;

        // Scope playhead must track the SAME clock as the drums (the resolved
        // host position, not the plugin's own counter). Otherwise, when the DAW
        // transport isn't at sample 0 (or loops/seeks), the bar-aligned waveform's
        // downbeat lands off the audible drum downbeat.
        const double sppbScope = 4.0 * samplesPerBeat;
        if (sppbScope > 0.0)
            playheadFraction.store(static_cast<float>(
                std::fmod(static_cast<double>(clockSample), sppbScope) / sppbScope),
                std::memory_order_relaxed);

        float blockPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float a = std::abs(in[i]);
            if (a > blockPeak)
                blockPeak = a;
        }

        auto abortRiffCapture = [this]() noexcept
        {
            phraseLearner.cancelUserCapture();
            phraseLearner.setHoldActive(false);
            riffCaptureActive.store(false, std::memory_order_release);
            riffCaptureNoteCount.store(0, std::memory_order_relaxed);
            riffCaptureBar.store(0, std::memory_order_relaxed);
            riffCapturePhase = RiffCapturePhase::Idle;
            patternPlayer.setClickTrack(false);
        };

        auto abortLiveListen = [this]() noexcept
        {
            liveGridPhase = LiveGridPhase::Idle;
            liveGridStartBeat = 0.0;
            liveListenActive.store(false, std::memory_order_release);
            liveListenBar.store(0, std::memory_order_relaxed);
            liveListenNotes.store(0, std::memory_order_relaxed);
            // Passive listen arms gridListening_ in the learner; clear it so a
            // stale listen cannot keep the grid armed after the UI stops it.
            if (phraseLearner.isGridListening())
            {
                phraseLearner.cancelLiveGridListen();
            }
        };

        auto engageGrooveLockFromRiff = [this, lockDuration, lockBars]() noexcept
        {
            grooveLockActive = true;
            grooveLockEndSample = hostSampleTime + lockDuration;
            grooveLockStartSample = hostSampleTime;
            lockBarsTotal.store(lockBars, std::memory_order_relaxed);
            lastRiffMatchSample = hostSampleTime;
            grooveLockReleaseArmed = false;
            phraseLearner.setHoldActive(true);
        };

        auto finishRiffCapture = [this, engageGrooveLockFromRiff, abortRiffCapture]() noexcept
        {
            const bool ok = phraseLearner.commitGridCapture();
            riffCaptureActive.store(false, std::memory_order_release);
            riffCapturePhase = RiffCapturePhase::Idle;
            patternPlayer.setClickTrack(false);
            if (ok)
                engageGrooveLockFromRiff();
            else
                abortRiffCapture();
        };

        if (riffForget.exchange(false, std::memory_order_acq_rel))
        {
            abortRiffCapture();
            abortLiveListen();
            phraseLearner.reset();
            grooveLockActive = false;
            grooveLockEndSample = -1;
            grooveLockReleaseArmed = false;
            lastRiffMatchSample = std::numeric_limits<int64_t>::min() / 2;
            prevPhraseLocked = false;
            grooveLocked.store(false, std::memory_order_release);
            riffHeld.store(false, std::memory_order_release);
        }

        if (riffCaptureStart.exchange(false, std::memory_order_acq_rel))
        {
            abortLiveListen();
            phraseLearner.beginGridCapture();
            riffCaptureActive.store(true, std::memory_order_release);
            riffCaptureNoteCount.store(0, std::memory_order_relaxed);
            riffCaptureBar.store(0, std::memory_order_relaxed);
            riffCapturePhase = RiffCapturePhase::WaitBar;
            riffCountInStartBeat = 0.0;
            grooveLockActive = false;
            grooveLockEndSample = -1;
            grooveLockReleaseArmed = false;
            grooveLocked.store(false, std::memory_order_release);
            phraseLearner.setHoldActive(false);
            patternPlayer.setClickTrack(true);
        }

        const bool capturingNow = riffCaptureActive.load(std::memory_order_acquire);

        if (capturingNow)
        {
            constexpr double kBar = 4.0;
            constexpr double kCountInBeats = 4.0;
            constexpr double kRecordBeats = static_cast<double>(PhraseLearner::kGridBars) * 4.0;

            if (riffCapturePhase == RiffCapturePhase::WaitBar)
            {
                const double nextBar = std::ceil(beatStart / kBar - 1.0e-9) * kBar;
                if (nextBar < beatEnd - 1.0e-9)
                {
                    riffCountInStartBeat = nextBar;
                    riffCapturePhase = RiffCapturePhase::CountIn;
                }
            }

            if (riffCapturePhase == RiffCapturePhase::CountIn
                && beatEnd >= riffCountInStartBeat + kCountInBeats - 1.0e-9)
            {
                riffCapturePhase = RiffCapturePhase::Recording;
            }

            if (riffCapturePhase == RiffCapturePhase::Recording)
            {
                const double recStart = riffCountInStartBeat + kCountInBeats;
                const double recEnd = recStart + kRecordBeats;
                const double cap0 = juce::jmax(beatStart, recStart);
                const double cap1 = juce::jmin(beatEnd, recEnd);
                if (cap1 > cap0)
                {
                    const int bassMidi = (pcForBass != INT_MIN) ? 36 + pcForBass : 36;
                    phraseLearner.stampGridRange(
                        cap0 - recStart, cap1 - recStart, blockPeak, bassMidi);
                }

                const int bar = 1 + static_cast<int>(std::floor((cap0 - recStart) / kBar));
                riffCaptureBar.store(juce::jlimit(1, PhraseLearner::kGridBars, bar),
                                     std::memory_order_relaxed);
                riffCaptureNoteCount.store(phraseLearner.getGridOccupiedCount(),
                                           std::memory_order_relaxed);

                if (beatEnd >= recEnd - 1.0e-9)
                    finishRiffCapture();
            }
            else
            {
                riffCaptureBar.store(0, std::memory_order_relaxed);
                riffCaptureNoteCount.store(phraseLearner.getGridOccupiedCount(),
                                           std::memory_order_relaxed);
            }
        }

        if (capturingNow || playOn)
        {
            abortLiveListen();
        }
        else if (phraseLearner.isLocked())
        {
            liveGridPhase = LiveGridPhase::Idle;
            liveListenActive.store(false, std::memory_order_release);
            liveListenBar.store(0, std::memory_order_relaxed);
            liveListenNotes.store(0, std::memory_order_relaxed);
        }
        else if (armActive)
        {
            // Live grid listen / follow (only once the engine is armed — Play,
            // riff capture, or a lock). Idle does not auto-listen/auto-lock.
            constexpr double kBar = 4.0;
            constexpr double kListenBeats = static_cast<double>(PhraseLearner::kGridBars) * 4.0;
            liveListenActive.store(true, std::memory_order_release);

            if (liveGridPhase == LiveGridPhase::Idle
                || liveGridPhase == LiveGridPhase::WaitBar)
            {
                if (!silentNow)
                {
                    const double nextBar = std::ceil(beatStart / kBar - 1.0e-9) * kBar;
                    if (nextBar < beatEnd - 1.0e-9)
                    {
                        phraseLearner.beginLiveGridListen();
                        liveGridStartBeat = nextBar;
                        liveGridPhase = LiveGridPhase::Filling;
                    }
                    else
                    {
                        liveGridPhase = LiveGridPhase::WaitBar;
                    }
                }
                liveListenBar.store(0, std::memory_order_relaxed);
                liveListenNotes.store(phraseLearner.getGridOccupiedCount(),
                                      std::memory_order_relaxed);
            }

            if (liveGridPhase == LiveGridPhase::Filling)
            {
                const double recStart = liveGridStartBeat;
                const double recEnd = recStart + kListenBeats;
                const double cap0 = juce::jmax(beatStart, recStart);
                const double cap1 = juce::jmin(beatEnd, recEnd);
                if (cap1 > cap0)
                {
                    const int bassMidi = (pcForBass != INT_MIN) ? 36 + pcForBass : 36;
                    phraseLearner.stampGridRange(
                        cap0 - recStart, cap1 - recStart, blockPeak, bassMidi);
                }

                const int bar = 1 + static_cast<int>(std::floor((cap0 - recStart) / kBar));
                liveListenBar.store(juce::jlimit(1, PhraseLearner::kGridBars, bar),
                                    std::memory_order_relaxed);
                liveListenNotes.store(phraseLearner.getGridOccupiedCount(),
                                      std::memory_order_relaxed);

                if (beatEnd >= recEnd - 1.0e-9)
                {
                    // The full 4-bar listen window has elapsed. A grid lock must
                    // be backed by real rhythmic evidence: ≥8 detected attacks.
                    // A sustained drone / open chord fills every 16th slot yet is
                    // NOT a riff — committing it would freeze the groove onto a
                    // single pitch and break root-following bass (the guitarist
                    // holding a chord is not "playing in time").
                    const bool rhythmicEvidence = phraseLearner.getAttackCount() >= 8;
                    if (rhythmicEvidence
                        && phraseLearner.getGridOccupiedCount() >= 8
                        && phraseLearner.commitGridCapture())
                    {
                        engageGrooveLockFromRiff();
                        abortLiveListen();
                    }
                    else
                    {
                        phraseLearner.reset();
                        liveGridPhase = LiveGridPhase::WaitBar;
                        liveListenBar.store(0, std::memory_order_relaxed);
                        liveListenNotes.store(0, std::memory_order_relaxed);
                    }
                }
            }
        }

        // Do not wipe a user take on phrase-breath silence.
        const auto bassNote = ((silentNow || !armActive) && !capturingNow)
            ? PhraseLearner::BassNote{}
            : phraseLearner.process(
                hostSampleTime,
                rms,
                pitchEstimator.getMidiNote(),
                pitchEstimator.getConfidence(),
                bpmForPlayer,
                numSamples,
                pcForBass
            );
        // Idle (armActive false) or silence: don't keep the riff mirror learning
        // in the background — the plugin only learns once the user arms it.
        if ((silentNow || !armActive) && !capturingNow && !phraseLearner.isLocked()
            && !phraseLearner.isGridCapturing())
            phraseLearner.reset();

        if (riffCaptureStop.exchange(false, std::memory_order_acq_rel)
            && riffCaptureActive.load(std::memory_order_acquire))
        {
            if (riffCapturePhase == RiffCapturePhase::Recording
                && phraseLearner.getGridOccupiedCount() >= 2)
                finishRiffCapture();
            else
                abortRiffCapture();
        }

        const bool phraseLocked = phraseLearner.isLocked();
        riffHeld.store(phraseLocked, std::memory_order_release);

        // ── 9b. Generative groove lock ────────────────────────────────────────
        // In generative mode (Play off), when the phrase learner detects the
        // guitarist's riff repeating, the groove auto-locks: the drum pattern
        // freezes (inference commits are suppressed) and the bass keeps looping
        // the learned riff while the guitarist expands/solos over it.
        //
        // User capture: the lock is engaged only on Stop (above). Auto-lock
        // still exists as a fallback when the user does not record first.
        // P0/R2: the hold is a FIXED `lockBars`-bar window.

        if (phraseLearner.justMatchedRiff())
            lastRiffMatchSample = hostSampleTime;

        const bool phraseLockEdge = (phraseLocked && !prevPhraseLocked);
        prevPhraseLocked = phraseLocked;

        const bool capturingHeld = riffCaptureActive.load(std::memory_order_acquire);
        if (playOn)
        {
            grooveLockActive = false;
            grooveLockEndSample = -1;
            grooveLockReleaseArmed = false;
            if (capturingHeld)
            {
                phraseLearner.cancelUserCapture();
                riffCaptureActive.store(false, std::memory_order_release);
                riffCaptureNoteCount.store(0, std::memory_order_relaxed);
                riffCaptureBar.store(0, std::memory_order_relaxed);
                riffCapturePhase = RiffCapturePhase::Idle;
                patternPlayer.setClickTrack(false);
            }
        }
        else if (!capturingHeld && !grooveLockActive
                 && postLockPhase != PostLockPhase::TransitionHold)
        {
            // Engage: the riff just repeated (lock edge), or the riff was played
            // within the re-engage grace after a hold expired. Skipped while a
            // post-lock transition is playing — the transition hold is the sole
            // authority on re-locking the riff during that window.
            const bool riffFresh = (hostSampleTime - lastRiffMatchSample) < reEngageGrace;
            if (phraseLockEdge || riffFresh)
            {
                grooveLockActive = true;
                grooveLockEndSample = hostSampleTime + lockDuration;
                grooveLockStartSample = hostSampleTime;
                lockBarsTotal.store(lockBars, std::memory_order_relaxed);
                lastRiffMatchSample = hostSampleTime;
                grooveLockReleaseArmed = false;
            }
        }
        else if (!capturingHeld && grooveLockActive)
        {
            // Held: the window is fixed — release exactly when it elapses.
            if (hostSampleTime >= grooveLockEndSample)
            {
                grooveLockActive = false;
                grooveLockReleaseArmed = true;
            }
        }
        grooveLocked.store(grooveLockActive, std::memory_order_release);
        // Keep the learned riff held whenever the groove is locked OR a post-lock
        // transition is playing, so it never drifts-unlocks mid-transition and is
        // always available to re-lock when the transition returns to the riff (A).
        phraseLearner.setHoldActive(grooveLockActive
            || postLockPhase == PostLockPhase::TransitionHold);

        // ── Riff-lock hold progress (UI): bar done / bars remaining ───────────
        // While locked, publish the 1-based current bar and the bars left before
        // the transition fires. Zeroed when not locked so the UI hides it.
        if (grooveLockActive && grooveLockStartSample >= 0 && samplesPerBar > 0)
        {
            const int64_t elapsed = hostSampleTime - grooveLockStartSample;
            const int total = lockBarsTotal.load(std::memory_order_relaxed);
            const int current = juce::jlimit(1, juce::jmax(1, total),
                                             static_cast<int>(elapsed / samplesPerBar) + 1);
            lockBarCurrent.store(current, std::memory_order_relaxed);
            lockBarsRemaining.store(juce::jmax(0, total - current), std::memory_order_relaxed);
        }
        else
        {
            lockBarCurrent.store(0, std::memory_order_relaxed);
            lockBarsRemaining.store(0, std::memory_order_relaxed);
            lockBarsTotal.store(0, std::memory_order_relaxed);
        }

        // ── P0/R4 + A5.2: on lock expiry, engage a post-lock TRANSITION SECTION
        // instead of releasing straight back to the listener. The transition
        // grammar (PatternRules::pickNextSectionAfterLock) picks a *contrast*
        // section (never the riff's own feel, never the last one played) and
        // holds it for `transitionBars` bars before returning to the riff (A).
        // If the riff re-appears mid-transition, we cut back to it immediately.
        if (grooveLockReleaseArmed)
        {
            grooveLockReleaseArmed = false;

            const int transitionBars = [this]() -> int
            {
                if (auto* raw = apvts.getRawParameterValue("transitionBars"))
                    return juce::jlimit(2, 32, static_cast<int>(std::lround(raw->load())));
                return 8;
            }();
            const int64_t samplesPerBarT =
                static_cast<int64_t>(4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);

            int genreIdT = 0;
            if (auto* rawGenre = apvts.getRawParameterValue("genre"))
                genreIdT = juce::jlimit(0, Groove::presetCount() - 1,
                                        static_cast<int>(std::lround(rawGenre->load())));

            const int lockedPat = latestPatternIndex.load(std::memory_order_acquire);
            const auto ts = PatternRules::pickNextSectionAfterLock(
                lockedPat, genreIdT, transitionSectionNameStr);

            postLockPhase = PostLockPhase::TransitionHold;
            transitionPool = ts.pool;
            transitionSectionNameStr = ts.name;
            lastPlayedPoolPattern = -1;  // fresh rotation for the new hold section
            lastTransitionSlot = -1;
            cachedTransitionPick = -1;
            transitionSectionName.store(ts.name, std::memory_order_release);
            ++transitionSectionNumberLocal;
            transitionSectionNumber.store(transitionSectionNumberLocal, std::memory_order_release);
            transitionBarsTotalLocal = transitionBars;
            transitionBarsTotal.store(transitionBars, std::memory_order_release);
            transitionBarsRemaining.store(transitionBars, std::memory_order_release);
            transitionStartSample = hostSampleTime;
            transitionEndSample = hostSampleTime + static_cast<int64_t>(transitionBars) * samplesPerBarT;
            transitionSectionActive.store(true, std::memory_order_release);

            // Musical entrance: crash + a build-up fill into the section's most
            // popular pool pattern (bar-quantized by PatternPlayer).
            patternPlayer.armTransitionCrash();
            PatternPlayer::GrooveCommit entry{};
            entry.patternIndex = (ts.pool.count > 0) ? ts.pool.indices[0] : lockedPat;
            entry.fillKind = PatternPlayer::TransitionFillKind::BuildUp;
            patternPlayer.queueGrooveCommit(entry);
        }

        // ── A5.2: run the transition hold. ────────────────────────────────────
        if (postLockPhase == PostLockPhase::TransitionHold)
        {
            // The riff re-appearing AFTER the transition began cuts it short and
            // re-locks. `lastRiffMatchSample > transitionStartSample` is an edge
            // check: a match from BEFORE the transition (e.g. the riff that was
            // playing at lock expiry) does NOT cut it — the transition gets to
            // play until the riff genuinely comes back.
            const bool riffReappeared = lastRiffMatchSample > transitionStartSample;
            if (phraseLockEdge || riffReappeared)
            {
                postLockPhase = PostLockPhase::Idle;
                transitionSectionActive.store(false, std::memory_order_release);
                grooveLockActive = true;
                grooveLockEndSample = hostSampleTime + lockDuration;
                grooveLockStartSample = hostSampleTime;
                lockBarsTotal.store(lockBars, std::memory_order_relaxed);
                lastRiffMatchSample = hostSampleTime;
                grooveLockReleaseArmed = false;
            }
            else if (hostSampleTime >= transitionEndSample)
            {
                // Hold finished. Continue to the next contrast section (up to
                // `transitionSections` total), then return to the riff (A).
                int maxSections = 2;
                if (auto* raw = apvts.getRawParameterValue("transitionSections"))
                    maxSections = juce::jlimit(1, 4, static_cast<int>(std::lround(raw->load())));

                if (transitionSectionNumberLocal < maxSections)
                {
                    int genreIdT2 = 0;
                    if (auto* rawGenre = apvts.getRawParameterValue("genre"))
                        genreIdT2 = juce::jlimit(0, Groove::presetCount() - 1,
                                                 static_cast<int>(std::lround(rawGenre->load())));
                    const int lockedPat2 = latestPatternIndex.load(std::memory_order_acquire);
                    const auto ts2 = PatternRules::pickNextSectionAfterLock(
                        lockedPat2, genreIdT2, transitionSectionNameStr);

                    const int transitionBars2 = [this]() -> int
                    {
                        if (auto* raw = apvts.getRawParameterValue("transitionBars"))
                            return juce::jlimit(2, 32, static_cast<int>(std::lround(raw->load())));
                        return 8;
                    }();
                    const int64_t samplesPerBarT2 =
                        static_cast<int64_t>(4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);

                    transitionPool = ts2.pool;
                    transitionSectionNameStr = ts2.name;
                    lastPlayedPoolPattern = -1;  // fresh rotation for the next hold section
                    lastTransitionSlot = -1;
                    cachedTransitionPick = -1;
                    transitionSectionName.store(ts2.name, std::memory_order_release);
                    ++transitionSectionNumberLocal;
                    transitionSectionNumber.store(transitionSectionNumberLocal, std::memory_order_release);
                    transitionBarsTotalLocal = transitionBars2;
                    transitionBarsTotal.store(transitionBars2, std::memory_order_release);
                    transitionBarsRemaining.store(transitionBars2, std::memory_order_release);
                    transitionStartSample = hostSampleTime;
                    transitionEndSample = hostSampleTime + static_cast<int64_t>(transitionBars2) * samplesPerBarT2;

                    patternPlayer.armTransitionCrash();
                    PatternPlayer::GrooveCommit next{};
                    next.patternIndex = (ts2.pool.count > 0) ? ts2.pool.indices[0] : lockedPat2;
                    next.fillKind = PatternPlayer::TransitionFillKind::BuildUp;
                    patternPlayer.queueGrooveCommit(next);
                }
                else
                {
                    // Transition sequence complete — firmly return to the locked
                    // riff (A): re-engage the groove lock so the cycle continues
                    // (A → B → A), instead of dropping straight to follow/listen.
                    // The riff was kept held in the learner, so it re-locks
                    // seamlessly and the drums resume the frozen riff groove.
                    postLockPhase = PostLockPhase::Idle;
                    transitionSectionActive.store(false, std::memory_order_release);
                    engageGrooveLockFromRiff();
                    patternPlayer.armTransitionCrash();
                }
            }
            else
            {
                // Countdown for the UI.
                const int64_t samplesPerBarC =
                    static_cast<int64_t>(4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
                const int64_t remaining = transitionEndSample - hostSampleTime;
                const int barsLeft = (samplesPerBarC > 0)
                    ? static_cast<int>((remaining + samplesPerBarC - 1) / samplesPerBarC)
                    : 0;
                transitionBarsRemaining.store(juce::jmax(0, barsLeft), std::memory_order_release);
            }
        }

        // ── 9c. Live riff mirror ─────────────────────────────────────────────
        // While the guitarist is playing a riff (evidence of riffing: ≥2
        // attacks in the last 2 bars), the bass mirrors each detected attack
        // immediately — no 2–6 s learning wait and no sparse beat-1 fallback.
        // Active grid capture (Record riff) suppresses the fallback bass; passive
        // live LISTEN does not — the drummer keeps playing while it observes.
        const bool riffActive = !silentNow
            && phraseLearner.countRecentAttacks(hostSampleTime, 2 * samplesPerBar) >= 2;
        patternPlayer.setPhraseLearnerActive(
            phraseLocked || riffActive || phraseLearner.isGridCapturing());

        // Shorter gate (~0.4 beat) so dense chugs do not stack into a wall.
        if (bassNote.trigger && !riffCaptureActive.load(std::memory_order_acquire)
            && !phraseLearner.isGridCapturing())
        {
            const int durationSamples = static_cast<int>((60.0 / bpmForPlayer) * 0.4 * sr);
            patternPlayer.triggerLearnedBassNote(bassNote.midiNote + bassTranspose, bassNote.velocity, 0, durationSamples);
        }

        if (phraseLocked)
        {
            // Locked: the bass plays the learned riff note-for-note (R1).
        }
        else if (!riffActive)
        {
            // Fallback (only when the guitarist is NOT actively riffing):
            // beat-grid bass on the guitarist's root.
            int bassRoot = 36;  // C2 fallback (drop-C root — matches the tracker anchor)
            if (semitoneOffset != INT_MIN)
            {
                bassRoot = 36 + semitoneOffset;
                while (bassRoot < 28) bassRoot += 12;
                while (bassRoot > 55) bassRoot -= 12;
            }
            
            int notesPerBar = 2;
            if (std::strcmp(section, "CHORUS") == 0 || std::strcmp(section, "SOLO") == 0)
                notesPerBar = 4;
            else if (std::strcmp(section, "INTRO") == 0 || std::strcmp(section, "OUTRO") == 0 || std::strcmp(section, "BREAKDOWN") == 0)
                notesPerBar = 2;
            
            patternPlayer.setBassParams(bassRoot, notesPerBar);
        }
    }

    // ── 10. Process MIDI ────────────────────────────────────────────────────
    patternPlayer.process(midi, numSamples, rawHostPos);

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
    displayNoiseFloor.store(structureTagger.getNoiseFloorRms(), std::memory_order_relaxed);
    displayStateIndex.store(static_cast<int>(st), std::memory_order_relaxed);
    displayPatternIndex.store(effectivePatternIdx, std::memory_order_relaxed);

    // Playhead fraction is computed earlier in the block from the resolved host
    // clock (so it aligns with the drums' transport grid).
}

int AccompanimentProcessor::getScopeSamplesPerBar() const noexcept
{
    // The scope ring is written every kScopeDecimation-th input sample (see the
    // display-scope feed in processBlock). A bar is 4 beats = 4 * 60/bpm seconds.
    constexpr int kScopeDecimation = 8;
    const double sr = cachedSampleRate.load(std::memory_order_relaxed);
    float bpm = displayBpm.load(std::memory_order_relaxed);
    if (!(bpm > 0.0f) || bpm > 1000.0f)
        bpm = 120.0f;
    const double rawBar = 4.0 * 60.0 / static_cast<double>(bpm) * sr;
    const int decBar = static_cast<int>(rawBar / static_cast<double>(kScopeDecimation));
    return juce::jmax(1, decBar);
}

void AccompanimentProcessor::copyScopeSamples(float* out, int maxCount) const noexcept
{
    if (out == nullptr || maxCount <= 0)
        return;
    const int head = scopeWriteIndex.load(std::memory_order_relaxed);
    const int count = juce::jmin(maxCount, kScopeSize);
    // Ring: [head-count, head) newest last. Copy oldest→newest so the UI draws
    // left→right in time order.
    for (int i = 0; i < count; ++i)
    {
        const int idx = (head - count + i) % kScopeSize;
        const int wrapped = (idx < 0) ? idx + kScopeSize : idx;
        out[i] = scopeSamples[static_cast<size_t>(wrapped)];
    }
}

void AccompanimentProcessor::bumpDebugPattern()
{
    const int dur = juce::jmax(1, static_cast<int>(std::round(5.0 * cachedSampleRate.load(std::memory_order_acquire))));
    debugPreviewSamplesRemaining.store(dur, std::memory_order_release);
    // D-23-04: drive rejection signal instead of directly cycling index
    patternRejectionCount.fetch_add(1, std::memory_order_release);
}

void AccompanimentProcessor::setCustomSongForm(const juce::String& serialized)
{
    // Persist with the session (Phase 2) and hand the parsed form to the audio
    // thread lock-free. Called on the message thread (editor / state restore).
    apvts.state.setProperty("customSongForm", serialized, nullptr);
    auto form = std::make_shared<SongForm>(
        StructureSequencer::parseFormString(serialized.toStdString()));
    std::atomic_store(&pendingSongForm, std::move(form));
    songFormVersion.fetch_add(1, std::memory_order_release);
}

juce::String AccompanimentProcessor::getCustomSongForm()
{
    const juce::var prop = apvts.state.getProperty("customSongForm");
    return prop.isString() ? prop.toString() : juce::String{};
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

    // Restore the editable form from the persisted property, falling back to the
    // default practice form (INTRO → VERSE → CHORUS → VERSE → CHORUS → OUTRO)
    // when no custom form was saved (older sessions).
    const juce::var prop = apvts.state.getProperty("customSongForm");
    if (prop.isString())
        setCustomSongForm(prop.toString());
    else
        setCustomSongForm("INTRO:4,VERSE:8,CHORUS:8,VERSE:8,CHORUS:8,OUTRO:4");
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
