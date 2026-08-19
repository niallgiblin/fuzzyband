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

    patternPlayer.prepare(sr, samplesPerBlock);
    patternPlayer.reset();

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
        if (grooveLocked.load(std::memory_order_acquire))
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
                    idx = groove->selectPatternFromMel(latestMel.data.data(), excludeParam);
                    displayStyle.store(groove->classifyStyle(latestMel.data.data()),
                                       std::memory_order_relaxed);
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

        if (drumHoldExpired || excludeParam >= 0 || transitionEvent || autoChangeReady)
        {
            commit.patternIndex = diversifiedIdx;
            commit.fillKind = chooseTransitionFillKind(lastCommittedStructureState, patternFeatures.state,
                                                       diversifiedIdx, patternFeatures.rmsEnergy, patternFeatures.rmsDelta);
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

    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;

    // ── 1. Energy analysis ──────────────────────────────────────────────────
    energyAnalyser.process(in, numSamples);
    pitchEstimator.process(in, numSamples);
    audioRingBuffer.write(in, numSamples);

    // ── 1b. Mel spectrogram extraction (when window ready) ─────────────────
    if (audioRingBuffer.isWindowReady())
    {
        if (melScratch.size() < 22050)
            melScratch.resize(22050);

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
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto t = pos->getBpm())
                bpmForPlayer = static_cast<float>(*t);
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
                const int bar = structureSequencer.getGlobalBarCount();
                const bool isLast = structureSequencer.isLastBar();
                effectivePatternIdx = isLast
                    ? PatternRules::selectFillPattern(0)
                    : pool.indices[bar % pool.count];
            }
        }
    }
    patternPlayer.setPatternIndex(effectivePatternIdx);

    // Section for velocity contrast (A3.1) and bass harmony (A1.2).
    // Reactive mode maps loud playing to CHORUS so dynamics track the guitarist.
    const char* section = playOn ? structureSequencer.getCurrentSectionName() : "VERSE";
    if (!playOn && st == StructureState::LOUD)
        section = "CHORUS";
    patternPlayer.setSection(Groove::sectionIdFromName(section));

    // ── 7. Silence gating ───────────────────────────────────────────────────
    const bool audioActive = (rms > 0.001f);
    const bool trulySilent = (!playOn && !audioActive) || digitalSilence;

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    if (previewRem > 0)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);
    patternPlayer.setStructureSilent(trulySilent && previewRem <= 0);

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

        // Run phrase learner - it tracks attacks and pitches, detects riff repetition
        const auto bassNote = silentNow
            ? PhraseLearner::BassNote{}
            : phraseLearner.process(
                hostSampleTime,
                rms,
                pitchEstimator.getMidiNote(),
                pitchEstimator.getConfidence(),
                bpmForPlayer,
                numSamples
            );
        if (silentNow)
            phraseLearner.reset();

        const bool phraseLocked = phraseLearner.isLocked() && !silentNow;

        // ── 9b. Generative groove lock ────────────────────────────────────────
        // In generative mode (Play off), when the phrase learner detects the
        // guitarist's riff repeating, the groove auto-locks: the drum pattern
        // freezes (inference commits are suppressed) and the bass keeps looping
        // the learned riff while the guitarist expands/solos over it.
        //
        // P0/R2: the hold is a FIXED `lockBars`-bar window — returning to the
        // riff does NOT extend it. After expiry the lock re-engages only if the
        // riff is still being played (fresh match within a 2-bar grace) — a
        // stale learner must not re-freeze the listener. Silence or Play-on
        // releases it.
        int lockBars = 16;
        if (auto* rawLockBars = apvts.getRawParameterValue("lockBars"))
        {
            // Discrete params (AudioParameterInt) report their raw integer value
            // through the APVTS adapter (same convention as the choice params).
            lockBars = juce::jlimit(4, 64, static_cast<int>(std::lround(rawLockBars->load())));
        }
        const int64_t samplesPerBar = static_cast<int64_t>(
            4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
        const int64_t lockDuration = static_cast<int64_t>(lockBars) * samplesPerBar;
        const int64_t reEngageGrace = 2 * samplesPerBar;  // riff freshness window

        if (phraseLearner.justMatchedRiff())
            lastRiffMatchSample = hostSampleTime;

        const bool phraseLockEdge = (phraseLocked && !prevPhraseLocked);
        prevPhraseLocked = phraseLocked;

        if (playOn || silentNow || digitalSilence)
        {
            grooveLockActive = false;
            grooveLockEndSample = -1;
            grooveLockReleaseArmed = false;
        }
        else if (!grooveLockActive)
        {
            // Engage: the riff just repeated (lock edge), or the riff was played
            // within the re-engage grace after a hold expired.
            const bool riffFresh = (hostSampleTime - lastRiffMatchSample) < reEngageGrace;
            if (phraseLockEdge || riffFresh)
            {
                grooveLockActive = true;
                grooveLockEndSample = hostSampleTime + lockDuration;
                lastRiffMatchSample = hostSampleTime;
                grooveLockReleaseArmed = false;
            }
        }
        else
        {
            // Held: the window is fixed — release exactly when it elapses.
            if (hostSampleTime >= grooveLockEndSample)
            {
                grooveLockActive = false;
                grooveLockReleaseArmed = true;  // P0/R4: transition out of the lock
            }
        }
        grooveLocked.store(grooveLockActive, std::memory_order_release);
        phraseLearner.setHoldActive(grooveLockActive);

        // P0/R4: on lock expiry, arm a crash + a release fill at the bar
        // boundary, then hand pattern selection back to the listener (the
        // inference thread resumes as soon as grooveLocked flips false).
        if (grooveLockReleaseArmed)
        {
            grooveLockReleaseArmed = false;
            patternPlayer.armTransitionCrash();
            PatternPlayer::GrooveCommit releaseCommit{};
            releaseCommit.patternIndex = latestPatternIndex.load(std::memory_order_acquire);
            releaseCommit.fillKind = PatternPlayer::TransitionFillKind::Release;
            patternPlayer.queueGrooveCommit(releaseCommit);
        }

        // Track the guitarist's root pitch class every block (C-anchored,
        // 1/8-beat stability window). It feeds only the fallback harmonic bass
        // (when NOT riffing) — the locked phrase bass is frozen to the learned
        // riff (P0/R1: the bass no longer retunes to the live root).
        const int semitoneOffset = stablePitchTracker.update(
            pitchEstimator.getMidiNote(),
            pitchEstimator.getConfidence(),
            bpmForPlayer, numSamples, sr, silentNow);

        // ── 9c. Live riff mirror ─────────────────────────────────────────────
        // While the guitarist is playing a riff (evidence of riffing: ≥2
        // attacks in the last 2 bars), the bass mirrors each detected attack
        // immediately — no 2–6 s learning wait and no sparse beat-1 fallback.
        // This suppresses the beat-grid fallback for the duration of riffing;
        // the grid returns when riffing stops. A lone accent (1 attack) does
        // not count as riffing.
        const bool riffActive = !silentNow
            && phraseLearner.countRecentAttacks(hostSampleTime, 2 * samplesPerBar) >= 2;
        patternPlayer.setPhraseLearnerActive(phraseLocked || riffActive);

        // Trigger either the locked pattern's note or the live attack's note.
        // Legato duration (~0.9 beat): the bass sustains through dense chugs
        // instead of staccato blips (monophonic handling overlaps cleanly).
        if (bassNote.trigger)
        {
            const int durationSamples = static_cast<int>((60.0 / bpmForPlayer) * 0.9 * sr);  // ~90% of beat
            patternPlayer.triggerLearnedBassNote(bassNote.midiNote + bassTranspose, bassNote.velocity, 0, durationSamples);
        }

        if (phraseLocked)
        {
            // Locked: the bass plays the learned riff note-for-note (R1). The
            // live-root retune is gone — the recorded pitches are authoritative
            // for the whole hold, so a root change by the guitarist does not
            // yank the bass. `StablePitchTracker` keeps running purely for the
            // fallback root when NOT locked.
        }
        else if (!riffActive)
        {
            // Fallback (only when the guitarist is NOT actively riffing):
            // beat-grid bass on the guitarist's root.
            int bassRoot = 36;  // C2 fallback (drop-C root — matches the tracker anchor)
            if (semitoneOffset != INT_MIN)
            {
                // Pitch-class following folded onto C2: C→36, E→40, G→43, B→47.
                // Every root lands in the C2–B2 octave — never below C2 (too low
                // for range-limited bass VSTs) and never a wrong pitch class.
                bassRoot = 36 + semitoneOffset;
                while (bassRoot < 28) bassRoot += 12;
                while (bassRoot > 55) bassRoot -= 12;
            }
            
            int notesPerBar = 2;
            if (std::strcmp(section, "CHORUS") == 0 || std::strcmp(section, "SOLO") == 0)
                notesPerBar = 4;
            // Minimum pulse: half notes everywhere (a whole-note drone on beat 1
            // read as "the bass isn't opening up" — user session feedback).
            else if (std::strcmp(section, "INTRO") == 0 || std::strcmp(section, "OUTRO") == 0 || std::strcmp(section, "BREAKDOWN") == 0)
                notesPerBar = 2;
            
            patternPlayer.setBassParams(bassRoot, notesPerBar);
        }
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
    displayNoiseFloor.store(structureTagger.getNoiseFloorRms(), std::memory_order_relaxed);
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
    // first preset when no custom form was saved (older sessions).
    const juce::var prop = apvts.state.getProperty("customSongForm");
    if (prop.isString())
        setCustomSongForm(prop.toString());
    else
        setCustomSongForm(juce::String(StructureSequencer::serializeForm(StructureSequencer::getPresets().front())));
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
