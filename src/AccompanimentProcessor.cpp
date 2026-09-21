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
    resetSectionRiffMemory();
    // Onset-aligned mirror pitch. `mirrorPitchWindow` is the LATENCY (how long
    // the note waits for the onset window) AND the fixed analysis length.
    //
    // Sized from physics, not taste: an estimator needs ~2 periods of the note to
    // name it, and the lowest note the plugin supports is C2 (65.4 Hz, the drop-C
    // design floor). That is ~30.6 ms at any sample rate. Measured on real DIs at
    // 170 BPM, pitch-class match vs window: 16 ms 55%, 24 ms 70%, 28-32 ms ~76%,
    // 40 ms 81% — so this is the knee, and everything below ~24 ms is unusable.
    //
    // A frequency-domain harmonic comb (whitened matched filter / linear HPS /
    // YIN+comb correction) was implemented and measured as a short-window
    // alternative: none of them beats YIN at equal window length, because a
    // 16 ms window cannot resolve harmonics 73 Hz apart. The window IS the
    // latency knob. See docs/BASS_MIRRORING.md §16 and §20.
    //
    // The delay is only audible while a riff is being learned — Record capture is
    // silent and frozen/section replay is grid-placed (0.9 ms), so it costs
    // nothing once a riff is stored.
    static constexpr double kLowestSupportedNoteHz = 65.4;   // C2, drop-C floor
    mirrorPitchWindow = juce::jlimit(256, kMaxOnsetWindow,
        static_cast<int>(std::lround(2.0 / kLowestSupportedNoteHz * sr)));
    clearPendingMirror();
    lastOnsetMirrorMidi = -1;
    mirrorHeldPc = -1;
    recentAttackCount = 0;
    recentAttackWrite = 0;

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
    lastLearnerHopAbs = -1;
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

int64_t AccompanimentProcessor::getRiffAPlayOriginSample() const noexcept
{
    return riffAPlayOriginMono;
}

