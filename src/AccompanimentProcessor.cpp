#include "AccompanimentProcessor.h"

#include <algorithm>
#include "AccompanimentEditor.h"
#include "inference/RuleBasedInference.h"
#include "inference/pattern_rules.h"
#if defined(MA_ENABLE_ONNX)
#include "inference/MetalGrooveInference.h"
#endif
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
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
    // T0.3: a failed load used to fall through silently, so a stale ONNX
    // Runtime dylib looked like a working ML build. jassert fires in Debug;
    // the integration test asserts getActiveInferenceName() so Release CI
    // cannot masquerade either. Keep the fallback so a DAW session still
    // instantiates if the model is missing.
    jassertfalse;
#endif
    return std::make_unique<RuleBasedInference>();
}

// ── Style smoothing (perception layer) ──────────────────────────────────────
// classifyStyle() runs once per mel window (~2 Hz: one 512 ms audio window), so
// "windows" here are ~0.5 s apart, NOT the ~50 Hz inference drain. Raw argmax is
// noisy, so we commit a style only after kStyleStableWindows consecutive agreeing
// windows, then hold it for kStyleHoldWindows windows before allowing a change.
// Together that is ~1.5 s to lock on and ~2 s minimum hold — long enough that a
// single phrase doesn't flip back and forth, short enough to track a real change.
constexpr int kStyleStableWindows = 3;
constexpr int kStyleHoldWindows   = 4;

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

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "humanize", 1 },
        "Humanize",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.01f },
        0.35f));

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
        false));

    // Generative groove lock: how many bars the auto-lock holds after the last
    // moment the riff was being played (returning to the riff extends it).
    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "lockBars", 1 },
        "Groove lock (bars)",
        4, 64, 16));

    // Post-lock transition grammar (A5.2): how long each contrast section
    // holds, and how many distinct contrasts to visit. Each contrast returns
    // to the locked riff (A) before the next one: 2 → A-B-A-C-A.
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
    patternSelectFrozen.store(false, std::memory_order_relaxed);
    inferencePatternSelectCount.store(0, std::memory_order_relaxed);
    playbackGate.reset();
    prevBlockRms = 0.0f;
    guitarEnergyRms_ = 0.0f;
    riffCapturePhase = RiffCapturePhase::Idle;
    riffCountInStartBeat = 0.0;
    playCountInActive = false;
    playCountInWaitingBar = false;
    playCountInStartBeat = 0.0;
    riffCaptureActive.store(false, std::memory_order_relaxed);
    riffCaptureNoteCount.store(0, std::memory_order_relaxed);
    riffCaptureBar.store(0, std::memory_order_relaxed);
    riffHeld.store(false, std::memory_order_relaxed);
    enginePhase = EnginePhase::Idle;
    riffA = {};
    riffB = {};
    riffAPlayOriginMono = -1;
    riffBPlayOriginMono = -1;
    lockOriginMono = -1;
    lockBarPhaseBeats = 0.0;
    drumA = drumB0 = drumB = 0;
    guitarSilentSamples = 0;
    playSectionIndex.store(-1, std::memory_order_relaxed);
    sectionEntryBar.store(0, std::memory_order_relaxed);
    lastPlayedPoolPattern = -1;
    lastPlayGrooveSlot = -1;
    lastFollowStateIndex = -1;
    lastBListenPoolPattern = -1;
    lastBListenGrooveSlot = -1;
    transitionCutShortArmed = false;
    sameRiffMatchCount = 0;
    lastRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
    firstRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
    lastSectionIndex = -1;
    lastSeenBarsElapsed = -1;
    requestBLockPick.store(false, std::memory_order_relaxed);
    bLockPick.store(-1, std::memory_order_relaxed);
    playFillArm.clear();
    riffAFillArm.clear();
    riffBFillArm.clear();

    // A5.2 post-lock transition state + display scope.
    postLockPhase = PostLockPhase::Idle;
    riffLoopActive = false;
    transitionStartMono = -1;
    transitionEndMono = -1;
    transitionSectionNumberLocal = 0;
    transitionSectionActive.store(false, std::memory_order_relaxed);
    transitionSectionName.store("VERSE", std::memory_order_relaxed);
    transitionBarsRemaining.store(0, std::memory_order_relaxed);
    transitionBarsTotal.store(0, std::memory_order_relaxed);
    transitionSectionNumber.store(0, std::memory_order_relaxed);
    for (int i = 0; i < kMaxTransitionSlots; ++i)
    {
        transitionSlotNames[i] = nullptr;
        transitionSlotPinned[i] = false;
    }
    scopeWriteIndex.store(0, std::memory_order_relaxed);
    playheadFraction.store(0.0f, std::memory_order_relaxed);
    sectionPhase.store(0, std::memory_order_relaxed);
    sectionBar.store(0, std::memory_order_relaxed);
    sectionBarsTotal.store(0, std::memory_order_relaxed);
    sectionBarsRemaining.store(0, std::memory_order_relaxed);
    sectionProgress.store(0.0f, std::memory_order_relaxed);

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
    lastClockSample = -1;
    lastClockBlockSamples = 0;
    grooveLockStartMono = -1;
    grooveLockEndMono = -1;
    latestPatternIndex.store(0, std::memory_order_relaxed);

    lastLoopValue = false;
    publishRiffUiSnapshot();

    inferencePaused.store(false, std::memory_order_release);
}

void AccompanimentProcessor::publishRiffUiSnapshot() noexcept
{
    const int pub = riffUiPublished.load(std::memory_order_relaxed);
    const int busy = riffUiReading.load(std::memory_order_acquire);
    int next = 0;
    while (next == pub || next == busy)
        ++next;
    if (next >= kRiffUiSlots)
        next = (pub == 0) ? 1 : 0;
    auto& dst = riffUiSlots[static_cast<size_t>(next)];
    dst.riffA = riffA;
    dst.riffB = riffB;
    dst.enginePhase = enginePhase;
    dst.drumA = drumA;
    dst.drumB0 = drumB0;
    dst.drumB = drumB;
    riffUiPublished.store(next, std::memory_order_release);
}

AccompanimentProcessor::RiffUiRead::RiffUiRead(const AccompanimentProcessor& p) noexcept
    : proc(p)
{
    int idx = 0;
    do
    {
        idx = proc.riffUiPublished.load(std::memory_order_acquire);
        proc.riffUiReading.store(idx, std::memory_order_release);
    }
    while (idx != proc.riffUiPublished.load(std::memory_order_acquire));
    slot = idx;
}

AccompanimentProcessor::RiffUiRead::~RiffUiRead() noexcept
{
    proc.riffUiReading.store(-1, std::memory_order_release);
}

const AccompanimentProcessor::RiffUiSnapshot&
AccompanimentProcessor::RiffUiRead::get() const noexcept
{
    return proc.riffUiSlots[static_cast<size_t>(slot)];
}

int AccompanimentProcessor::getRiffAOccupiedCount() const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffA;
    if (!riff.valid)
        return 0;
    int n = 0;
    for (int i = 0; i < PhraseLearner::kGridSlots; ++i)
        if (riff.occupied[static_cast<size_t>(i)])
            ++n;
    return n;
}

bool AccompanimentProcessor::getRiffASlotOccupied(int slot) const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffA;
    return riff.valid && slot >= 0 && slot < PhraseLearner::kGridSlots
        && riff.occupied[static_cast<size_t>(slot)];
}

int AccompanimentProcessor::getRiffASlotMidi(int slot) const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffA;
    if (!riff.valid || slot < 0 || slot >= PhraseLearner::kGridSlots
        || !riff.occupied[static_cast<size_t>(slot)])
        return -1;
    return riff.midi[static_cast<size_t>(slot)];
}

int AccompanimentProcessor::getRiffASlotGate(int slot) const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffA;
    if (!riff.valid || slot < 0 || slot >= PhraseLearner::kGridSlots
        || !riff.occupied[static_cast<size_t>(slot)])
        return 0;
    return static_cast<int>(riff.gate16[static_cast<size_t>(slot)]);
}

int AccompanimentProcessor::getDrumA() const noexcept
{
    const RiffUiRead read(*this);
    return read.get().drumA;
}

int AccompanimentProcessor::getDrumB0() const noexcept
{
    const RiffUiRead read(*this);
    return read.get().drumB0;
}

int AccompanimentProcessor::getDrumB() const noexcept
{
    const RiffUiRead read(*this);
    return read.get().drumB;
}

bool AccompanimentProcessor::isRiffBLocked() const noexcept
{
    const RiffUiRead read(*this);
    return read.get().enginePhase == EnginePhase::RiffBLocked;
}

int AccompanimentProcessor::getRiffBOccupiedCount() const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffB;
    if (!riff.valid)
        return 0;
    int n = 0;
    for (int i = 0; i < PhraseLearner::kGridSlots; ++i)
        if (riff.occupied[static_cast<size_t>(i)])
            ++n;
    return n;
}

bool AccompanimentProcessor::getRiffBSlotOccupied(int slot) const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffB;
    return riff.valid && slot >= 0 && slot < PhraseLearner::kGridSlots
        && riff.occupied[static_cast<size_t>(slot)];
}

int AccompanimentProcessor::getRiffBSlotMidi(int slot) const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffB;
    if (!riff.valid || slot < 0 || slot >= PhraseLearner::kGridSlots
        || !riff.occupied[static_cast<size_t>(slot)])
        return -1;
    return riff.midi[static_cast<size_t>(slot)];
}

int AccompanimentProcessor::getRiffBSlotGate(int slot) const noexcept
{
    const RiffUiRead read(*this);
    const auto& riff = read.get().riffB;
    if (!riff.valid || slot < 0 || slot >= PhraseLearner::kGridSlots
        || !riff.occupied[static_cast<size_t>(slot)])
        return 0;
    return static_cast<int>(riff.gate16[static_cast<size_t>(slot)]);
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

        // ── Style classification (always-on) ────────────────────────────────
        // classifyStyle() must keep running while the groove is locked or a
        // transition is playing, otherwise the UI style readout freezes on the
        // articulation captured at lock time and never tracks subsequent
        // changes. Only PATTERN SELECTION is frozen by the lock below; the
        // style perception head updates every drain.
        MelWindow latestMel{};
        bool gotMel = false;
#if defined(MA_ENABLE_ONNX)
        {
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
                    updateCommittedStyle(groove->classifyStyle(latestMel.data.data()));
            }
        }
#endif

        // Pattern selection is frozen in count-in, capture, and Record riff A / B-locked.
        // PlaySection and RiffBListen honor argmax (constrained on the audio thread).
        // T6.3: B-listen must keep selecting so the contrast pool can rotate.
        if (patternSelectFrozen.load(std::memory_order_acquire)
            || grooveLocked.load(std::memory_order_acquire)
            || riffCaptureActive.load(std::memory_order_acquire))
        {
            if (requestBLockPick.load(std::memory_order_acquire))
            {
                // One-shot B lock pick: argmax constrained later on the audio thread.
#if defined(MA_ENABLE_ONNX)
                if (gotMel)
                {
                    if (auto* groove = dynamic_cast<MetalGrooveInference*>(inference.get()))
                    {
                        const int pick = groove->selectPatternFromMel(latestMel.data.data(), -1, -1);
                        bLockPick.store(pick, std::memory_order_release);
                    }
                }
#endif
                requestBLockPick.store(false, std::memory_order_release);
            }
            return;
        }

        inferencePatternSelectCount.fetch_add(1, std::memory_order_relaxed);

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
        if (gotMel)
        {
            if (auto* groove = dynamic_cast<MetalGrooveInference*>(inference.get()))
            {
                // T4.2: seed from the section instance so a phrase is stable
                // while different sections (or follow-mode state entries) can
                // draw a neighbour from the top-K. B-lock one-shots stay
                // deterministic (seed -1) above.
                const int melSeed = sectionEntryBar.load(std::memory_order_acquire) & 0x7fffffff;
                idx = groove->selectPatternFromMel(latestMel.data.data(), excludeParam, melSeed);
                usedMelPath = true;
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

        // R1: rhythm-driven correction. A timbre/energy selector picks the groove
        // "family" but not the density — steer the base toward a state-compatible
        // groove that matches how densely the guitarist is actually picking.
        idx = PatternRules::refineByRhythm(idx, latest);
        PatternPlayer::GrooveCommit commit{};
        commit.patternIndex = currentPat;
        bool hasGrooveCommit = false;
        bool acceptedDrumPattern = false;

        // T6.1: 1-bar hold (was 2 bars / 8 beats). A large RMS step may commit
        // at beat rate so a flurry of gestures cannot thrash faster than that.
        // The 512 ms mel window stays: metal_groove.onnx is trained on 64×40
        // frames from 22050 samples — a shorter window would need a retrain.
        const float bpmNowDrum = latest.bpm > 0.0f ? latest.bpm : 120.0f;
        const int64_t holdSamples =
            static_cast<int64_t>(4.0 * 60.0 / static_cast<double>(bpmNowDrum) * sr);
        const int64_t beatSamples =
            static_cast<int64_t>(1.0 * 60.0 / static_cast<double>(bpmNowDrum) * sr);
        const bool gestureChange = std::abs(latest.rmsDelta) > 0.6f;
        const int64_t sinceChange = (lastDrumPatternChangeSample < 0)
            ? holdSamples
            : (latest.sampleTimestamp - lastDrumPatternChangeSample);
        const bool drumHoldExpired = (lastDrumPatternChangeSample < 0)
            || (sinceChange >= holdSamples);
        const bool inPlay = playSectionIndex.load(std::memory_order_acquire) >= 0;
        const bool gestureOk = inPlay && gestureChange
            && (lastDrumPatternChangeSample < 0 || sinceChange >= beatSamples);

        const int64_t samplesPerBarDiv = static_cast<int64_t>(4.0 * 60.0 / static_cast<double>(latest.bpm) * sr);
        const int barMod8 = (samplesPerBarDiv > 0) ? static_cast<int>((latest.sampleTimestamp / samplesPerBarDiv) % 8) : 0;
        int genreId = 0;
        if (auto* rawGenre = apvts.getRawParameterValue("genre"))
            genreId = juce::jlimit(0, Groove::presetCount() - 1,
                                   static_cast<int>(std::lround(rawGenre->load())));

        // Play skips genre/style rewrite (audio thread constrains to the section
        // pool). Idle/follow still steers so the UI groove can move.
        int finalIdx = idx;
        if (playSectionIndex.load(std::memory_order_acquire) < 0)
        {
            finalIdx = PatternRules::diversifyPatternForGenre(idx, latest, barMod8, genreId);
            if (committedStyle >= 0 && committedStyle <= 3)
                finalIdx = PatternRules::diversifyPatternForStyle(
                    finalIdx, committedStyle, barMod8, latest.state, genreId);
        }

        if (drumHoldExpired || excludeParam >= 0 || gestureOk)
        {
            acceptedDrumPattern = true;
            // Play and B-listen drums are owned by the audio thread (pool
            // rotation). Inference still updates latestPatternIndex as a vote;
            // a player commit here would change the kit off the bar line and
            // drop authored bass hits (T5.1 / T6.3).
            const bool audioOwnsDrums =
                playSectionIndex.load(std::memory_order_acquire) >= 0
                || transitionSectionActive.load(std::memory_order_acquire);
            if (!audioOwnsDrums)
            {
                commit.patternIndex = finalIdx;
                commit.alignToBeat = gestureOk && !drumHoldExpired;
                hasGrooveCommit = true;
            }
        }
        // T4.4: audio thread is the only writer of displayPatternIndex.

        if (acceptedDrumPattern)
        {
            latestPatternIndex.store(finalIdx, std::memory_order_release);
            lastDrumPatternChangeSample = latest.sampleTimestamp;
            lastCommittedStructureState = patternFeatures.state;
        }
        if (hasGrooveCommit)
            (void) grooveCommitQueue.try_enqueue(commit);
        // Live drums use Groove::Template humanize only (no rendered grid enqueue).
    }
}