int64_t AccompanimentProcessor::getRiffBPlayOriginSample() const noexcept
{
    return riffBPlayOriginMono;
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
    lastOnsetDensity_ = fv.onsetDensityPerBeat;
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

    // Fill-grammar context (Phase 37 A1): the same signals the selector uses.
    // displayStyle is atomic and written on the inference thread; reading it here
    // and storing the copy on the audio thread keeps this race-free.
    patternPlayer.setFillEnergy(rms);
    patternPlayer.setFillDensity(fv.onsetDensityPerBeat);
    patternPlayer.setFillStyle(displayStyle.load(std::memory_order_relaxed));

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
        // Step 2: a fresh Play run learns the form again from the top.
        resetSectionRiffMemory();
        clearPendingMirror();
        lastOnsetMirrorMidi = -1;
        mirrorHeldPc = -1;
        recentAttackCount = 0;
        recentAttackWrite = 0;
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

                // Step 2: store the section we just left, then either replay a
                // stored riff for this section name or start learning it.
                const double spbSec =
                    (60.0 / juce::jmax(1.0, static_cast<double>(bpmForPlayer))) * sr;
                beginPlaySectionTake(secName, clockSample, spbSec);
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
                // Guitarist intensity steers drum density: when they are riffing
                // in 16ths, restrict the pool to the dense (metal) members so
                // Play does not answer a fast riff with a sparse backbeat. Falls
                // back to the full pool when nothing dense is available.
                PatternRules::SectionPatternPool pickPool = orderedPool;
                const float playDensity = phraseLearner.getOnsetDensityPerBeat(
                    hostSampleTime, rhythmWindowSamples, samplesPerBeatLocal);
                {
                    if (playDensity >= 2.5f)
                    {
                        // Answer fast riffing with a driving groove — but keep the
                        // genre's vocabulary. The old fixed `perBar >= 12` cut a
                        // pool down to whatever was densest on paper, which for
                        // Thrash (Thrash 10/bar, D-Beat 11/bar vs Verse Fast
                        // 16/bar) collapsed the pool to a single generic pattern.
                        // Rank by notated density, keep the members close to the
                        // densest, and never collapse a multi-member pool below
                        // two members (the phrase rotation still needs a choice).
                        int   ord[8]  = {};
                        float dens[8] = {};
                        int   n = 0;
                        for (int i = 0; i < orderedPool.count && n < 8; ++i)
                        {
                            const int pi = orderedPool.indices[i];
                            if (pi < 0 || pi >= patternLibrary.patternCount())
                                continue;
                            const auto& pat = patternLibrary.getPattern(pi);
                            ord[n] = pi;
                            dens[n] = static_cast<float>(pat.drumEvents.size())
                                    / juce::jmax(1.0f, pat.lengthInBars);
                            ++n;
                        }
                        for (int i = 1; i < n; ++i)   // insertion sort: densest first
                        {
                            const int pi = ord[i];
                            const float d = dens[i];
                            int j = i - 1;
                            while (j >= 0 && dens[j] < d)
                            {
                                ord[j + 1] = ord[j];
                                dens[j + 1] = dens[j];
                                --j;
                            }
                            ord[j + 1] = pi;
                            dens[j + 1] = d;
                        }
                        const float threshold = (n > 0)
                            ? juce::jmax(6.0f, 0.65f * dens[0]) : 6.0f;
                        PatternRules::SectionPatternPool densePool{};
                        densePool.count = 0;
                        for (int i = 0; i < n && densePool.count < 8; ++i)
                            if (dens[i] >= threshold)
                                densePool.indices[densePool.count++] = ord[i];
                        for (int i = 0; i < n && densePool.count < 2; ++i)
                        {
                            bool have = false;
                            for (int k = 0; k < densePool.count; ++k)
                                if (densePool.indices[k] == ord[i]) have = true;
                            if (!have)
                                densePool.indices[densePool.count++] = ord[i];
                        }
                        if (densePool.count > 0)
                            pickPool = densePool;
                    }
                }
                const unsigned seed = static_cast<unsigned>(
                    sectionEntryBar.load(std::memory_order_relaxed));
                int picked = PatternRules::pickPoolPattern(
                    pickPool, seed, grooveSlot, lastPlayedPoolPattern);
                // B2/ML: let the mel-CNN steer WITHIN the section pool. The
                // model's vocabulary is the metal-era set (0-21) while the rock
                // pools are 22-27, so a raw ML pick is almost never a pool member
                // and Play silently ignored the classifier. Re-home the pick with
                // the same genre rules the follow path uses, then prefer it when it
                // lands in the pool (never repeating the previous phrase; the pool
                // stays authoritative, so Play rotation is still bar-quantised).
                FeatureVector mlF{};
                mlF.state = st;
                mlF.bpm = bpmForPlayer;
                mlF.rmsEnergy = rms;
                mlF.spectralCentroid = centroid;
                mlF.onsetDensityPerBeat = playDensity;
                const int barMod8 = (samplesPerBeatLocal > 0.0)
                    ? static_cast<int>(static_cast<double>(hostSampleTime)
                                       / (samplesPerBeatLocal * 4.0)) % 8
                    : 0;
                const int mlPick = PatternRules::diversifyPatternForGenre(
                    patternIdx, mlF, barMod8, genreId);
                if (PatternRules::poolContains(pickPool, mlPick)
                    && mlPick != lastPlayedPoolPattern
                    && mlPick > 0)
                    picked = mlPick;
                else if (PatternRules::poolContains(pickPool, patternIdx)
                    && patternIdx != lastPlayedPoolPattern
                    && patternIdx > 0)
                    picked = patternIdx;
                if (picked < 0)
                    picked = PatternRules::constrainToPool(patternIdx, pickPool, st);
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
    // Step 2: Play ended (stop, or the form completed) mid-take → store what
    // the section captured so a later return to that name can replay it.
    if (!playOn && playTakeActive)
        storePlaySectionTake();
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

    // 39-02: fill-cue detection. Latch a per-bar cue from the energy trend and the
    // pick density: a swell while picking densely makes the next fill bigger (+1),
    // a drop-out makes it smaller (-1). Bar-latched so the fill tier is stable for
    // the whole fill (and so 128/512/2048 renders stay identical).
    if (playOn)
    {
        const auto globalBar = structureSequencer.getGlobalBarCount();
        if (globalBar != lastCueBar_)
        {
            const float rise = guitarEnergyRms_ - cueEnergyPrev_;
            cueEnergyPrev_ = guitarEnergyRms_;
            fillCueLatch_ = 0;
            if (rise > 0.02f && lastOnsetDensity_ >= 1.8f) fillCueLatch_ = 1;
            else if (rise < -0.03f)                        fillCueLatch_ = -1;
            lastCueBar_ = globalBar;
        }
    }
    else
    {
        fillCueLatch_ = 0;
        lastCueBar_ = -1;
    }
    patternPlayer.setFillCue(fillCueLatch_);

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

        // Pitch used for a captured 16th. The onset-aligned estimate (resolved
        // from the window that starts at the pick) is best; the block YIN and
        // the stable tracker lag one note behind on pitch changes.
        auto capturePitchMidi = [&]() noexcept -> int
        {
            if (lastOnsetMirrorMidi >= 0)
                return lastOnsetMirrorMidi;
            const float instPitch = pitchEstimator.getMidiNote();
            const float instConf  = pitchEstimator.getConfidence();
            if (instConf > 0.3f)
                return 36 + ((static_cast<int>(std::round(instPitch)) % 12) + 12) % 12;
            return (pcForBass != INT_MIN) ? 36 + pcForBass : 36;
        };

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
            clearTransitionMemory();
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
            // Measure the hold from the bar-aligned riff origin, not from whichever
            // block boundary detected the capture, so `lockBars` of music is the
            // same length at every host buffer size (T9.2).
            grooveLockStartMono = riffAPlayOriginMono;
            grooveLockEndMono = riffAPlayOriginMono + lockDuration;
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
            clearPendingMirror();
            lastOnsetMirrorMidi = -1;
            mirrorHeldPc = -1;
            recentAttackCount = 0;
            recentAttackWrite = 0;
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
                    const int bassMidi = capturePitchMidi();
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

        if ((enginePhase == EnginePhase::RiffBListen || enginePhase == EnginePhase::RiffBLocked)
            && phraseLearner.isGridListening()
            && samplesPerBeat > 0.0 && transitionStartMono >= 0)
        {
            // Capture runs for the WHOLE contrast, including after the learner
            // auto-locks it into RiffBLocked. Before this the stamp stopped (and
            // the grid was cleared) the moment the lock fired, so an 8-bar
            // contrast stored only the first bar or two: the replay was a
            // 4-note sketch and the return sounded like no bass at all.
            const int bassMidi = capturePitchMidi();
            const double elapsedMonoBeats =
                static_cast<double>(hostSampleTime - transitionStartMono) / samplesPerBeat;
            const double originBeat = beatStart - elapsedMonoBeats;
            stampLearnerGridSlots(in, numSamples, beatStart, beatEnd, samplesPerBeat,
                                  originBeat, bassMidi, true);
        }

        // Step 2: capture the first 4 bars of a Play section (bar-aligned to the
        // transport) into the 64-slot snapshot, in parallel with the live mirror.
        // Beats are shifted into the section's own frame so slot 0 is bar 1 beat 1.
        if (playOn && playTakeActive && phraseLearner.isGridListening()
            && samplesPerBeat > 0.0)
        {
            const double relBlockStart = beatStart - playTakeOriginBeat;
            const double relBlockEnd = juce::jmin(
                beatEnd - playTakeOriginBeat,
                static_cast<double>(PhraseLearner::kGridBars) * 4.0);
            if (relBlockStart < static_cast<double>(PhraseLearner::kGridBars) * 4.0
                && relBlockEnd > relBlockStart)
            {
                const int bassMidi = capturePitchMidi();
                stampLearnerGridSlots(in, numSamples, relBlockStart, relBlockEnd,
                                      samplesPerBeat, 0.0, bassMidi, false);
            }
        }

        // Do not wipe a user take on phrase-breath silence.
        // The learner is fed the FAST onset RMS, not the 0.1 s structure RMS:
        // note attacks live at 10-100 ms, and a 100 ms window cannot show the
        // trough between 16ths (the attack detector then re-fired several times
        // per note, so the mirror machine-gunned). Structure/loudness keeps the
        // slow RMS.
        // The attack detector must see the waveform at a fixed hop, not once per
        // host block. With a large host buffer the old once-per-block call fed it
        // a single RMS value taken from the last ~20 ms of the block, so mirror
        // density collapsed as the buffer grew (370 -> 78 notes/s from 64 -> 4096
        // samples; the buffer sweep in test_bass_mirror_play_realaudio). Drive the
        // learner once per fixed onset hop instead, so it behaves the same at 64
        // and 65536 samples per block.
        // Modes where the live mirror owns the bass (Play section / Riff-B
        // listen / locked contrast). The legato follow only applies there.
        const bool mirrorListening = (enginePhase == EnginePhase::PlaySection
                                   || enginePhase == EnginePhase::RiffBListen
                                   || enginePhase == EnginePhase::RiffBLocked);

        // Snap a monotonic hop time onto the transport's 16th grid.
        //
        // Both the attack mirror AND the legato re-tune use this, so the whole
        // live bass sits on the SAME grid the learned/frozen replay uses. That is
        // the point: the frozen replay is 16th-quantised by construction, so if
        // the live first pass is not, the bass visibly jumps when the riff locks.
        //
        // Measured on a real DI (1323, 170 BPM): the old "±15 ms" bound left the
        // live mirror scattered — phase std 27.3 ms, only 21% of notes on the
        // grid. With this it is 96% on the grid and std 1.9 ms, with no measurable
        // pitch cost. Scatter, not the constant window offset, is what reads as
        // "bad timing".
        //
        // kSnapFractionOfSixteenth = 0.40 is a pure "nearest 16th" snap (tempo-
        // relative, so it behaves identically at every BPM). Lower it for a softer
        // snap; 0.17 reproduces the old ~±15 ms bound. Below ~0.25 it can still
        // resolve 32nds — at 0.50 two 32nds collapse to the same 16th, which
        // matches what the learned capture/replay already does.
        //
        // The delta is measured on the TRANSPORT grid and applied to the
        // MONOTONIC hop — the two clocks are never merged.
        auto snapDeltaFor = [&](int hopOff) noexcept -> int64_t
        {
            if (!(samplesPerBeat > 1.0))
                return 0;
            static constexpr double kSnapFractionOfSixteenth = 0.50;
            const double sixteenthQ = samplesPerBeat / 4.0;
            const double tol = kSnapFractionOfSixteenth * sixteenthQ;
            const int64_t attackTransport = clockSample + hopOff;
            const int64_t nearest = static_cast<int64_t>(
                std::llround(static_cast<double>(attackTransport) / sixteenthQ) * sixteenthQ);
            const int64_t d = nearest - attackTransport;
            return (std::abs(static_cast<double>(d)) <= tol) ? d : int64_t{ 0 };
        };
        if (armActive || capturingNow)
        {
            const float blockPitchMidi = pitchEstimator.getMidiNote();
            const float blockPitchConf = pitchEstimator.getConfidence();
            const int hopCount = energyAnalyser.getOnsetHopCount();
            // Both are driven at the same fixed hop, so index h aligns. Fall back
            // to the block estimate if they ever diverge (older build mismatch).
            const bool hopPitchAligned = (pitchEstimator.getHopCount() == hopCount);
            for (int h = 0; h < hopCount; ++h)
            {
                const int hopOffset = energyAnalyser.getOnsetHopOffset(h);
                const int64_t hopAbs = hostSampleTime + hopOffset;
                const float pitchMidi = hopPitchAligned
                    ? pitchEstimator.getHopMidi(h) : blockPitchMidi;
                const float pitchConf = hopPitchAligned
                    ? pitchEstimator.getHopConf(h) : blockPitchConf;
                // Per-16th capture pitch vote (see flushPendingCaptureSlot).
                voteCaptureSlotPitch(pitchMidi, pitchConf);
                // Hops are at fixed global positions (the analyser's countdown
                // carries across blocks), so the delta is the true time since the
                // previous call — no per-block "tail" sample, which is what still
                // made detection depend on the host block size.
                const int delta = (lastLearnerHopAbs >= 0)
                    ? juce::jmax(1, static_cast<int>(hopAbs - lastLearnerHopAbs))
                    : hopOffset + 1;
                lastLearnerHopAbs = hopAbs;
                const auto bn = phraseLearner.process(
                    hopAbs, energyAnalyser.getOnsetHopRms(h),
                    pitchMidi, pitchConf, bpmForPlayer, delta, pcForBass,
                    energyAnalyser.getOnsetHopFlux(h));
                // Queue every detected attack for onset-aligned pitch resolution
                // (used by the live mirror AND the riff capture). Emission is
                // decided later, once the window is available.
                // Record every real pick for the riff-capture articulation, and
                // queue it for onset-pitch resolution — even during user capture,
                // where the learner suppresses bn.trigger.
                if (bn.trigger || phraseLearner.wasAttackDetected())
                {
                    enqueueMirrorTrigger(hopAbs + snapDeltaFor(hopOffset), bn.velocity, bn.midiNote);
                    lastMirrorAttackHopAbs = hopAbs;
                    recordRecentAttack(hopAbs);
                    legatoPcPending = INT_MIN;
                    legatoHops = 0;
                }
                else if (mirrorListening && mirrorHeldPc >= 0 && pitchConf > 0.25f
                         && (lastMirrorAttackHopAbs == std::numeric_limits<int64_t>::min()
                             || hopAbs - lastMirrorAttackHopAbs
                                    > static_cast<int64_t>(mirrorPitchWindow)))
                {
                    // Legato pitch follow: a held/legato change
                    // with no fresh pick retunes the held note once the fixed-hop
                    // pitch estimate agrees for a few hops (~30 ms). Hop-counted,
                    // not block-counted, so it is buffer-size invariant.
                    //
                    // `pendingMirrorCount == 0` is essential: a detected pick is
                    // queued with its onset-resolved pitch, but `mirrorHeldPc` is
                    // only updated when that entry is FLUSHED (one onset window
                    // later, ~40 ms). Without this guard the hops between the pick
                    // and the flush compare the new note against the stale held
                    // note, accumulate 3 hops and fire the same note again 32 ms
                    // after the pick — a same-pitch double-trigger on every pitch
                    // change ("sounds like someone playing badly"). Measured on a
                    // real DI: 61 same-pitch note-ons within 48 ms vs 6 before the
                    // confidence fix let this path run at all.
                    const int hopPc = ((static_cast<int>(std::lround(pitchMidi)) % 12) + 12) % 12;
                    if (hopPc != mirrorHeldPc)
                    {
                        if (hopPc == legatoPcPending)
                            ++legatoHops;
                        else
                        {
                            legatoPcPending = hopPc;
                            legatoHops = 1;
                        }
                        if (legatoHops >= 3)
                        {
                            enqueueMirrorTrigger(hopAbs + snapDeltaFor(hopOffset), 0.58f, 36 + hopPc);
                            mirrorHeldPc = hopPc;
                            legatoPcPending = INT_MIN;
                            legatoHops = 0;
                        }
                    }
                    else
                    {
                        legatoPcPending = INT_MIN;
                        legatoHops = 0;
                    }
                }
                else
                {
                    legatoPcPending = INT_MIN;
                    legatoHops = 0;
                }
            }
        }
        else
        {
            // Gated/idle: drop the accumulator anchor so a later resume does not
            // pass one enormous delta to the learner.
            lastLearnerHopAbs = -1;
        }
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
                // Do NOT cancel the grid listen here: the contrast memory is
                // written from the FULL contrast at transitionEndMono, and
                // cancelling mid-way is what truncated it to a few slots. The
                // lock only pins the contrast DRUMS.
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
        // The post-lock transition keeps the bass on the live mirror even after
        // the learner locks a contrast riff: the drums commit to the contrast
        // section, the bass follows the player.
        phraseLearner.setLiveMirrorWhenLocked(postLockPhase == PostLockPhase::TransitionHold);
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
            // Learn this contrast slot on its first visit, replay it afterwards
            // (docs/BASS_MIRRORING.md §17). Must run AFTER latchLockClock so the
            // replay origin is bar-phase aligned.
            beginTransitionTake(slot, samplesPerBeat);

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
            // The T6.2 same-riff cut-short was removed: it fired on a loose
            // IOI+pitch match and ended the section early even when the user had
            // selected more transition bars. The transition now always runs its
            // selected length (bounded by transitionEndMono below).
            if (transitionEndMono >= 0 && hostSampleTime >= transitionEndMono)
            {
                // Snapshot the contrast before leaving it, so the next visit to
                // this slot replays what was played here.
                storeTransitionTake();
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
        // The mirror is the bass part. While the guitarist is sounding, the bass
        // follows them — one note per detected attack, held through a sustain —
        // and the authored/harmonic grid line is muted entirely.
        //
        // The harmony line is a *total fallback*: it is heard only when the
        // guitarist has stopped (`silentNow`). Gating it on "no attack detected"
        // instead was the recurring regression: a sustained note, a legato
        // phrase or any missed detection looked like a gap, so the harmony line
        // played constantly under the player ("the bass goes into harmony and
        // stays there"). Silence, not the absence of attacks, is the gap.
        const bool listenBass = (enginePhase == EnginePhase::PlaySection
                              || enginePhase == EnginePhase::RiffBListen
                              || enginePhase == EnginePhase::RiffBLocked);
        const bool guitarAudible = !silentNow;
        patternPlayer.setGuitarAudible(guitarAudible);
        // Fast input level so a held mirror note releases when the note decays,
        // not only when the slow structure gate later notices silence.
        patternPlayer.setGuitarLevel(energyAnalyser.getOnsetRmsEnergy());
        // A replayed section riff owns the voice: do not let the harmony grid
        // layer under it when the guitarist pauses. Nor under a post-lock
        // transition: that section is a DRUM contrast, and the bass must mirror
        // the player (rest when they rest) instead of playing a root/fifth line
        // over it — that was the loud, unmusical transition bass.
        // 38-03: SOLO never mirrors the lead. When a learned VERSE/CHORUS phrase is
        // available the replay owns the voice (grid off); otherwise the in-key grid
        // plays as the fallback (grid on even while the guitarist is audible).
        const bool soloSectionNow = playOn && std::strcmp(section, "SOLO") == 0;
        patternPlayer.setBeatGridBassEnabled(
            (listenBass && !guitarAudible && !playTakeReplaying
             && postLockPhase != PostLockPhase::TransitionHold)
            || (soloSectionNow && !playTakeReplaying));

        // The fallback keeps the key the guitarist last played (the live tracker
        // reports nothing once they stop), so the harmony line is not stuck on C.
        const int rootOffset = (semitoneOffset != INT_MIN) ? semitoneOffset
                                                           : lastBassPitchClassOffset;
        int bassRoot = 36;
        if (rootOffset != INT_MIN)
        {
            bassRoot = 36 + rootOffset;
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
        const bool mirrorActive = listenBass && !guitarStopped
            && !riffCaptureActive.load(std::memory_order_acquire)
            && !phraseLearner.isGridCapturing()
            && !soloSectionNow;   // 38-03: SOLO reuses a learned phrase, not the lead
        const int accentPat = patternPlayer.getActivePatternIndex();
        if (accentPat != lastAccentPatternIdx)
        {
            lastAccentPatternIdx = accentPat;
            drumAccentMask = buildDrumAccentMask(accentPat);
        }
        if (enginePhase == EnginePhase::RiffA
            && !riffCaptureActive.load(std::memory_order_acquire))
        {
            emitFrozenRiff(riffA, riffAPlayOriginMono, numSamples,
                           static_cast<double>(bpmForPlayer), sr,
                           hostSampleTime, bassTranspose);
            clearPendingMirror();
        }
        else if (playOn && playTakeReplaying)
        {
            // Step 2: a return to a section name replays the riff captured on
            // its first pass. The replay is authoritative while it plays; the
            // live mirror is suppressed below (see the note in §5.5).
            const auto* stored = findSectionRiff(playTakeReplayName);
            if (stored != nullptr)
                emitFrozenRiff(*stored, playTakeOriginMono, numSamples,
                               static_cast<double>(bpmForPlayer), sr,
                               hostSampleTime, bassTranspose);
            flushMirrorTriggers(numSamples, hostSampleTime + numSamples, bassTranspose,
                                durationSamples, mirrorActive, stored, playTakeOriginMono,
                                samplesPerBeatQ);
        }
        else if (!playOn && transitionTakeReplaying)
        {
            // Record contrast recall (§17): replay the riff learned on this
            // contrast slot's first visit. Authoritative while it plays, so the
            // bass keeps going when the guitarist stops — the same contract as
            // Riff A. Without this the transition only mirrored the live player
            // and went silent on a rest.
            const auto* stored = findTransitionRiff(transitionTakeSlot);
            if (stored != nullptr)
                emitFrozenRiff(*stored, transitionReplayOriginMono, numSamples,
                               static_cast<double>(bpmForPlayer), sr,
                               hostSampleTime, bassTranspose);
            flushMirrorTriggers(numSamples, hostSampleTime + numSamples, bassTranspose,
                                durationSamples, mirrorActive, stored,
                                transitionReplayOriginMono, samplesPerBeatQ);
        }
        else
        {
            // Resolve onset pitch for any queued attacks; emit only when the live
            // mirror owns the bass (early-return branches above clear the queue).
            flushMirrorTriggers(numSamples, hostSampleTime + numSamples,
                                bassTranspose, durationSamples, mirrorActive);
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
    // Snap to the bar line, stored signed, in [-2, 2) beats.
    //
    // Plain `fmod` always snaps backwards, so a detect block that starts a few
    // samples *before* a bar line shifted the riff origin back a whole bar: the
    // locked riff entered on its bar 2 rather than bar 1, and the phase depended on
    // the host block size (measured: 128 -> 0-slot offset, 512/2048 -> 16-slot /
    // one-bar offset).
    //
    // Snapping forward is only safe when the block merely STRADDLES the line: a
    // genuine mid-bar engage pushed forward would leave the origin in the future and
    // silence the bass until it arrived. A block can never start more than one block
    // before the line (~0.26 beats at 40 BPM / 2048 samples), so a half-beat window
    // separates the two cases cleanly.
    constexpr double kMaxForwardSnapBeats = 0.5;
    const double phaseBack = std::fmod(static_cast<double>(transportSample) / samplesPerBeat, 4.0);
    const double phaseBackWrapped = (phaseBack < 0.0) ? phaseBack + 4.0 : phaseBack;
    const double forwardBeats = 4.0 - phaseBackWrapped;
    lockBarPhaseBeats = (forwardBeats <= kMaxForwardSnapBeats) ? -forwardBeats : phaseBackWrapped;
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
    // A jump that lands on the SAME bar phase — a bar-aligned DAW loop wrap, the
    // normal case — must not re-phase the riff. The mono clock already keeps the
    // loop continuous; re-latching would move the origin to whichever block
    // detected the wrap, nudging the riff by up to a 16th on every pass (measured:
    // one onset per loop dropped, plus a mid-loop phase step). Only a genuinely
    // off-grid seek needs the bar phase re-latched.
    const auto signedBarPhase = [samplesPerBeat](int64_t sample) noexcept -> double
    {
        if (samplesPerBeat <= 0.0)
            return 0.0;
        double p = std::fmod(static_cast<double>(sample) / samplesPerBeat, 4.0);
        if (p >= 2.0)
            p -= 4.0;
        else if (p < -2.0)
            p += 4.0;
        return p;
    };

    double err = std::fabs(signedBarPhase(transportSample) - signedBarPhase(lastClockSample));
    if (err > 2.0)
        err = 4.0 - err;
    if (err < 0.05)
        return;   // bar-aligned wrap: keep the continuous mono phase

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
    captureVoteCounts_.fill(0);
}

void AccompanimentProcessor::voteCaptureSlotPitch(float midi, float conf) noexcept
{
    if (captureSlotIndex_ < 0 || conf < kCaptureVoteConf)
        return;
    const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
    auto& c = captureVoteCounts_[static_cast<std::size_t>(pc)];
    if (c < 0xffff)
        ++c;
}

void AccompanimentProcessor::flushPendingCaptureSlot() noexcept
{
    if (captureSlotIndex_ < 0)
        return;
    const bool occupied = captureSlotPeak_ >= 0.025f;
    if (occupied)
    {
        // Majority vote over the confident fixed-hop estimates inside the slot.
        // Beats "the last attack's pitch": correct for a held note and free of
        // a stale attack inherited from the previous slot. Falls back to
        // captureSlotMidi_ (the real-time value) when the slot had no confident
        // hop estimate at all.
        int winner = -1;
        std::uint16_t bestVotes = 0;
        for (int pc = 0; pc < 12; ++pc)
        {
            if (captureVoteCounts_[static_cast<std::size_t>(pc)] > bestVotes)
            {
                bestVotes = captureVoteCounts_[static_cast<std::size_t>(pc)];
                winner = pc;
            }
        }
        if (winner >= 0)
            captureSlotMidi_ = 36 + winner;


        const bool onset = !prevSlotOccupied_
            || captureSlotPeak_ > prevSlotPeak_ * 1.15f + 0.01f
            || captureSlotPeak_ > prevSlotEnd_ * 1.15f + 0.01f
            || attackInCaptureSlot(captureSlotStartAbs, captureSlotStartAbs + captureSlotSamples);
        // Capture the slot's attack level as a velocity. The learned riff was
        // replayed at one fixed velocity, which is what made a locked bass sound
        // robotic; this keeps the dynamics the guitarist actually played.
        const uint8_t slotVel = static_cast<uint8_t>(std::lround(
            127.0 * juce::jlimit(0.46f, 0.88f, 0.46f + captureSlotPeak_ * 1.9f)));
        phraseLearner.stampGridRange(
            static_cast<double>(captureSlotIndex_) * 0.25,
            static_cast<double>(captureSlotIndex_) * 0.25 + 0.25,
            captureSlotPeak_, captureSlotMidi_, onset, slotVel);
        prevSlotPeak_ = captureSlotPeak_;
        // The true end-of-slot envelope. A held note ends near its peak (no onset);
        // a re-picked 16th decays within the slot and jumps back up (onset).
        prevSlotEnd_ = captureSlotEnd_;
        prevSlotOccupied_ = true;
    }
    else
    {
        prevSlotPeak_ = captureSlotPeak_;
        prevSlotEnd_ = 0.0f;
        prevSlotOccupied_ = false;
    }
    captureVoteCounts_.fill(0);
    captureSlotIndex_ = -1;
}

const PhraseLearner::LearnedRiff* AccompanimentProcessor::findSectionRiff(
    const char* name) const noexcept
{
    if (name == nullptr || name[0] == '\0')
        return nullptr;
    for (int i = 0; i < kSectionRiffSlots; ++i)
    {
        const auto& mem = sectionRiffs[static_cast<size_t>(i)];
        if (mem.valid && std::strcmp(mem.name, name) == 0)
            return &mem.riff;
    }
    return nullptr;
}

void AccompanimentProcessor::resetSectionRiffMemory() noexcept
{
    sectionRiffs.fill({});
    sectionRiffWrite = 0;
    playTakeActive = false;
    playTakeReplaying = false;
    playTakeSectionName[0] = '\0';
    playTakeReplayName[0] = '\0';
    playTakeOriginMono = -1;
    playTakeOriginBeat = 0.0;
    clearTransitionMemory();
}

void AccompanimentProcessor::clearTransitionMemory() noexcept
{
    transitionRiffs.fill({});
    transitionTakeActive = false;
    transitionTakeReplaying = false;
    transitionTakeSlot = -1;
    transitionReplayOriginMono = -1;
}

const PhraseLearner::LearnedRiff* AccompanimentProcessor::findTransitionRiff(int slot) const noexcept
{
    if (slot < 0 || slot >= kMaxTransitionSlots)
        return nullptr;
    const auto& mem = transitionRiffs[static_cast<std::size_t>(slot)];
    return (mem.valid && mem.riff.valid) ? &mem.riff : nullptr;
}

int AccompanimentProcessor::getTransitionRiffOccupiedCount(int slot) const noexcept
{
    const auto* riff = findTransitionRiff(slot);
    if (riff == nullptr)
        return 0;
    int n = 0;
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
        if (riff->occupied[static_cast<std::size_t>(s)])
            ++n;
    return n;
}

void AccompanimentProcessor::storeTransitionRiff(int slot,
                                                const PhraseLearner::LearnedRiff& riff) noexcept
{
    if (slot < 0 || slot >= kMaxTransitionSlots || !riff.valid)
        return;
    auto& mem = transitionRiffs[static_cast<std::size_t>(slot)];
    mem.valid = true;
    std::strncpy(mem.name, transitionSectionNameStr != nullptr ? transitionSectionNameStr : "?",
                 sizeof(mem.name) - 1);
    mem.name[sizeof(mem.name) - 1] = '\0';
    mem.riff = riff;
    mem.learnedAtMono = hostSampleTime;
}

void AccompanimentProcessor::storeTransitionTake() noexcept
{
    if (!transitionTakeActive)
    {
        transitionTakeReplaying = false;
        transitionReplayOriginMono = -1;
        return;
    }

    if (phraseLearner.isGridListening())
        flushPendingCaptureSlot();

    PhraseLearner::LearnedRiff snap{};
    phraseLearner.exportPattern(snap);
    const int occupied = phraseLearner.getGridOccupiedCount();

    // A contrast with fewer than two occupied 16ths is not a riff; keep the
    // previous memory (if any) and fall back to the live mirror next visit.
    if (occupied >= 2 && snap.valid)
        storeTransitionRiff(transitionTakeSlot, snap);

    phraseLearner.endSectionGridListen();
    transitionTakeActive = false;
    transitionTakeReplaying = false;
    transitionTakeSlot = -1;
    transitionReplayOriginMono = -1;
}

void AccompanimentProcessor::beginTransitionTake(int slot, double samplesPerBeat) noexcept
{
    // Store whatever the contrast we are leaving learned, then either replay this
    // slot's stored riff or start learning it.
    storeTransitionTake();

    transitionTakeSlot = slot;
    transitionReplayOriginMono = frozenRiffOriginMono(samplesPerBeat);

    const PhraseLearner::LearnedRiff* stored = findTransitionRiff(slot);
    if (stored != nullptr)
    {
        transitionTakeActive = false;
        transitionTakeReplaying = true;
        phraseLearner.endSectionGridListen();
    }
    else
    {
        transitionTakeActive = true;
        transitionTakeReplaying = false;
        // Passive capture: stamps the 16th grid in parallel with the live
        // mirror, and unlike beginGridCapture it does NOT suppress the mirror.
        phraseLearner.beginSectionGridListen();
    }
}

void AccompanimentProcessor::beginPlaySectionTake(const char* name, int64_t clockSample,
                                                  double samplesPerBeat) noexcept
{
    // Store what the section we are leaving learned, then start the new one.
    storePlaySectionTake();

    if (name == nullptr || name[0] == '\0')
        return;

    std::strncpy(playTakeSectionName, name, sizeof(playTakeSectionName) - 1);
    playTakeSectionName[sizeof(playTakeSectionName) - 1] = '\0';

    // Bar-aligned monotonic origin for the replay loop. Same forward-snap rule
    // as latchLockClock (a detecting block may straddle the bar line), but kept
    // in dedicated fields so the A/B lock clock is not disturbed.
    playTakeOriginMono = hostSampleTime;
    playTakeOriginBeat = 0.0;
    if (samplesPerBeat > 0.0)
    {
        const double beatAtEntry = static_cast<double>(clockSample) / samplesPerBeat;
        double barPhase = std::fmod(beatAtEntry, 4.0);
        if (barPhase < 0.0)
            barPhase += 4.0;
        const double forward = 4.0 - barPhase;
        const double phaseBeats = (forward <= 0.5) ? -forward : barPhase;
        playTakeOriginMono = hostSampleTime
            - static_cast<int64_t>(std::llround(phaseBeats * samplesPerBeat));
        playTakeOriginBeat = beatAtEntry - phaseBeats;
    }

    phraseLearner.setAutoLockEnabled(false);   // Play never auto-locks the learner
    resetSlotOnsetTracker();

    // 38-03: SOLO reuses a learned VERSE/CHORUS phrase instead of mirroring the
    // lead line (a bass echoing a solo is not what a band does). Prefer VERSE,
    // else CHORUS; when neither exists the SOLO falls back to the in-key
    // harmonic/authored grid (the mirror stays suppressed in SOLO either way).
    if (std::strcmp(name, "SOLO") == 0)
    {
        if (findSectionRiff("VERSE") != nullptr)       std::strncpy(playTakeReplayName, "VERSE", sizeof(playTakeReplayName) - 1);
        else if (findSectionRiff("CHORUS") != nullptr) std::strncpy(playTakeReplayName, "CHORUS", sizeof(playTakeReplayName) - 1);
        else                                            playTakeReplayName[0] = '\0';
        playTakeReplayName[sizeof(playTakeReplayName) - 1] = '\0';
    }
    else
    {
        std::strncpy(playTakeReplayName, playTakeSectionName, sizeof(playTakeReplayName) - 1);
        playTakeReplayName[sizeof(playTakeReplayName) - 1] = '\0';
    }

    const PhraseLearner::LearnedRiff* stored = findSectionRiff(playTakeReplayName);
    // Every visit keeps capturing in parallel: the replay stays authoritative
    // (the live mirror is suppressed below), but the section memory is replaced
    // with this pass when the section ends. Before this a section was frozen
    // forever on its FIRST pass — a warm-up first bar, a late start or a bad
    // first take was stuck in the memory for the whole session.
    playTakeActive = true;
    playTakeReplaying = (stored != nullptr && stored->valid);
    phraseLearner.beginSectionGridListen();
}

void AccompanimentProcessor::storePlaySectionTake() noexcept
{
    if (!playTakeActive)
    {
        playTakeReplaying = false;
        playTakeOriginMono = -1;
        return;
    }

    if (phraseLearner.isGridListening())
        flushPendingCaptureSlot();

    PhraseLearner::LearnedRiff snap{};
    phraseLearner.exportPattern(snap);
    const int occupied = phraseLearner.getGridOccupiedCount();

    // A section with fewer than two occupied 16ths is not a riff; do not create
    // a memory entry (the next visit then falls back to live mirroring).
    if (occupied >= 2 && snap.valid && playTakeSectionName[0] != '\0')
    {
        int slot = -1;
        for (int i = 0; i < kSectionRiffSlots; ++i)
        {
            const auto& mem = sectionRiffs[static_cast<size_t>(i)];
            if (mem.valid && std::strcmp(mem.name, playTakeSectionName) == 0)
            {
                slot = i;
                break;
            }
        }
        if (slot < 0)
        {
            slot = sectionRiffWrite;
            sectionRiffWrite = (sectionRiffWrite + 1) % kSectionRiffSlots;
        }
        auto& mem = sectionRiffs[static_cast<size_t>(slot)];
        mem.valid = true;
        std::strncpy(mem.name, playTakeSectionName, sizeof(mem.name) - 1);
        mem.name[sizeof(mem.name) - 1] = '\0';
        mem.riff = snap;
        mem.learnedAtMono = hostSampleTime;
    }

    phraseLearner.endSectionGridListen();
    playTakeActive = false;
    playTakeReplaying = false;
    playTakeSectionName[0] = '\0';
    playTakeOriginMono = -1;
    playTakeOriginBeat = 0.0;
    resetSlotOnsetTracker();
}

int AccompanimentProcessor::getStoredSectionRiffOccupiedCount(const char* name) const noexcept
{
    const auto* riff = findSectionRiff(name);
    if (riff == nullptr)
        return 0;
    int n = 0;
    for (int s = 0; s < PhraseLearner::kGridSlots; ++s)
        if (riff->occupied[static_cast<size_t>(s)])
            ++n;
    return n;
}

bool AccompanimentProcessor::getStoredSectionSlotOccupied(const char* name, int slot) const noexcept
{
    if (slot < 0 || slot >= PhraseLearner::kGridSlots)
        return false;
    const auto* riff = findSectionRiff(name);
    return riff != nullptr && riff->occupied[static_cast<size_t>(slot)];
}

int AccompanimentProcessor::getStoredSectionSlotMidi(const char* name, int slot) const noexcept
{
    if (slot < 0 || slot >= PhraseLearner::kGridSlots)
        return -1;
    const auto* riff = findSectionRiff(name);
    if (riff == nullptr || !riff->occupied[static_cast<size_t>(slot)])
        return -1;
    return riff->midi[static_cast<size_t>(slot)];
}

void AccompanimentProcessor::enqueueMirrorTrigger(int64_t targetAbs, float velocity,
                                                 int fallbackNote) noexcept
{
    if (pendingMirrorCount >= kMaxPendingMirror)
        return;   // overflow: drop. 64 covers ~7 s of 16ths at 150 BPM.
    pendingMirror[pendingMirrorCount++] = { targetAbs, velocity, fallbackNote };
}

void AccompanimentProcessor::recordRecentAttack(int64_t abs) noexcept
{
    recentAttackAbs[static_cast<size_t>(recentAttackWrite)] = abs;
    recentAttackWrite = (recentAttackWrite + 1) % kMaxRecentAttacks;
    if (recentAttackCount < kMaxRecentAttacks)
        ++recentAttackCount;
}

bool AccompanimentProcessor::attackInCaptureSlot(int64_t fromAbs, int64_t toAbs) const noexcept
{
    for (int i = 0; i < recentAttackCount; ++i)
    {
        const int idx = (recentAttackWrite - 1 - i + kMaxRecentAttacks) % kMaxRecentAttacks;
        const int64_t a = recentAttackAbs[static_cast<size_t>(idx)];
        if (a >= fromAbs && a < toAbs)
            return true;
        if (a < fromAbs)
            break;   // ring is chronological newest-first
    }
    return false;
}

void AccompanimentProcessor::flushMirrorTriggers(int numSamples, int64_t blockEndAbs,
                                                 int bassTranspose, int durationSamples,
                                                 bool emit,
                                                 const PhraseLearner::LearnedRiff* skipOccupied,
                                                 int64_t skipOrigin,
                                                 double skipSamplesPerBeat) noexcept
{
    const int64_t blockStartAbs = blockEndAbs - numSamples;
    for (int i = 0; i < pendingMirrorCount; )
    {
        const auto& p = pendingMirror[i];
        const int64_t emitAbs = p.targetAbs + mirrorPitchWindow;
        if (emitAbs > blockEndAbs)
            break;   // onset window not yet complete; FIFO keeps the rest ordered

        // Onset-aligned pitch: the window that STARTS at the pick. Fall back to
        // the learner's note when the estimate is unavailable/low-confidence.
        int note = p.fallbackNote;
        float midi = 0.0f, conf = 0.0f;
        // Fixed analysis length, NOT "whatever the block happens to offer".
        // The wait guarantees `mirrorPitchWindow` samples after the pick are in
        // the ring, so request exactly that. Letting the window grow with the
        // host block made the resolved pitch (and every count derived from it —
        // the legato follow, the capture) buffer-size dependent.
        const int analysisLen = juce::jlimit(64, kMaxOnsetWindow, mirrorPitchWindow);
        if (pitchEstimator.estimateOnset(blockEndAbs, p.targetAbs, analysisLen, midi, conf)
            && conf > 0.25f)
        {
            const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
            note = 36 + pc;
        }
        // Capture pitch (no transposition): the riff recorder stamps this value.
        lastOnsetMirrorMidi = note;

        // Gap fill: while a learned riff owns the voice, the mirror is normally
        // suppressed. But the memory can be SILENT where the guitarist is playing
        // — measured on a real take: the capture window's first 14 sixteenths
        // were empty (the player had not started yet), and the return looped that
        // 1.2 s hole every 4 bars. Let the mirror fill only the 16ths the memory
        // leaves empty, so the learned part keeps its exact grid timing and the
        // player's extra notes are still heard.
        bool skipForMemory = false;
        if (emit && skipOccupied != nullptr && skipSamplesPerBeat > 0.0 && skipOrigin >= 0)
        {
            const double q = 0.25 * skipSamplesPerBeat;
            if (q > 0.0)
            {
                const int64_t idx = static_cast<int64_t>(std::llround(
                    static_cast<double>(p.targetAbs - skipOrigin) / q));
                const int slots = PhraseLearner::kGridSlots;
                const int slot = static_cast<int>(((idx % slots) + slots) % slots);
                skipForMemory = skipOccupied->occupied[static_cast<size_t>(slot)];
            }
        }
        if (!emit || skipForMemory)
        {
            for (int k = i + 1; k < pendingMirrorCount; ++k)
                pendingMirror[static_cast<size_t>(k - 1)] = pendingMirror[static_cast<size_t>(k)];
            --pendingMirrorCount;
            continue;
        }
        const int offset = static_cast<int>(juce::jlimit<int64_t>(0, numSamples - 1,
                                                                 emitAbs - blockStartAbs));
        patternPlayer.triggerLearnedBassNote(note + bassTranspose, p.velocity, offset,
                                             durationSamples, /*hold=*/true);
        lastBassPitchClassOffset = ((note % 12) + 12) % 12;
        mirrorHeldPc = lastBassPitchClassOffset;

        // Pop the front entry (the queue stays ordered by target sample).
        for (int k = i + 1; k < pendingMirrorCount; ++k)
            pendingMirror[static_cast<size_t>(k - 1)] = pendingMirror[static_cast<size_t>(k)];
        --pendingMirrorCount;
    }
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
            // The range is half-open: the sample exactly ON the end beat belongs
            // to the NEXT slot. A bare std::ceil can include it by one ULP
            // (measured: the 8th-note attack on a 16th boundary leaked into the
            // previous slot at a 2048-sample host block but not at 128/512),
            // which made the captured snapshot block-size dependent.
            const int i1 = juce::jlimit(1, numSamples,
                static_cast<int>(std::ceil((ov1 - blockBeat0) * samplesPerBeat - 1.0e-9)));
            float slotPeak = 0.0f;
            for (int i = i0; i < i1; ++i)
            {
                const float a = std::abs(in[i]);
                if (a > slotPeak)
                    slotPeak = a;
            }

            // End-of-slot envelope, measured over a FIXED fraction of the 16th
            // (its last 20%) rather than a fraction of the block. A block-relative
            // tail spans a different amount of time at each buffer size — at 128
            // samples it could land near a zero-crossing of a low note, at 2048 it
            // could not — so the onset decision, and therefore the whole locked-riff
            // articulation, changed with the host buffer size (measured: 9 / 5 / 1
            // onsets for the same 4-bar chug at 128 / 512 / 2048).
            constexpr double kTailFraction = 0.2;
            const double tailStartBeat = slotB - 0.25 * kTailFraction;
            float slotEnd = 0.0f;
            if (ov1 > tailStartBeat)
            {
                const double tb0 = juce::jmax(ov0, tailStartBeat);
                const int j0 = juce::jlimit(0, numSamples - 1,
                    static_cast<int>(std::floor((tb0 - blockBeat0) * samplesPerBeat)));
                const int j1 = juce::jlimit(1, numSamples,
                    static_cast<int>(std::ceil((ov1 - blockBeat0) * samplesPerBeat - 1.0e-9)));
                for (int i = j0; i < j1; ++i)
                {
                    const float a = std::abs(in[i]);
                    if (a > slotEnd)
                        slotEnd = a;
                }
            }
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
                captureVoteCounts_.fill(0);
                captureSlotStartAbs = hostSampleTime
                    + static_cast<int64_t>(std::llround((slotA - blockBeat0) * samplesPerBeat));
                captureSlotSamples = static_cast<int64_t>(std::llround(0.25 * samplesPerBeat));
            }
            captureSlotMidi_ = bassMidi;
            if (slotPeak > captureSlotPeak_)
                captureSlotPeak_ = slotPeak;
            if (slotEnd > captureSlotEnd_)
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

std::uint64_t AccompanimentProcessor::buildDrumAccentMask(int patternIdx) const noexcept
{
    std::uint64_t mask = 0;
    if (patternIdx < 0 || patternIdx >= patternLibrary.patternCount())
        return mask;
    const auto& pat = patternLibrary.getPattern(patternIdx);
    const int lenSlots = juce::jlimit(1, PhraseLearner::kGridSlots,
        static_cast<int>(std::lround(juce::jmax(1.0f, pat.lengthInBars) * 16.0f)));
    for (const auto& ev : pat.drumEvents)
    {
        if (ev.note != 36 && ev.note != 38 && ev.note != 49)   // kick, snare, crash
            continue;
        int s = static_cast<int>(std::lround(ev.beatOffset * 4.0f));
        if (s < 0)
            continue;
        for (int base = 0; base < PhraseLearner::kGridSlots; base += lenSlots)
        {
            const int idx = base + (s % lenSlots);
            if (idx >= 0 && idx < PhraseLearner::kGridSlots)
                mask |= (1ULL << idx);
        }
    }
    return mask;
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
        // Ring into the next onset instead of stopping 10 % short. A locked riff
        // of one-sixteenth notes at 0.9 x gate sounded staccato and mechanical;
        // the monophonic bass voice cuts the tail at the next note-on, so a small
        // over-hang is free legato. Bounded so a long sustain cannot ring for bars.
        constexpr double kLegatoTailBeats = 0.12;
        const int duration = juce::jlimit(1, static_cast<int>(std::lround(4.0 * spb)),
            static_cast<int>(std::lround(
                (static_cast<double>(gate16) * 0.25 + kLegatoTailBeats) * spb)));
        const int64_t slotOffset = static_cast<int64_t>(
            std::llround(static_cast<double>(s) * 0.25 * spb));
        // Loop instance that CONTAINS clockSample. The previous form took the
        // smallest k with absSample >= clockSample, which jumped a whole loop
        // whenever the (bar-aligned) origin sat even a few samples before the
        // block start: a Record contrast replay whose origin was 256 samples in
        // the past emitted nothing for an entire 4-bar loop. Clamping to the
        // containing loop keeps the first loop's not-yet-passed slots alive; a
        // genuine future origin still starts on k=0 and waits for its time.
        const int64_t rel = clockSample - (origin + slotOffset);
        int64_t k = rel / loopSamples;
        if (k * loopSamples < rel)
            ++k;   // mathematical ceil (works for negative rel too)
        const int64_t absSample = origin + slotOffset + k * loopSamples;
        if (absSample < clockSample || absSample >= blockEnd)
            continue;
        const int offset = static_cast<int>(absSample - clockSample);
        const int note = riff.midi[static_cast<size_t>(s)] + bassTranspose;
        // Velocity: what the guitarist actually played, accented where the kit's
        // kick/snare lands on the same 16th, so the bass locks with the drums
        // instead of sitting on top of them at a flat level.
        float vel01 = (riff.velocity[static_cast<size_t>(s)] > 0)
                          ? static_cast<float>(riff.velocity[static_cast<size_t>(s)]) / 127.0f
                          : 0.58f;
        if ((drumAccentMask >> (s & 63)) & 1ULL)
            vel01 = juce::jmin(0.95f, vel01 * 1.12f);
        patternPlayer.triggerLearnedBassNote(note, vel01, offset, duration,
                                             /*hold=*/false, PatternPlayer::BassSource::Frozen);
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