void AccompanimentProcessor::updateCommittedStyle(int rawStyle) noexcept
{
    // Raw argmax is called once per ~512 ms mel window, so `styleAgreeCount` is
    // in windows (~0.5 s each), not drain iterations.
    if (rawStyle == styleRaw)
        ++styleAgreeCount;
    else
    {
        styleRaw = rawStyle;
        styleAgreeCount = 1;
    }

    if (styleHoldRemaining > 0)
        --styleHoldRemaining;

    // Single notes (class 2) are transient articulations, so they commit and
    // release faster than held styles (chug / chord / sustain) — a lead line
    // should register without being averaged away, but still hold long enough
    // not to flicker between adjacent windows.
    const int confirmWindows = (rawStyle == 2) ? 2 : kStyleStableWindows;
    const int holdWindows    = (rawStyle == 2) ? 2 : kStyleHoldWindows;

    if (styleAgreeCount >= confirmWindows
        && rawStyle != committedStyle
        && styleHoldRemaining == 0)
    {
        committedStyle = rawStyle;
        styleHoldRemaining = holdWindows;
    }

    displayStyle.store(committedStyle, std::memory_order_relaxed);
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

juce::String AccompanimentProcessor::getCurrentSectionName() const noexcept
{
    if (structureSequencer.isComplete())
        return "Complete";
    return juce::String(structureSequencer.getCurrentSectionName());
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
    // A note is "ringing" if the guitarist picked within the last ~4 s — the note
    // is still sounding even though its RMS has decayed. Pass this so the
    // structure tagger does not declare SILENT (and drop the drums) mid-note.
    const int64_t noteHoldSamples = static_cast<int64_t>(
        4.0 * cachedSampleRate.load(std::memory_order_relaxed));
    const bool noteRinging = phraseLearner.hasRecentAttack(hostSampleTime, noteHoldSamples);
    const StructureState st = structureTagger.update(
        rms, centroid, hfFlux, numSamples, energyAnalyser.getPeakRms(), noteRinging);

    const bool digitalSilence = (rms < 1.0e-6f);
    const double sr = cachedSampleRate.load(std::memory_order_relaxed);

    // ── 2. BPM (DAW transport only) ─────────────────────────────────────────
    // The DAW transport is the single source of tempo. The manual knob is used
    // only as a fallback when the host provides no transport BPM (e.g. the
    // standalone build, which has no playhead). Audio-derived tempo is not used.
    float bpmForPlayer = 120.0f;
    int64_t rawHostPos = hostSampleTime;
    bool hostRolling = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            hostRolling = pos->getIsPlaying() || pos->getIsRecording();
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

    // ── Resolved drum clock vs monotonic lock clock ─────────────────────────
    // Drums quantize to the DAW transport position (clockSample). Lock /
    // transition *durations* use the plugin's monotonic hostSampleTime so a
    // DAW loop wrap cannot freeze the schedule or silence frozen bass. Bar
    // alignment is captured once at engage (lockBarPhaseBeats) so the bass
    // re-entry still lands on the audible drum downbeat.
    const int64_t clockSample = patternPlayer.previewResolvedHostSample(rawHostPos, numSamples, hostRolling);

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

    // R1: guitar onset density / IOI for rhythm-driven pattern selection. A
    // window of ~2 bars; the PhraseLearner's existing attack ring supplies both
    // without allocation or lock (real-time safe, O(attacks)).
    const double samplesPerBeatLocal = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
    const int64_t rhythmWindowSamples = static_cast<int64_t>(8.0 * (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr);
    fv.onsetDensityPerBeat = phraseLearner.getOnsetDensityPerBeat(
        hostSampleTime, rhythmWindowSamples, samplesPerBeatLocal);
    fv.onsetIoiBeats = (samplesPerBeatLocal > 0.0)
        ? static_cast<float>(phraseLearner.getMeanIoiSamples(hostSampleTime, rhythmWindowSamples) / samplesPerBeatLocal)
        : 0.0f;

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

    float humanize = 0.35f;
    if (auto* rawHumanize = apvts.getRawParameterValue("humanize"))
        humanize = juce::jlimit(0.0f, 1.0f, rawHumanize->load());
    patternPlayer.setHumanize(humanize);

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

    const bool playRequested = playActive.load(std::memory_order_acquire);
    const bool playStartEdge = playRequested && !wasPlayOn && !playCountInActive;
    if (playStartEdge)
    {
        // Each Play press starts the form from the top after a 1-bar click.
        // Play never inherits a Record riff.
        structureSequencer.setLooping(false);
        playCountInActive = true;
        playCountInWaitingBar = true;
        playCountInStartBeat = 0.0;
        riffA = {};
        riffB = {};
        riffAPlayOriginMono = -1;
        riffBPlayOriginMono = -1;
        lockOriginMono = -1;
        lockBarPhaseBeats = 0.0;
        phraseLearner.reset();
        phraseLearner.setAutoLockEnabled(false);
        enginePhase = EnginePhase::PlayCountIn;
        grooveLockActive = false;
        riffLoopActive = false;
        grooveLockStartMono = -1;
        grooveLockEndMono = -1;
        riffHeld.store(false, std::memory_order_release);
        grooveLocked.store(false, std::memory_order_release);
        playSectionIndex.store(-1, std::memory_order_release);
        resetDrumHoldRequested.store(true, std::memory_order_release);
        guitarSilentSamples = 0;
        patternPlayer.setBeatGridBassEnabled(true);
        playFillArm.clear();
        riffAFillArm.clear();
        riffBFillArm.clear();
    }
    if (!playRequested)
    {
        // PLAY toggled off mid-count-in → cancel the count-in.
        playCountInActive = false;
        playCountInWaitingBar = false;
    }

    // Advance the Play count-in: wait for the next bar boundary, click for one
    // bar, then start the form on the downbeat.
    if (playCountInActive)
    {
        const double spbCountIn = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
        const double beatStartCountIn = static_cast<double>(clockSample) / spbCountIn;
        const double beatEndCountIn = beatStartCountIn + static_cast<double>(numSamples) / spbCountIn;
        constexpr double kBarBeats = 4.0;
        if (playCountInWaitingBar)
        {
            const double nextBar = std::ceil(beatStartCountIn / kBarBeats - 1.0e-9) * kBarBeats;
            if (nextBar < beatEndCountIn - 1.0e-9)
            {
                playCountInStartBeat = nextBar;
                playCountInWaitingBar = false;
            }
        }
        else if (beatEndCountIn >= playCountInStartBeat + kBarBeats - 1.0e-9)
        {
            playCountInActive = false;
            structureSequencer.reset();
            enginePhase = EnginePhase::PlaySection;
            phraseLearner.setAutoLockEnabled(false);
            patternPlayer.setBeatGridBassEnabled(true);
        }
    }

    bool playOn = playRequested && !playCountInActive;
    bool playEndedThisBlock = false;
    if (playOn && structureSequencer.isComplete())
    {
        playActive.store(false, std::memory_order_release);
        playOn = false;
        playEndedThisBlock = true;
    }

    int effectivePatternIdx = patternIdx;
    if (playOn)
    {
        // Sample last/penultimate *before* advance: the sequencer increments
        // barsElapsed at the wrap in this block's last samples, which still
        // belong to the outgoing bar on the host grid. Arming after advance
        // queued fills one bar late (expired before beat 4 of the real last bar).
        const bool fillLast = structureSequencer.isLastBar();
        const bool fillPenultimate =
            structureSequencer.getBarsElapsed() == structureSequencer.getBarsInSection() - 2;
        const double spbPlay = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
        const double lastBarOriginBeat = playLastBarOriginBeat(clockSample, spbPlay);

        structureSequencer.advance(numSamples, bpmForPlayer, sr);
        if (structureSequencer.isComplete())
        {
            playActive.store(false, std::memory_order_release);
            playOn = false;
            playEndedThisBlock = true;
            enginePhase = EnginePhase::Idle;
            playSectionIndex.store(-1, std::memory_order_release);
        }
        else
        {
            enginePhase = EnginePhase::PlaySection;
            const auto* secName = structureSequencer.getCurrentSectionName();
            const auto orderedPool = PatternRules::orderedSectionPatternPoolForGenre(secName, genreId);
            const int secIndex = structureSequencer.getCurrentSectionIndex();
            const int barsElapsedNow = structureSequencer.getBarsElapsed();
            playSectionIndex.store(secIndex, std::memory_order_release);
            if (secIndex != lastSectionIndex || !wasPlayOn
                || barsElapsedNow < lastSeenBarsElapsed)
            {
                lastSectionIndex = secIndex;
                sectionEntryBar.store(structureSequencer.getGlobalBarCount(),
                                      std::memory_order_release);
                lastPlayedPoolPattern = -1;
                lastPlayGrooveSlot = -1;
                resetDrumHoldRequested.store(true, std::memory_order_release);
                patternPlayer.armTransitionCrash();
            }
            lastSeenBarsElapsed = barsElapsedNow;

            // T4.1: rotation is the source of truth in Play. Re-pick only when
            // the groove slot advances so the inference hold cannot fight the
            // phrase length. T6.1: a large RMS step forces one re-pick (applied
            // at the next beat via alignToBeat) so the kit reacts within ~250 ms.
            const int barsPerGroove = PatternRules::barsPerGrooveForSection(secName);
            const int grooveSlot = barsElapsedNow / juce::jmax(1, barsPerGroove);
            const bool gestureChange = std::abs(rmsDelta) > 0.6f;
            if (gestureChange)
                lastPlayGrooveSlot = -1;
            if (grooveSlot != lastPlayGrooveSlot || lastPlayedPoolPattern < 0)
            {
                const unsigned seed = static_cast<unsigned>(
                    sectionEntryBar.load(std::memory_order_relaxed));
                int picked = PatternRules::pickPoolPattern(
                    orderedPool, seed, grooveSlot, lastPlayedPoolPattern);
                if (PatternRules::poolContains(orderedPool, patternIdx)
                    && patternIdx != lastPlayedPoolPattern
                    && patternIdx > 0)
                    picked = patternIdx;
                if (picked < 0)
                    picked = PatternRules::constrainToPool(patternIdx, orderedPool, st);
                lastPlayedPoolPattern = picked;
                lastPlayGrooveSlot = grooveSlot;
            }
            if (lastPlayedPoolPattern >= 0)
                effectivePatternIdx = lastPlayedPoolPattern;
            const unsigned seed = static_cast<unsigned>(structureSequencer.getGlobalBarCount())
                                ^ static_cast<unsigned>(secIndex * 31u);
            updateOutgoingFill(playFillArm, fillLast, fillPenultimate, rms, seed, lastBarOriginBeat);
        }
    }
    else
    {
        playFillArm.clear();
    }
    if (!playOn && enginePhase == EnginePhase::RiffBListen)
    {
        // T6.3: rotate the contrast pool instead of pinning drumB0.
        const int64_t spBarB = static_cast<int64_t>(
            4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
        const int barsElapsedB = (spBarB > 0 && transitionStartMono >= 0)
            ? static_cast<int>((hostSampleTime - transitionStartMono) / spBarB) : 0;
        const int bpg = PatternRules::barsPerGrooveForSection(transitionSectionNameStr);
        const int grooveSlotB = barsElapsedB / juce::jmax(1, bpg);
        if (grooveSlotB != lastBListenGrooveSlot || lastBListenPoolPattern < 0)
        {
            const unsigned seed = static_cast<unsigned>(
                transitionSectionNumberLocal * 31
                + static_cast<unsigned>(transitionStartMono & 0x7fffffff));
            int picked = PatternRules::pickPoolPattern(
                transitionPool, seed, grooveSlotB, lastBListenPoolPattern);
            if (PatternRules::poolContains(transitionPool, patternIdx)
                && patternIdx != lastBListenPoolPattern
                && patternIdx > 0)
                picked = patternIdx;
            if (picked < 0)
                picked = PatternRules::constrainToPool(patternIdx, transitionPool, st);
            lastBListenPoolPattern = picked;
            lastBListenGrooveSlot = grooveSlotB;
        }
        if (lastBListenPoolPattern >= 0)
            effectivePatternIdx = lastBListenPoolPattern;
    }
    else if (!playOn && enginePhase == EnginePhase::RiffBLocked)
    {
        if (drumB > 0)
            effectivePatternIdx = drumB;
        else if (drumB0 > 0)
            effectivePatternIdx = drumB0;
    }
    else if (!playOn && enginePhase == EnginePhase::RiffA && drumA > 0)
    {
        effectivePatternIdx = drumA;
    }
    wasPlayOn = playOn;
    if (!playOn)
    {
        // T4.2: follow-mode "section instance" is a structure-state entry so
        // identical features in two different states can draw different top-K
        // neighbours, while a single state hold stays on one seed.
        const int stIdx = static_cast<int>(st);
        if (stIdx != lastFollowStateIndex)
        {
            lastFollowStateIndex = stIdx;
            const int64_t spBar = static_cast<int64_t>(
                4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
            const int bar = (spBar > 0)
                ? static_cast<int>(hostSampleTime / spBar) : 0;
            sectionEntryBar.store(bar, std::memory_order_release);
        }
    }
    {
        const bool freezeSelect = (enginePhase != EnginePhase::Idle
                                   && enginePhase != EnginePhase::PlaySection
                                   && enginePhase != EnginePhase::RiffBListen);
        patternSelectFrozen.store(freezeSelect, std::memory_order_release);
    }
    patternPlayer.setPatternIndex(effectivePatternIdx);

    // T6.1: gesture commits apply at the next beat, not the next bar.
    if (std::abs(rmsDelta) > 0.6f
        && playOn
        && effectivePatternIdx != patternPlayer.getActivePatternIndex())
    {
        PatternPlayer::GrooveCommit beatCommit{};
        beatCommit.patternIndex = effectivePatternIdx;
        beatCommit.alignToBeat = true;
        patternPlayer.queueGrooveCommit(beatCommit);
    }

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

    // Guitarist-energy dynamic: a ~500 ms RMS swell so the kit tracks phrases,
    // not per-block transients (T3.2). The map is bidirectional: silence sits
    // at 0.94 (≈ −0.5 dB), a hot signal reaches 1.20 (≈ +1.6 dB).
    const double srNow = cachedSampleRate.load(std::memory_order_relaxed);
    const float blockSec = static_cast<float>(numSamples)
                         / static_cast<float>(juce::jmax(1.0, srNow));
    constexpr float kEnergyTauSec = 0.50f;
    const float alpha = 1.0f - std::exp(-blockSec / kEnergyTauSec);
    guitarEnergyRms_ += alpha * (rms - guitarEnergyRms_);
    patternPlayer.setGuitarEnergy(PatternPlayer::guitarEnergyFromRms(guitarEnergyRms_));

    // ── 7. Silence gating ───────────────────────────────────────────────────
    // Item: "only starts listening at Record riff / Play" — the plugin is idle
    // (silent, not learning) until the user arms it via Play or a riff capture.
    // Once armed it keeps playing through quiet guitar (T6.4: the lock exists
    // to accompany independently of the player). Transport-level digital silence
    // still cuts.
    const bool armActive = playOn
        || playCountInActive
        || riffCaptureActive.load(std::memory_order_acquire)
        || riffCaptureStart.load(std::memory_order_acquire)
        || grooveLocked.load(std::memory_order_acquire)
        || transitionSectionActive.load(std::memory_order_acquire);
    const bool trulySilent = digitalSilence || !armActive;

    int previewRem = debugPreviewSamplesRemaining.load(std::memory_order_acquire);
    if (previewRem > 0)
        debugPreviewSamplesRemaining.store(juce::jmax(0, previewRem - numSamples), std::memory_order_release);
    const bool capturingRiff = riffCaptureActive.load(std::memory_order_acquire)
        || riffCaptureStart.load(std::memory_order_acquire);
    patternPlayer.setClickTrack(capturingRiff || playCountInActive);
    patternPlayer.setStructureSilent((trulySilent && previewRem <= 0) && !capturingRiff);

    // ── 8. Dequeue groove commit ───────────────────────────────────────────
    // Inference → audio thread handoff for bar-quantized pattern changes with
    // transition fills. Honored only when the sequencer is not overriding
    // pattern selection (i.e. when the Play button is off).
    PatternPlayer::GrooveCommit commit{};
    bool gotCommit = false;
    while (grooveCommitQueue.try_dequeue(commit)) gotCommit = true;
    (void)gotCommit;  // T4.1: Play rotation is authoritative. T6.1 gesture
                      // commits are queued on the audio thread (alignToBeat).
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

        const double samplesPerBeat = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
        const double beatStart = static_cast<double>(clockSample) / samplesPerBeat;
        const double beatEnd = beatStart + static_cast<double>(numSamples) / samplesPerBeat;

        // T2.2: a seek / loop wrap while locked re-latches bar phase so frozen
        // bass stays on the host grid, without restarting the remaining duration.
        {
            const int64_t slack = static_cast<int64_t>(numSamples) * 2;
            if (lastClockSample >= 0)
            {
                const int64_t expected = lastClockSample + static_cast<int64_t>(lastClockBlockSamples);
                const int64_t delta = clockSample - expected;
                if ((delta < -slack || delta > slack)
                    && (enginePhase == EnginePhase::RiffA
                        || enginePhase == EnginePhase::RiffBLocked
                        || postLockPhase == PostLockPhase::TransitionHold))
                    reanchorLockClockOnJump(clockSample, samplesPerBeat);
            }
            lastClockSample = clockSample;
            lastClockBlockSamples = numSamples;
        }

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
        (void)blockPeak;

        auto resetTransitionCycle = [this]() noexcept
        {
            postLockPhase = PostLockPhase::Idle;
            transitionSectionActive.store(false, std::memory_order_release);
            transitionSectionNumberLocal = 0;
            transitionSectionNumber.store(0, std::memory_order_release);
            transitionSectionNameStr = "VERSE";
            transitionSectionName.store("VERSE", std::memory_order_release);
            transitionStartMono = -1;
            transitionEndMono = -1;
            transitionCutShortArmed = false;
            sameRiffMatchCount = 0;
            lastRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
            firstRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
            lastBListenPoolPattern = -1;
            lastBListenGrooveSlot = -1;
            transitionBarsRemaining.store(0, std::memory_order_relaxed);
            transitionBarsTotal.store(0, std::memory_order_relaxed);
            for (int i = 0; i < kMaxTransitionSlots; ++i)
            {
                transitionSlotNames[i] = nullptr;
                transitionSlotPinned[i] = false;
            }
        };

        auto abortRiffCapture = [this]() noexcept
        {
            phraseLearner.cancelUserCapture();
            phraseLearner.setHoldActive(false);
            riffCaptureActive.store(false, std::memory_order_release);
            riffCaptureNoteCount.store(0, std::memory_order_relaxed);
            riffCaptureBar.store(0, std::memory_order_relaxed);
            riffCapturePhase = RiffCapturePhase::Idle;
            patternPlayer.setClickTrack(false);
            if (enginePhase == EnginePhase::RecWaitBar
                || enginePhase == EnginePhase::RecCountIn
                || enginePhase == EnginePhase::RecCapture)
                enginePhase = EnginePhase::Idle;
        };

        // Set true on the block a transition re-engages the locked riff (A).
        auto enterRiffA = [this, lockDuration, lockBars, clockSample, patternIdx, samplesPerBeat]() noexcept
        {
            if (!riffA.valid)
                phraseLearner.exportPattern(riffA);
            if (!riffA.valid)
                return;
            enginePhase = EnginePhase::RiffA;
            latchLockClock(clockSample, samplesPerBeat);
            riffAPlayOriginMono = frozenRiffOriginMono(samplesPerBeat);
            grooveLockActive = true;
            riffLoopActive = true;
            grooveLockEndMono = hostSampleTime + lockDuration;
            grooveLockStartMono = hostSampleTime;
            lockBarsTotal.store(lockBars, std::memory_order_relaxed);
            lastRiffMatchSample = hostSampleTime;
            grooveLockReleaseArmed = false;
            drumA = (drumA > 0) ? drumA : patternIdx;
            phraseLearner.setHoldActive(true);
            phraseLearner.setAutoLockEnabled(false);
            patternPlayer.setBeatGridBassEnabled(false);
            patternPlayer.setPatternIndex(drumA);
            riffHeld.store(true, std::memory_order_release);
            grooveLocked.store(true, std::memory_order_release);
            guitarSilentSamples = 0;
        };

        auto finishRiffCapture = [this, enterRiffA, abortRiffCapture]() noexcept
        {
            flushPendingCaptureSlot();
            const bool ok = phraseLearner.commitGridCapture();
            riffCaptureActive.store(false, std::memory_order_release);
            riffCapturePhase = RiffCapturePhase::Idle;
            patternPlayer.setClickTrack(false);
            if (ok)
            {
                phraseLearner.exportPattern(riffA);
                drumA = latestPatternIndex.load(std::memory_order_acquire);
                if (drumA <= 0)
                    drumA = 1;
                enterRiffA();
            }
            else
                abortRiffCapture();
        };

        const bool forgetNow = riffForget.exchange(false, std::memory_order_acq_rel);
        // Play-once: when the Sections form finishes, drop back to idle. Do not
        // keep a riff learned during the song, and do not fall into generative
        // auto-lock / follow-mode accompaniment. The next Play or Record arms
        // the engine again. Forget cancels the riff loop the same way.
        if (forgetNow || playEndedThisBlock)
        {
            abortRiffCapture();
            resetTransitionCycle();
            riffA = {};
            riffB = {};
            riffAPlayOriginMono = -1;
            riffBPlayOriginMono = -1;
            lockOriginMono = -1;
            lockBarPhaseBeats = 0.0;
            drumA = drumB0 = drumB = 0;
            phraseLearner.reset();
            phraseLearner.setAutoLockEnabled(false);
            enginePhase = EnginePhase::Idle;
            grooveLockActive = false;
            riffLoopActive = false;
            grooveLockStartMono = -1;
            grooveLockEndMono = -1;
            grooveLockReleaseArmed = false;
            lastRiffMatchSample = std::numeric_limits<int64_t>::min() / 2;
            prevPhraseLocked = false;
            grooveLocked.store(false, std::memory_order_release);
            riffHeld.store(false, std::memory_order_release);
            guitarSilentSamples = 0;
            patternPlayer.setBeatGridBassEnabled(true);
            playSectionIndex.store(-1, std::memory_order_release);
            playFillArm.clear();
            riffAFillArm.clear();
            riffBFillArm.clear();
        }

        if (riffCaptureStart.exchange(false, std::memory_order_acq_rel))
        {
            resetTransitionCycle();
            riffA = {};
            riffB = {};
            riffAPlayOriginMono = -1;
            riffBPlayOriginMono = -1;
            lockOriginMono = -1;
            lockBarPhaseBeats = 0.0;
            drumA = drumB0 = drumB = 0;
            phraseLearner.beginGridCapture();
            resetSlotOnsetTracker();
            phraseLearner.setAutoLockEnabled(false);
            riffCaptureActive.store(true, std::memory_order_release);
            riffCaptureNoteCount.store(0, std::memory_order_relaxed);
            riffCaptureBar.store(0, std::memory_order_relaxed);
            riffCapturePhase = RiffCapturePhase::WaitBar;
            enginePhase = EnginePhase::RecWaitBar;
            riffCountInStartBeat = 0.0;
            grooveLockActive = false;
            riffLoopActive = false;
            grooveLockStartMono = -1;
            grooveLockEndMono = -1;
            grooveLockReleaseArmed = false;
            grooveLocked.store(false, std::memory_order_release);
            riffHeld.store(false, std::memory_order_release);
            phraseLearner.setHoldActive(false);
            patternPlayer.setClickTrack(true);
            patternPlayer.setBeatGridBassEnabled(false);
            guitarSilentSamples = 0;
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
                    enginePhase = EnginePhase::RecCountIn;
                }
            }

            if (riffCapturePhase == RiffCapturePhase::CountIn
                && beatEnd >= riffCountInStartBeat + kCountInBeats - 1.0e-9)
            {
                riffCapturePhase = RiffCapturePhase::Recording;
                enginePhase = EnginePhase::RecCapture;
            }

            if (riffCapturePhase == RiffCapturePhase::Recording)
            {
                const double recStart = riffCountInStartBeat + kCountInBeats;
                const double recEnd = recStart + kRecordBeats;
                const double cap0 = juce::jmax(beatStart, recStart);
                const double cap1 = juce::jmin(beatEnd, recEnd);
                if (cap1 > cap0)
                {
                    // Capture the instantaneous pitch class (not only the held
                    // root) so the recorded riff keeps its melodic contour for
                    // note-for-note mirroring. YIN octave-flips on distorted
                    // guitar, but pitch CLASS is octave-invariant; when the
                    // estimate is unreliable, fall back to the stable root class.
                    int bassMidi = (pcForBass != INT_MIN) ? 36 + pcForBass : 36;
                    const float instPitch = pitchEstimator.getMidiNote();
                    const float instConf  = pitchEstimator.getConfidence();
                    if (instConf > 0.3f)
                    {
                        const int pc = ((static_cast<int>(std::round(instPitch)) % 12) + 12) % 12;
                        bassMidi = 36 + pc;
                    }
                    stampLearnerGridSlots(in, numSamples, cap0, cap1, samplesPerBeat,
                                          recStart, bassMidi, false);
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

        if (enginePhase == EnginePhase::RiffBListen && phraseLearner.isGridListening()
            && samplesPerBeat > 0.0 && transitionStartMono >= 0)
        {
            int bassMidi = (pcForBass != INT_MIN) ? 36 + pcForBass : 36;
            const float instPitch = pitchEstimator.getMidiNote();
            const float instConf  = pitchEstimator.getConfidence();
            if (instConf > 0.3f)
            {
                const int pc = ((static_cast<int>(std::round(instPitch)) % 12) + 12) % 12;
                bassMidi = 36 + pc;
            }
            const double elapsedMonoBeats =
                static_cast<double>(hostSampleTime - transitionStartMono) / samplesPerBeat;
            const double originBeat = beatStart - elapsedMonoBeats;
            stampLearnerGridSlots(in, numSamples, beatStart, beatEnd, samplesPerBeat,
                                  originBeat, bassMidi, true);
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
            && !phraseLearner.isGridCapturing() && !phraseLearner.isGridListening()
            && enginePhase != EnginePhase::RiffBListen
            && enginePhase != EnginePhase::RiffBLocked)
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

        if (postLockPhase == PostLockPhase::TransitionHold)
        {
            const int64_t twoBeats = static_cast<int64_t>(
                2.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
            if (phraseLearner.justMatchedReference())
            {
                if (sameRiffMatchCount == 0)
                    firstRefMatchMono = hostSampleTime;
                lastRiffMatchSample = hostSampleTime;
                lastRefMatchMono = hostSampleTime;
                ++sameRiffMatchCount;
            }
            else if (lastRefMatchMono > 0
                     && hostSampleTime - lastRefMatchMono > twoBeats)
            {
                sameRiffMatchCount = 0;
                firstRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
            }
        }
        else if (phraseLearner.justMatchedRiff()
            && enginePhase != EnginePhase::RiffBListen
            && enginePhase != EnginePhase::RiffBLocked)
        {
            lastRiffMatchSample = hostSampleTime;
        }

        const bool phraseLockEdge = (phraseLocked && !prevPhraseLocked);
        prevPhraseLocked = phraseLocked;

        const bool capturingHeld = riffCaptureActive.load(std::memory_order_acquire);
        if (playOn)
        {
            grooveLockActive = false;
            riffLoopActive = false;
            grooveLockStartMono = -1;
            grooveLockEndMono = -1;
            grooveLockReleaseArmed = false;
            if (playStartEdge)
                resetTransitionCycle();
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
        else if (enginePhase == EnginePhase::RiffA && !capturingHeld)
        {
            if (grooveLockActive && grooveLockStartMono >= 0 && samplesPerBar > 0)
            {
                const int total = lockBarsTotal.load(std::memory_order_relaxed);
                const int current = juce::jlimit(1, juce::jmax(1, total),
                    static_cast<int>((hostSampleTime - grooveLockStartMono) / samplesPerBar) + 1);
                const double spbA = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
                double currentBarStart = (spbA > 0.0)
                    ? std::floor(static_cast<double>(clockSample) / spbA / 4.0) * 4.0 : 0.0;
                if (currentBarStart < 0.0)
                    currentBarStart += 4.0;
                const double lastBarOriginA = (current == total)
                    ? currentBarStart : currentBarStart + 4.0;
                const unsigned seedA = static_cast<unsigned>(current)
                                     ^ (static_cast<unsigned>(total) * 31u);
                updateOutgoingFill(riffAFillArm, current == total, current == total - 1,
                                   rms, seedA, lastBarOriginA);
            }
            if (grooveLockEndMono >= 0 && hostSampleTime >= grooveLockEndMono)
            {
                grooveLockActive = false;
                grooveLockReleaseArmed = true;
            }
        }
        else if (enginePhase == EnginePhase::RiffBListen && phraseLockEdge)
        {
            flushPendingCaptureSlot();
            phraseLearner.exportPattern(riffB);
            if (riffB.valid)
            {
                const int oneShot = bLockPick.exchange(-1, std::memory_order_acq_rel);
                drumB = (oneShot >= 0)
                    ? PatternRules::constrainToPool(oneShot, transitionPool, st)
                    : (lastBListenPoolPattern > 0 ? lastBListenPoolPattern : drumB0);
                phraseLearner.cancelLiveGridListen();
                phraseLearner.setAutoLockEnabled(false);
                phraseLearner.setHoldActive(true);
                enginePhase = EnginePhase::RiffBLocked;
                latchLockClock(clockSample, samplesPerBeat);
                riffBPlayOriginMono = frozenRiffOriginMono(samplesPerBeat);
                patternPlayer.setBeatGridBassEnabled(false);
                patternPlayer.setPatternIndex(drumB > 0 ? drumB : drumB0);
                riffHeld.store(true, std::memory_order_release);
            }
        }
        else if (enginePhase == EnginePhase::RiffBLocked && !phraseLearner.isLocked())
        {
            // T6.3: drift-unlock after N bars of non-matching attacks unfreezes B.
            enginePhase = EnginePhase::RiffBListen;
            phraseLearner.setAutoLockEnabled(true);
            patternPlayer.setBeatGridBassEnabled(true);
            riffB = {};
            riffBPlayOriginMono = -1;
        }

        grooveLocked.store(enginePhase == EnginePhase::RiffA, std::memory_order_release);
        riffLoopActive = (enginePhase == EnginePhase::RiffA
                       || enginePhase == EnginePhase::RiffBListen
                       || enginePhase == EnginePhase::RiffBLocked);
        grooveLockActive = (enginePhase == EnginePhase::RiffA);
        phraseLearner.setHoldActive(enginePhase == EnginePhase::RiffA
                                 || enginePhase == EnginePhase::RiffBLocked);
        riffHeld.store(riffA.valid || phraseLearner.isLocked(), std::memory_order_release);

        // ── Riff-lock hold progress (UI): bar done / bars remaining ───────────
        // While locked, publish the 1-based current bar and the bars left before
        // the transition fires. Zeroed when not locked so the UI hides it.
        if (grooveLockActive && grooveLockStartMono >= 0 && samplesPerBar > 0)
        {
            const int64_t elapsed = hostSampleTime - grooveLockStartMono;
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
        // instead of releasing straight back to the listener. Each contrast slot
        // (B/C/D/E) is pinned to the groove family chosen on its first visit and
        // reused on every wrap, so SECTIONS=1 is a stable A-B-A-B (not a new
        // family each cycle). A new slot avoids the riff's own family and every
        // already-pinned slot. Pins are cleared on Forget / Record / prepareToPlay.
        // `transitionSections` is how many distinct contrasts to visit before
        // wrapping: 2 → A-B-A-C-A-B-A-C…
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
            int maxSections = 2;
            if (auto* raw = apvts.getRawParameterValue("transitionSections"))
                maxSections = juce::jlimit(1, 4, static_cast<int>(std::lround(raw->load())));
            if (transitionSectionNumberLocal >= maxSections)
                transitionSectionNumberLocal = 0;

            // Slot-pinned contrast selection: capture the slot index BEFORE the
            // counter is incremented so B/C/D/E always map to the same slot index.
            const int slot = juce::jlimit(0, kMaxTransitionSlots - 1, transitionSectionNumberLocal);
            PatternRules::TransitionSection ts{};
            if (transitionSlotPinned[slot])
            {
                // Slot already visited — reuse the pinned family for this slot.
                ts.name = transitionSlotNames[slot];
                ts.pool = PatternRules::orderedSectionPatternPoolForGenre(transitionSlotNames[slot], genreIdT);
            }
            else
            {
                // First visit: pick a family that avoids the riff's own feel AND
                // every already-pinned slot (the multi-avoid overload ignores
                // nullptr / empty entries so unpinned slots are no-ops).
                ts = PatternRules::pickNextSectionAfterLock(
                    lockedPat, genreIdT, transitionSlotNames, kMaxTransitionSlots);
                transitionSlotNames[slot] = ts.name;
                transitionSlotPinned[slot] = true;
            }

            postLockPhase = PostLockPhase::TransitionHold;
            transitionPool = ts.pool;
            transitionSectionNameStr = ts.name;
            drumB0 = PatternRules::contrastHomePattern(
                drumA > 0 ? drumA : lockedPat, ts.pool);
            drumB = drumB0;
            riffB = {};
            riffBPlayOriginMono = -1;
            if (!riffA.valid)
                phraseLearner.exportPattern(riffA);
            phraseLearner.reset();
            phraseLearner.beginLiveGridListen();
            if (riffA.valid)
                phraseLearner.setMatchReference(riffA);
            resetSlotOnsetTracker();
            phraseLearner.setAutoLockEnabled(true);
            enginePhase = EnginePhase::RiffBListen;
            // Seed rotation with the first contrast groove so bar 1 is not
            // immediately re-picked (fills and authored bass stay on drumB0).
            lastBListenPoolPattern = drumB0;
            lastBListenGrooveSlot = 0;
            transitionCutShortArmed = false;
            sameRiffMatchCount = 0;
            lastRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
            firstRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
            grooveLockActive = false;
            riffLoopActive = true;
            requestBLockPick.store(true, std::memory_order_release);
            bLockPick.store(-1, std::memory_order_release);
            patternPlayer.setBeatGridBassEnabled(true);
            patternPlayer.setPatternIndex(drumB0);
            transitionSectionName.store(ts.name, std::memory_order_release);
            ++transitionSectionNumberLocal;
            transitionSectionNumber.store(transitionSectionNumberLocal, std::memory_order_release);
            transitionBarsTotalLocal = transitionBars;
            transitionBarsTotal.store(transitionBars, std::memory_order_release);
            transitionBarsRemaining.store(transitionBars, std::memory_order_release);
            latchLockClock(clockSample, samplesPerBeat);
            transitionStartMono = hostSampleTime;
            transitionEndMono = hostSampleTime + static_cast<int64_t>(transitionBars) * samplesPerBarT;
            transitionSectionActive.store(true, std::memory_order_release);

            patternPlayer.armTransitionCrash();
            riffAFillArm.clear();
        }

        // ── A5.2: run the transition hold. ────────────────────────────────────
        if (postLockPhase == PostLockPhase::TransitionHold)
        {
            // T6.2 / END_USER_STRESS_TEST §B4: replaying the *same* riff cuts
            // the contrast short at the next bar. A different riff does not.
            // The clock still guarantees an exit at transitionEndMono.
            const int64_t samplesPerBarC =
                static_cast<int64_t>(4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
            const bool sameRiffReturned = sameRiffMatchCount >= 4
                                       && lastRiffMatchSample > transitionStartMono
                                       && firstRefMatchMono > 0
                                       && (lastRefMatchMono - firstRefMatchMono)
                                            >= static_cast<int64_t>(samplesPerBeat);
            if (sameRiffReturned && !transitionCutShortArmed && samplesPerBarC > 0)
            {
                transitionCutShortArmed = true;
                const int64_t elapsed = std::max(int64_t{ 0 }, hostSampleTime - transitionStartMono);
                const int64_t intoBar = elapsed % samplesPerBarC;
                const int64_t toNextBar = (intoBar == 0) ? 0 : (samplesPerBarC - intoBar);
                const int64_t cutAt = hostSampleTime + toNextBar;
                if (transitionEndMono < 0 || cutAt < transitionEndMono)
                    transitionEndMono = cutAt;
            }
            if (transitionEndMono >= 0 && hostSampleTime >= transitionEndMono)
            {
                postLockPhase = PostLockPhase::Idle;
                transitionSectionActive.store(false, std::memory_order_release);
                transitionCutShortArmed = false;
                riffB = {};
                riffBPlayOriginMono = -1;
                riffBFillArm.clear();
                phraseLearner.cancelLiveGridListen();
                patternPlayer.armTransitionCrash();
                enterRiffA();
            }
            else
            {
                // Countdown for the UI.
                const int64_t remaining = transitionEndMono - hostSampleTime;
                const int barsLeft = (samplesPerBarC > 0)
                    ? static_cast<int>((remaining + samplesPerBarC - 1) / samplesPerBarC)
                    : 0;
                transitionBarsRemaining.store(juce::jmax(0, barsLeft), std::memory_order_release);
                const double spbB = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
                double currentBarStartB = (spbB > 0.0)
                    ? std::floor(static_cast<double>(clockSample) / spbB / 4.0) * 4.0 : 0.0;
                if (currentBarStartB < 0.0)
                    currentBarStartB += 4.0;
                const double lastBarOriginB = (barsLeft == 1)
                    ? currentBarStartB : currentBarStartB + 4.0;
                const unsigned seedB = static_cast<unsigned>(transitionSectionNumberLocal)
                                     ^ (static_cast<unsigned>(barsLeft) * 17u);
                updateOutgoingFill(riffBFillArm, barsLeft == 1, barsLeft == 2,
                                   rms, seedB, lastBarOriginB);
            }
        }

        // ── 9c. Live riff mirror ─────────────────────────────────────────────
        // While the guitarist is audible, the bass mirrors each detected attack
        // immediately — "play along with what I'm playing". Previously this
        // required a dense chug (≥2 attacks in the last 2 bars); below that the
        // bass dropped to a fixed beat 1/3 root drone that played ON TOP of the
        // mirrored notes, so sparse or pattern-following figures sounded
        // disconnected. Now ANY audible picking arms the listening bass, and a
        // short grace window keeps it armed across brief pauses so it does not
        // flap back and forth to the drone mid-phrase.
        const bool listenBass = (enginePhase == EnginePhase::PlaySection
                              || enginePhase == EnginePhase::RiffBListen);
        patternPlayer.setBeatGridBassEnabled(listenBass);

        int bassRoot = 36;
        if (semitoneOffset != INT_MIN)
        {
            bassRoot = 36 + semitoneOffset;
            while (bassRoot < 28) bassRoot += 12;
            while (bassRoot > 55) bassRoot -= 12;
        }
        int notesPerBar = 2;
        if (std::strcmp(section, "CHORUS") == 0 || std::strcmp(section, "SOLO") == 0)
            notesPerBar = 4;
        else if (std::strcmp(section, "INTRO") == 0 || std::strcmp(section, "OUTRO") == 0
                 || std::strcmp(section, "BREAKDOWN") == 0)
            notesPerBar = 2;
        patternPlayer.setBassParams(bassRoot, notesPerBar);

        if (playOn && structureSequencer.isLastBar())
            patternPlayer.armBassLeadIn();

        // Guitar-stop: live-mirror bass stays gated (no ghost notes on a
        // breath). T6.4: locked A/B accompaniment is independent of picking —
        // do not silence the kit or skip frozen-riff emission.
        if (silentNow || rms < 0.003f)
            guitarSilentSamples += numSamples;
        else
            guitarSilentSamples = 0;
        const bool guitarStopped = guitarSilentSamples >= static_cast<int64_t>(sr);

        const double samplesPerBeatQ = (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
        const int durationSamples = juce::jmax(1, static_cast<int>(0.85 * samplesPerBeatQ));
        int mirrorNote = bassNote.midiNote;
        float mirrorVel = bassNote.velocity;
        bool shouldTrigger = bassNote.trigger && !guitarStopped;
        if (enginePhase == EnginePhase::RiffA
            && !riffCaptureActive.load(std::memory_order_acquire))
        {
            emitFrozenRiff(riffA, riffAPlayOriginMono, numSamples,
                           static_cast<double>(bpmForPlayer), sr,
                           hostSampleTime, bassTranspose);
        }
        else if (enginePhase == EnginePhase::RiffBLocked)
        {
            emitFrozenRiff(riffB, riffBPlayOriginMono, numSamples,
                           static_cast<double>(bpmForPlayer), sr,
                           hostSampleTime, bassTranspose);
        }
        else if (shouldTrigger && !riffCaptureActive.load(std::memory_order_acquire)
            && !phraseLearner.isGridCapturing()
            && listenBass)
        {
            const int note = mirrorNote + bassTranspose;
            const double sixteenthQ = samplesPerBeatQ / 4.0;
            const double maxSnap = std::min(0.030 * sr, sixteenthQ * 0.5);
            int offset = 0;
            if (sixteenthQ > 1.0)
            {
                const int64_t nearest = static_cast<int64_t>(
                    std::llround(static_cast<double>(clockSample) / sixteenthQ)
                    * sixteenthQ);
                const int64_t delta = nearest - clockSample; // negative ⇒ already passed
                if (std::abs(static_cast<double>(delta)) <= maxSnap
                    && delta >= 0 && delta < numSamples)
                    offset = static_cast<int>(delta);
                // else offset 0: never add sixteenthQ to chase the next 16th.
            }
            patternPlayer.triggerLearnedBassNote(note, mirrorVel, offset, durationSamples);
        }
    }

    // ── 10. Process MIDI ────────────────────────────────────────────────────
    patternPlayer.process(midi, numSamples, rawHostPos, hostRolling);

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

    if (!playOn && enginePhase == EnginePhase::RiffBListen && lastBListenPoolPattern >= 0)
    {
        effectivePatternIdx = lastBListenPoolPattern;
        patternPlayer.setPatternIndex(effectivePatternIdx);
    }
    else if (!playOn && enginePhase == EnginePhase::RiffBLocked)
    {
        if (drumB > 0)
            effectivePatternIdx = drumB;
        else if (drumB0 > 0)
            effectivePatternIdx = drumB0;
        patternPlayer.setPatternIndex(effectivePatternIdx);
    }
    else if (!playOn && enginePhase == EnginePhase::RiffA && drumA > 0)
    {
        effectivePatternIdx = drumA;
        patternPlayer.setPatternIndex(effectivePatternIdx);
    }

    // Display updates
    displayBpm.store(bpmForPlayer, std::memory_order_relaxed);
    displayRms.store(rms, std::memory_order_relaxed);
    displayCentroid.store(centroid, std::memory_order_relaxed);
    displayHfFlux.store(hfFlux, std::memory_order_relaxed);
    displayNoiseFloor.store(structureTagger.getNoiseFloorRms(), std::memory_order_relaxed);
    displayStateIndex.store(static_cast<int>(st), std::memory_order_relaxed);
    displayPatternIndex.store(armActive ? effectivePatternIdx : 0, std::memory_order_relaxed);

    // ── Unified section-progress display (audio thread → UI) ─────────────────
    // One consistent "bar X of Y / N left" for Play, riff lock (A), and the
    // post-lock transition (B/C), so the guitarist can anticipate the change.
    // `stRemaining` is always `total - currentBar` (bars strictly after the bar
    // being played), so the countdown is self-consistent across all phases.
    const int64_t samplesPerBarNow =
        static_cast<int64_t>(4.0 * 60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer)) * sr);
    int phase = 0;
    int sb = 0, stTotal = 0, stRemaining = 0;
    if (playOn)
    {
        phase = static_cast<int>(SectionPhase::Play);
        stTotal = structureSequencer.getBarsInSection();
        sb = juce::jlimit(1, juce::jmax(1, stTotal),
                          structureSequencer.getBarsElapsed() + 1);
        stRemaining = juce::jmax(0, stTotal - sb);
    }
    else if (postLockPhase == PostLockPhase::TransitionHold)
    {
        phase = static_cast<int>(SectionPhase::Transition);
        stTotal = transitionBarsTotalLocal;
        sb = (samplesPerBarNow > 0 && transitionStartMono >= 0)
            ? juce::jlimit(1, juce::jmax(1, stTotal),
                           static_cast<int>((hostSampleTime - transitionStartMono) / samplesPerBarNow) + 1)
            : 1;
        stRemaining = juce::jmax(0, stTotal - sb);
    }
    else if (grooveLockActive)
    {
        phase = static_cast<int>(SectionPhase::Lock);
        stTotal = lockBarsTotal.load(std::memory_order_relaxed);
        sb = (samplesPerBarNow > 0 && grooveLockStartMono >= 0)
            ? juce::jlimit(1, juce::jmax(1, stTotal),
                           static_cast<int>((hostSampleTime - grooveLockStartMono) / samplesPerBarNow) + 1)
            : 1;
        stRemaining = juce::jmax(0, stTotal - sb);
    }
    const float progress = (stTotal > 0)
        ? static_cast<float>(sb) / static_cast<float>(stTotal) : 0.0f;
    sectionPhase.store(phase, std::memory_order_relaxed);
    sectionBar.store(sb, std::memory_order_relaxed);
    sectionBarsTotal.store(stTotal, std::memory_order_relaxed);
    sectionBarsRemaining.store(stRemaining, std::memory_order_relaxed);
    sectionProgress.store(progress, std::memory_order_relaxed);

    // Playhead fraction is computed earlier in the block from the resolved host
    // clock (so it aligns with the drums' transport grid).
    publishRiffUiSnapshot();
}

void AccompanimentProcessor::updateOutgoingFill(OutgoingFillArm& arm, bool isLast,
                                                bool isPenultimate, float rms,
                                                unsigned seed, double lastBarOriginBeat) noexcept
{
    if (isPenultimate && !arm.penultimate)
    {
        if (PatternRules::selectFillPatternForEnergy(rms, seed) == 19)
        {
            patternPlayer.armBarFillAtBeat(19, lastBarOriginBeat);
            arm.deferred19 = true;
        }
        arm.penultimate = true;
    }
    if (!isPenultimate)
        arm.penultimate = false;

    if (isLast && !arm.lastBar)
    {
        if (!arm.deferred19)
        {
            const int fill = PatternRules::selectFillPatternForEnergy(rms, seed);
            patternPlayer.armBarFillAtBeat(fill, lastBarOriginBeat);
        }
        arm.lastBar = true;
    }
    if (!isLast && !isPenultimate)
    {
        arm.lastBar = false;
        arm.deferred19 = false;
    }
}

double AccompanimentProcessor::playLastBarOriginBeat(int64_t clockSample, double samplesPerBeat) const noexcept
{
    if (samplesPerBeat <= 0.0)
        return 0.0;
    const double currentBarStart = std::floor(
        static_cast<double>(clockSample) / samplesPerBeat / 4.0) * 4.0;
    const int lastIdx = juce::jmax(0, structureSequencer.getBarsInSection() - 1);
    const int elapsed = structureSequencer.getBarsElapsed();
    const int global = structureSequencer.getGlobalBarCount();
    const double seqSpb = structureSequencer.getSamplesPerBar();
    const double acc = structureSequencer.getBarAccumulator();
    if (seqSpb <= 1.0)
        return currentBarStart + static_cast<double>(lastIdx - elapsed) * 4.0;
    const double formOrigin = static_cast<double>(clockSample)
                            - (static_cast<double>(global) * seqSpb + acc);
    const double lastStartSamples = formOrigin
        + static_cast<double>(global - elapsed + lastIdx) * seqSpb;
    return lastStartSamples / samplesPerBeat;
}

void AccompanimentProcessor::latchLockClock(int64_t transportSample, double samplesPerBeat) noexcept
{
    if (samplesPerBeat <= 0.0)
    {
        lockBarPhaseBeats = 0.0;
        lockOriginMono = hostSampleTime;
        return;
    }
    lockBarPhaseBeats = std::fmod(static_cast<double>(transportSample) / samplesPerBeat, 4.0);
    if (lockBarPhaseBeats < 0.0)
        lockBarPhaseBeats += 4.0;
    lockOriginMono = hostSampleTime;
}

int64_t AccompanimentProcessor::frozenRiffOriginMono(double samplesPerBeat) const noexcept
{
    if (lockOriginMono < 0)
        return -1;
    if (samplesPerBeat <= 0.0)
        return lockOriginMono;
    return lockOriginMono - static_cast<int64_t>(std::llround(lockBarPhaseBeats * samplesPerBeat));
}

void AccompanimentProcessor::reanchorLockClockOnJump(int64_t transportSample, double samplesPerBeat) noexcept
{
    // Duration fields stay put: a seek is a musical restart of the riff phase,
    // not a reset of how many lock/transition bars remain.
    latchLockClock(transportSample, samplesPerBeat);
    const int64_t origin = frozenRiffOriginMono(samplesPerBeat);
    if (riffAPlayOriginMono >= 0)
        riffAPlayOriginMono = origin;
    if (riffBPlayOriginMono >= 0)
        riffBPlayOriginMono = origin;
}

void AccompanimentProcessor::resetSlotOnsetTracker() noexcept
{
    prevSlotPeak_ = 0.0f;
    prevSlotEnd_ = 0.0f;
    prevSlotOccupied_ = false;
    captureSlotIndex_ = -1;
    captureSlotPeak_ = 0.0f;
    captureSlotEnd_ = 0.0f;
    captureSlotMidi_ = 36;
}

void AccompanimentProcessor::flushPendingCaptureSlot() noexcept
{
    if (captureSlotIndex_ < 0)
        return;
    const bool occupied = captureSlotPeak_ >= 0.025f;
    if (occupied)
    {
        const bool onset = !prevSlotOccupied_
            || captureSlotPeak_ > prevSlotPeak_ * 1.15f + 0.01f
            || captureSlotPeak_ > prevSlotEnd_ * 1.15f + 0.01f;
        phraseLearner.stampGridRange(
            static_cast<double>(captureSlotIndex_) * 0.25,
            static_cast<double>(captureSlotIndex_) * 0.25 + 0.25,
            captureSlotPeak_, captureSlotMidi_, onset);
        prevSlotPeak_ = captureSlotPeak_;
        // Only treat the slot as having decayed if its trailing window is
        // below the occupancy floor. A 10 ms slice of a held sine can sit
        // near a zero-crossing (~0.08) without the note having ended; using
        // that as prevSlotEnd_ would re-onset every 16th (T5.2).
        prevSlotEnd_ = (captureSlotEnd_ < 0.025f) ? captureSlotEnd_ : captureSlotPeak_;
        prevSlotOccupied_ = true;
    }
    else
    {
        prevSlotPeak_ = captureSlotPeak_;
        prevSlotEnd_ = 0.0f;
        prevSlotOccupied_ = false;
    }
    captureSlotIndex_ = -1;
}

void AccompanimentProcessor::stampLearnerGridSlots(const float* in, int numSamples,
                                                   double beatStart, double beatEnd,
                                                   double samplesPerBeat, double originBeat,
                                                   int bassMidi, bool wrapLoop) noexcept
{
    if (in == nullptr || numSamples <= 0 || samplesPerBeat <= 0.0)
        return;
    if (beatEnd <= beatStart)
        return;

    constexpr double kLoop = static_cast<double>(PhraseLearner::kGridBars) * 4.0;
    const double blockBeat0 = beatStart;

    auto stampWindow = [&](double cap0, double cap1, double origin) noexcept
    {
        if (cap1 <= cap0)
            return;
        const int slot0 = std::max(0, static_cast<int>(std::floor((cap0 - origin) * 4.0)));
        const int slot1 = std::min(PhraseLearner::kGridSlots - 1,
                                   static_cast<int>(std::floor((cap1 - origin - 1.0e-9) * 4.0)));
        for (int s = slot0; s <= slot1; ++s)
        {
            const double slotA = origin + static_cast<double>(s) * 0.25;
            const double slotB = slotA + 0.25;
            const double ov0 = juce::jmax(cap0, slotA);
            const double ov1 = juce::jmin(cap1, slotB);
            if (ov1 <= ov0)
                continue;
            const int i0 = juce::jlimit(0, numSamples - 1,
                static_cast<int>(std::floor((ov0 - blockBeat0) * samplesPerBeat)));
            const int i1 = juce::jlimit(1, numSamples,
                static_cast<int>(std::ceil((ov1 - blockBeat0) * samplesPerBeat)));
            float slotPeak = 0.0f;
            float slotEnd = 0.0f;
            const int nSamp = juce::jmax(1, i1 - i0);
            const int tail0 = i0 + (3 * nSamp) / 4;
            for (int i = i0; i < i1; ++i)
            {
                const float a = std::abs(in[i]);
                if (a > slotPeak)
                    slotPeak = a;
                if (i >= tail0 && a > slotEnd)
                    slotEnd = a;
            }
            // Accumulate per 16th, then decide onset once the slot completes.
            // A 10 ms block of a sine can sit near a zero-crossing and look like
            // a rest or a re-attack; the whole-slot peak does not.
            if (s != captureSlotIndex_)
            {
                if (captureSlotIndex_ >= 0)
                {
                    const int expected = captureSlotIndex_ + 1;
                    const int wrapped = (captureSlotIndex_ == PhraseLearner::kGridSlots - 1) ? 0 : expected;
                    flushPendingCaptureSlot();
                    if (s != expected && s != wrapped)
                        prevSlotOccupied_ = false;
                }
                captureSlotIndex_ = s;
                captureSlotPeak_ = 0.0f;
                captureSlotEnd_ = 0.0f;
            }
            captureSlotMidi_ = bassMidi;
            if (slotPeak > captureSlotPeak_)
                captureSlotPeak_ = slotPeak;
            captureSlotEnd_ = slotEnd;
        }
    };

    if (!wrapLoop)
    {
        stampWindow(beatStart, beatEnd, originBeat);
        return;
    }

    double t = beatStart - originBeat;
    const double tEnd = beatEnd - originBeat;
    if (tEnd <= 0.0)
        return;
    if (t < 0.0)
        t = 0.0;
    while (t < tEnd)
    {
        const double cycle = std::floor(t / kLoop);
        const double cycleAbs = originBeat + cycle * kLoop;
        const double chunkEndRel = juce::jmin(tEnd, (cycle + 1.0) * kLoop);
        const double cap0 = originBeat + juce::jmax(t, cycle * kLoop);
        const double cap1 = originBeat + chunkEndRel;
        stampWindow(cap0, cap1, cycleAbs);
        t = chunkEndRel;
    }
}

void AccompanimentProcessor::emitFrozenRiff(const PhraseLearner::LearnedRiff& riff,
                                            int64_t originSample, int numSamples,
                                            double bpm, double sr, int64_t clockSample,
                                            int bassTranspose) noexcept
{
    if (!riff.valid || numSamples <= 0 || bpm <= 0.0 || sr <= 0.0)
        return;
    const double loopBeats = riff.lenBeats > 0.0 ? riff.lenBeats : 16.0;
    const double spb = 60.0 / bpm * sr;
    if (spb <= 0.0)
        return;
    const int64_t origin = (originSample >= 0) ? originSample : 0;
    const int64_t blockEnd = clockSample + static_cast<int64_t>(numSamples);

    // Buffer-invariance (review T9.2): place each slot by its ABSOLUTE sample
    // (origin + loop phase) in integer arithmetic. The previous form computed a
    // block-relative beat difference in double and took std::floor of it, which
    // flipped by ±1 sample with the block grid and clamped slots that fell into
    // the neighbouring block onto the block start — the render then depended on
    // the host buffer size.
    const int64_t loopSamples = static_cast<int64_t>(std::llround(loopBeats * spb));
    if (loopSamples <= 0)
        return;

    bool anyGate = false;
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
    {
        if (riff.gate16[static_cast<size_t>(s)] > 0)
        {
            anyGate = true;
            break;
        }
    }

    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
    {
        if (!riff.occupied[static_cast<size_t>(s)])
            continue;
        const uint8_t gate = riff.gate16[static_cast<size_t>(s)];
        if (anyGate && gate == 0)
            continue;   // sustain continuation — already covered by the onset's gate
        const int gate16 = anyGate ? juce::jmax(1, static_cast<int>(gate)) : 1;
        const int duration = juce::jmax(1, static_cast<int>(std::lround(
            static_cast<double>(gate16) * 0.25 * spb * 0.9)));
        const int64_t slotOffset = static_cast<int64_t>(
            std::llround(static_cast<double>(s) * 0.25 * spb));
        // Smallest k with origin + slotOffset + k*loopSamples >= clockSample.
        const int64_t rel = clockSample - (origin + slotOffset);
        int64_t k = rel / loopSamples;
        if (k * loopSamples < rel)
            ++k;   // mathematical ceil (works for negative rel too)
        const int64_t absSample = origin + slotOffset + k * loopSamples;
        if (absSample < clockSample || absSample >= blockEnd)
            continue;
        const int offset = static_cast<int>(absSample - clockSample);
        const int note = riff.midi[static_cast<size_t>(s)] + bassTranspose;
        patternPlayer.triggerLearnedBassNote(note, 0.58f, offset, duration);
    }
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
    patternPlayer.flushAllPendingNoteOffs(midi, 0);
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
