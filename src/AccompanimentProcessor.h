#pragma once

/**
 * @file
 * @brief JUCE @c AudioProcessor: guitar analysis, inference, and MIDI pattern output.
 */

#include <JuceHeader.h>
#include <array>
#include <limits>
#include "analysis/EnergyAnalyser.h"
#include "analysis/StructureTagger.h"
#include "analysis/StructureSequencer.h"
#include "analysis/AudioRingBuffer.h"
#include "analysis/MelSpectrogramExtractor.h"
#include "analysis/PlaybackGate.h"
#include "analysis/PitchEstimator.h"
#include "analysis/StablePitchTracker.h"
#include "analysis/PhraseLearner.h"
#include "analysis/FeatureVector.h"
#include "inference/IInference.h"
#include "inference/pattern_rules.h"
#include "midi/MidiPatternLibrary.h"
#include "midi/PatternPlayer.h"
#include "readerwriterqueue.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @brief Main plugin processor: onset/tempo, energy/structure, rule-based inference, @ref PatternPlayer.
 *
 * Real-time work happens in @ref processBlock; inference runs on a dedicated background thread with a
 * lock-free feature queue (see `ARCHITECTURE.md`).
 */
class AccompanimentProcessor final : public juce::AudioProcessor
{
public:
    AccompanimentProcessor();
    ~AccompanimentProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts; }

    float getDisplayBpm() const noexcept { return displayBpm.load(std::memory_order_relaxed); }
    juce::String getSectionName() const noexcept;
    juce::String getCurrentSectionName() const noexcept;
    int getDisplayStateIndex() const noexcept { return displayStateIndex.load(std::memory_order_relaxed); }
    int getDisplayPatternIndex() const noexcept { return displayPatternIndex.load(std::memory_order_relaxed); }

    /** @brief The pattern the drums are actually playing (last committed). Tests/UI. */
    int getLatestPatternIndex() const noexcept { return latestPatternIndex.load(std::memory_order_relaxed); }
    /** @brief PatternPlayer active index after bar-boundary apply. Tests. */
    int getPlayedPatternIndex() const noexcept { return patternPlayer.getActivePatternIndex(); }
    /** @brief Test-only: pretend inference committed this index (Play constrains it). */
    void injectDrumPatternForTests(int idx) noexcept
    {
        latestPatternIndex.store(idx, std::memory_order_release);
    }
    /** @brief Test-only: how many times inference actually selected a Play/idle pattern. */
    int getInferencePatternSelectCountForTests() const noexcept
    {
        return inferencePatternSelectCount.load(std::memory_order_relaxed);
    }
    int getDisplayStyle() const noexcept { return displayStyle.load(std::memory_order_relaxed); }
    float getDisplayRms() const noexcept { return displayRms.load(std::memory_order_relaxed); }
    float getDisplayCentroid() const noexcept { return displayCentroid.load(std::memory_order_relaxed); }
    float getDisplayHfFlux() const noexcept { return displayHfFlux.load(std::memory_order_relaxed); }
    float getDisplayNoiseFloor() const noexcept { return displayNoiseFloor.load(std::memory_order_relaxed); }

    void bumpDebugPattern();

    /** @brief Human-readable name of the active inference backend (for UI label per D-26-09). */
    const std::string& getActiveInferenceName() const noexcept { return activeInferenceName; }
    uint64_t getOnnxErrorCount() const noexcept;

    /**
     * @brief True when generative mode has auto-locked the groove onto a
     *        repeated riff (drums frozen, bass holding the learned riff).
     *        Written by the audio thread, read by the UI (and inference thread).
     */
    bool isGrooveLocked() const noexcept { return grooveLocked.load(std::memory_order_relaxed); }

    /** @brief 1-based current bar within the riff-lock hold (0 = not locked). */
    int getLockBarCurrent() const noexcept { return lockBarCurrent.load(std::memory_order_relaxed); }
    /** @brief Bars left in the riff-lock hold before the transition (0 = not locked). */
    int getLockBarsRemaining() const noexcept { return lockBarsRemaining.load(std::memory_order_relaxed); }
    /** @brief Total bars in the current riff-lock hold (0 = not locked). */
    int getLockBarsTotal() const noexcept { return lockBarsTotal.load(std::memory_order_relaxed); }

    /** @brief Follow-mode: arm a user riff capture (message thread). Audio thread consumes. */
    void requestRiffCaptureStart() noexcept { riffCaptureStart.store(true, std::memory_order_release); }
    /** @brief Follow-mode: commit the captured riff and start the lock (message thread). */
    void requestRiffCaptureStop() noexcept { riffCaptureStop.store(true, std::memory_order_release); }
    /** @brief Clear the learned/recorded riff and return to idle (message thread). */
    void requestRiffForget() noexcept { riffForget.store(true, std::memory_order_release); }
    bool isRiffCapturing() const noexcept { return riffCaptureActive.load(std::memory_order_relaxed); }
    int getRiffCaptureNoteCount() const noexcept { return riffCaptureNoteCount.load(std::memory_order_relaxed); }
    /** @brief 0 = count-in (or waiting for bar), 1–4 = recording bar. */
    int getRiffCaptureBar() const noexcept { return riffCaptureBar.load(std::memory_order_relaxed); }
    bool hasLearnedRiff() const noexcept { return riffHeld.load(std::memory_order_relaxed); }

    /** @brief Test/debug: occupied 16ths in the Record A snapshot (0 if none). */
    int getRiffAOccupiedCount() const noexcept
    {
        if (!riffA.valid)
            return 0;
        int n = 0;
        for (int i = 0; i < PhraseLearner::kGridSlots; ++i)
            if (riffA.occupied[static_cast<size_t>(i)])
                ++n;
        return n;
    }
    bool getRiffASlotOccupied(int slot) const noexcept
    {
        return riffA.valid && slot >= 0 && slot < PhraseLearner::kGridSlots
            && riffA.occupied[static_cast<size_t>(slot)];
    }
    int getRiffASlotMidi(int slot) const noexcept
    {
        if (!riffA.valid || slot < 0 || slot >= PhraseLearner::kGridSlots
            || !riffA.occupied[static_cast<size_t>(slot)])
            return -1;
        return riffA.midi[static_cast<size_t>(slot)];
    }

    int getDrumA() const noexcept { return drumA; }
    int getDrumB0() const noexcept { return drumB0; }
    int getDrumB() const noexcept { return drumB; }
    bool isRiffBLocked() const noexcept { return enginePhase == EnginePhase::RiffBLocked; }

    int getRiffBOccupiedCount() const noexcept
    {
        if (!riffB.valid)
            return 0;
        int n = 0;
        for (int i = 0; i < PhraseLearner::kGridSlots; ++i)
            if (riffB.occupied[static_cast<size_t>(i)])
                ++n;
        return n;
    }
    bool getRiffBSlotOccupied(int slot) const noexcept
    {
        return riffB.valid && slot >= 0 && slot < PhraseLearner::kGridSlots
            && riffB.occupied[static_cast<size_t>(slot)];
    }
    int getRiffBSlotMidi(int slot) const noexcept
    {
        if (!riffB.valid || slot < 0 || slot >= PhraseLearner::kGridSlots
            || !riffB.occupied[static_cast<size_t>(slot)])
            return -1;
        return riffB.midi[static_cast<size_t>(slot)];
    }

    // ── Post-lock transition grammar (A5.2) ──────────────────────────────────
    // After a groove lock expires, the engine plays a *contrast* section for a
    // few bars before firmly returning to the locked riff (A). UI reads these
    // atomics.

    /** @brief True while a post-lock transition section is playing (drums on the section pool). */
    bool isTransitionSectionActive() const noexcept { return transitionSectionActive.load(std::memory_order_relaxed); }
    /** @brief Name of the current transition section ("CHORUS", "BREAKDOWN", …). */
    const char* getTransitionSectionName() const noexcept { return transitionSectionName.load(std::memory_order_relaxed); }
    /** @brief Bars remaining in the current transition section hold. */
    int getTransitionBarsRemaining() const noexcept { return transitionBarsRemaining.load(std::memory_order_relaxed); }
    /** @brief Total bars in the current transition section hold. */
    int getTransitionBarsTotal() const noexcept { return transitionBarsTotal.load(std::memory_order_relaxed); }
    /** @brief How many distinct transition sections have been visited since the lock released (1 = B, 2 = C, …). */
    int getTransitionSectionNumber() const noexcept { return transitionSectionNumber.load(std::memory_order_relaxed); }

    // ── Unified section-progress display (audio thread → UI) ──────────────────
    // One consistent bar countdown across every armed phase — Play (song form),
    // riff lock (A), and post-lock transition (B/C) — so the guitarist can always
    // anticipate a change. Phase 0=idle, 1=Play, 2=Riff lock (A), 3=Transition (B/C).
    enum class SectionPhase { Idle = 0, Play = 1, Lock = 2, Transition = 3 };

    /** @brief Current armed section phase (0=idle, 1=Play, 2=Lock, 3=Transition). */
    int getSectionPhase() const noexcept { return sectionPhase.load(std::memory_order_relaxed); }
    /** @brief 1-based current bar within the active section (0 when idle). */
    int getSectionBar() const noexcept { return sectionBar.load(std::memory_order_relaxed); }
    /** @brief Total bars in the active section (0 when idle). */
    int getSectionBarsTotal() const noexcept { return sectionBarsTotal.load(std::memory_order_relaxed); }
    /** @brief Bars remaining before the active section changes (≥0). */
    int getSectionBarsRemaining() const noexcept { return sectionBarsRemaining.load(std::memory_order_relaxed); }
    /** @brief Fraction of the active section elapsed, [0,1] (0 when idle). */
    float getSectionProgress() const noexcept { return sectionProgress.load(std::memory_order_relaxed); }

    // ── Display scope: rolling input waveform + playhead (DAW-style) ─────────
    // Ring of decimated input samples. Sized to hold at least a full bar at the
    // tempos the plugin targets so the editor can render a bar-aligned scope
    // with the downbeat at the left and beat notches 1-2-3-4. (8x decimation.)
    static constexpr int kScopeSize = 16384;
    /** @brief Decimated samples per bar at the current BPM (~kScopeSize/... ). */
    int getScopeSamplesPerBar() const noexcept;
    /** @brief Copy of the most recent decimated input samples for UI drawing. */
    void copyScopeSamples(float* out, int maxCount) const noexcept;
    /** @brief Number of samples currently in the scope ring (0..kScopeSize). */
    int getScopeCount() const noexcept { return scopeWriteIndex.load(std::memory_order_relaxed) % kScopeSize; }
    /** @brief Playhead position in [0,1) within the current bar (UI). */
    float getPlayheadFraction() const noexcept { return playheadFraction.load(std::memory_order_relaxed); }

    // Phase 23 rejection signal: written by message thread (Phase 24 button), read/decremented by inference thread.
    std::atomic<int> patternRejectionCount{ 0 };

    std::atomic<bool> playActive{ false };  // Play button state — when true, sequencer runs and playback is forced

    /**
     * @brief Set the editable song form from its serialized string
     *        ("VERSE:8,CHORUS:8,..."). Call on the message thread; the parsed
     *        form is handed to the audio thread lock-free and the string is
     *        persisted with the session (Phase 2).
     */
    void setCustomSongForm(const juce::String& serialized);

    /** @brief Persisted custom form string, or empty if none. Message thread. */
    juce::String getCustomSongForm();

    /** @brief Test-only: stop the background thread from draining @a featureQueue (integration tests). */
    void pauseBackgroundInferenceForTests();
    /** @brief Test-only: run one inference drain synchronously (call while paused). */
    void flushBackgroundInferenceForTests();
    /** @brief Test-only: allow the background inference thread to drain again. */
    void resumeBackgroundInferenceForTests();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void inferenceLoop();
    void drainFeatureQueueAndRunInference();
    /** @brief Smooth a raw style classification into the committed style used downstream. */
    void updateCommittedStyle(int rawStyle) noexcept;
    /** @brief Emit occupied 16ths from a learned-riff snapshot in this block. */
    void emitFrozenRiff(const PhraseLearner::LearnedRiff& riff, int64_t originSample,
                        int numSamples, double bpm, double sr,
                        int64_t clockSample, int bassTranspose) noexcept;
    void stampLearnerGridSlots(const float* in, int numSamples,
                               double beatStart, double beatEnd, double samplesPerBeat,
                               double originBeat, int bassMidi, bool wrapLoop) noexcept;
    /** @brief Capture bar phase from the transport clock and stamp lockOriginMono. */
    void latchLockClock(int64_t transportSample, double samplesPerBeat) noexcept;
    /** @brief Frozen-riff origin in the monotonic frame (bar-phase aligned). */
    int64_t frozenRiffOriginMono(double samplesPerBeat) const noexcept;
    /** @brief On a host seek/loop wrap, re-latch bar phase; keep remaining duration. */
    void reanchorLockClockOnJump(int64_t transportSample, double samplesPerBeat) noexcept;
    struct OutgoingFillArm
    {
        bool lastBar = false;
        bool penultimate = false;
        bool deferred19 = false;
        void clear() noexcept { lastBar = penultimate = deferred19 = false; }
    };
    void updateOutgoingFill(OutgoingFillArm& arm, bool isLast, bool isPenultimate,
                            float rms, unsigned seed, double beatInBar) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    EnergyAnalyser energyAnalyser;
    StructureTagger structureTagger;
    PitchEstimator pitchEstimator;
    StablePitchTracker stablePitchTracker;
    PhraseLearner phraseLearner;
    StructureSequencer structureSequencer;
    AudioRingBuffer audioRingBuffer{ 22050 };
    MelSpectrogramExtractor melExtractor;
    MidiPatternLibrary patternLibrary;
    PatternPlayer patternPlayer;

    std::unique_ptr<IInference> inference;

    std::string activeInferenceName = "None";

    moodycamel::ReaderWriterQueue<FeatureVector> featureQueue{ 4096 };
    moodycamel::ReaderWriterQueue<PatternPlayer::GrooveCommit> grooveCommitQueue{ 32 };

    // Mel spectrogram queue: audio thread → inference thread (v0.8.0)
    static constexpr int kMelQueueCapacity = 32;
    struct MelWindow { std::array<float, MelSpectrogramExtractor::kOutputSize> data; };
    moodycamel::ReaderWriterQueue<MelWindow> melQueue{ kMelQueueCapacity };
    std::vector<float> melScratch;  // preallocated: 22050 samples for window read

    std::atomic<int> latestPatternIndex{ 0 };
    std::atomic<bool> resetDrumHoldRequested{ false };
    std::atomic<bool> patternSelectFrozen{ false };
    std::atomic<int> inferencePatternSelectCount{ 0 };

    // Mel spectrogram queue: audio thread → inference thread (v0.8.0)

    std::atomic<float> displayBpm{ 120.0f };
    std::atomic<int> displayStateIndex{ 0 };
    std::atomic<int> displayPatternIndex{ 0 };
    std::atomic<int> displayStyle{ -1 };  // -1=not classified yet; 0=palm_mute..4=silence
    std::atomic<float> displayRms{ 0.0f };
    std::atomic<float> displayCentroid{ 0.0f };
    std::atomic<float> displayHfFlux{ 0.0f };
    std::atomic<float> displayNoiseFloor{ 0.012f };

    std::atomic<bool> inferenceRunning{ false };
    std::atomic<bool> inferencePaused{ false };
    std::thread inferenceThread;
    std::mutex inferenceDrainMutex;

    int64_t hostSampleTime = 0;
    int64_t lastClockSample = -1;          // previous block's resolved transport sample
    int lastClockBlockSamples = 0;         // previous block size (jump detection)

    // Generative groove lock (Phase: lock-in). The audio thread runs the lock
    // state machine; the inference thread and UI read grooveLocked.
    std::atomic<bool> grooveLocked{ false };
    enum class EnginePhase {
        Idle, PlayCountIn, PlaySection,
        RecWaitBar, RecCountIn, RecCapture,
        RiffA, RiffBListen, RiffBLocked
    };
    EnginePhase enginePhase = EnginePhase::Idle;
    PhraseLearner::LearnedRiff riffA{};
    PhraseLearner::LearnedRiff riffB{};
    int64_t riffAPlayOriginMono = -1;   // monotonic hostSampleTime-frame origin
    int64_t riffBPlayOriginMono = -1;
    int drumA = 0;
    int drumB0 = 0;
    int drumB = 0;
    std::atomic<int> playSectionIndex{ -1 };
    std::atomic<bool> requestBLockPick{ false };
    std::atomic<int> bLockPick{ -1 };
    int64_t guitarSilentSamples = 0;
    bool grooveLockActive = false;     // derived: enginePhase == RiffA (UI/tests)
    bool riffLoopActive = false;       // derived: any Riff* phase
    int64_t lockOriginMono = -1;       // hostSampleTime when the current A/B cycle began
    double  lockBarPhaseBeats = 0.0;   // fmod(transport beats at engage, 4) — bar alignment
    int64_t grooveLockEndMono = -1;    // hold ends here (monotonic hostSampleTime frame)
    int64_t grooveLockStartMono = -1;  // hold began here (monotonic hostSampleTime frame)
    int64_t lastRiffMatchSample = std::numeric_limits<int64_t>::min() / 2;  // last riff-grid attack
    bool prevPhraseLocked = false;     // phrase-lock edge detection
    bool grooveLockReleaseArmed = false;  // P0/R4: arm a transition fill at lock expiry

    // ── Riff-lock hold progress (audio thread → UI) ───────────────────────────
    // While a riff lock is held (recorded take or live grid listen), the UI
    // shows "bar X of Y, N left" so the guitarist knows when the transition
    // fires. All values 0 when not locked.
    std::atomic<int> lockBarCurrent{ 0 };    // 1-based current bar in the hold
    std::atomic<int> lockBarsRemaining{ 0 }; // bars left before the transition
    std::atomic<int> lockBarsTotal{ 0 };     // total bars in the hold

    std::atomic<bool> riffCaptureStart{ false };   // message → audio: begin capture
    std::atomic<bool> riffCaptureStop{ false };    // message → audio: commit/cancel capture
    std::atomic<bool> riffForget{ false };         // message → audio: wipe learned riff
    std::atomic<bool> riffCaptureActive{ false };  // audio → UI
    std::atomic<int> riffCaptureNoteCount{ 0 };    // occupied 16th slots → UI
    std::atomic<int> riffCaptureBar{ 0 };          // 0=count-in, 1–4=recording bar
    std::atomic<bool> riffHeld{ false };           // phraseLearner.isLocked() → UI

    enum class RiffCapturePhase { Idle, WaitBar, CountIn, Recording };
    RiffCapturePhase riffCapturePhase = RiffCapturePhase::Idle;
    double riffCountInStartBeat = 0.0;

    // ── Post-lock transition grammar (A5.2): audio-thread state ──────────────
    // When a groove lock expires, the engine holds a *contrast* section
    // (pickNextSectionAfterLock) for `transitionBars` bars, then ALWAYS
    // re-engages the riff (A). `transitionSections` is how many distinct
    // contrasts to visit across successive lock cycles — A → B → A → C → A
    // when set to 2 — not a chain of B → C without returning to A. Each
    // slot (B, C, …) is pinned to one groove family so 1 = A-B-A-B (same B),
    // not a new CHORUS/SOLO every time. The section number counts B, C, … so
    // the UI can show which contrast we are in.
    enum class PostLockPhase { Idle, TransitionHold };
    PostLockPhase postLockPhase = PostLockPhase::Idle;
    int64_t transitionEndMono = -1;         // hostSampleTime when the hold ends
    int64_t transitionStartMono = -1;       // hostSampleTime when the hold began (pool rotation)
    PatternRules::SectionPatternPool transitionPool{};  // patterns to rotate during hold
    const char* transitionSectionNameStr = "VERSE";
    int transitionSectionNumberLocal = 0;   // audio-thread section counter
    static constexpr int kMaxTransitionSlots = 4;
    const char* transitionSlotNames[kMaxTransitionSlots] {};  // pinned family per B/C/D/E
    bool transitionSlotPinned[kMaxTransitionSlots] {};
    int transitionBarsTotalLocal = 0;       // hold length in bars (from APVTS)
    std::atomic<bool> transitionSectionActive{ false };
    std::atomic<const char*> transitionSectionName{ "VERSE" };
    std::atomic<int> transitionBarsRemaining{ 0 };
    std::atomic<int> transitionBarsTotal{ 0 };
    std::atomic<int> transitionSectionNumber{ 0 };

    // ── Unified section-progress display (audio thread → UI) ──────────────────
    std::atomic<int> sectionPhase{ 0 };          // 0=idle,1=Play,2=Lock,3=Transition
    std::atomic<int> sectionBar{ 0 };            // 1-based current bar
    std::atomic<int> sectionBarsTotal{ 0 };
    std::atomic<int> sectionBarsRemaining{ 0 };
    std::atomic<float> sectionProgress{ 0.0f };  // elapsed fraction [0,1]

    // ── Play / post-lock section tracking ────────────────────────────────────
    // Play rotates the section pool (T4.1); Record B freezes drumB0 then drumB.
    int lastSectionIndex = -1;      // section we last seeded for
    std::atomic<int> sectionEntryBar{ 0 };  // bar count at section/state entry (mel seed)
    int lastPlayedPoolPattern = -1; // previous Play-slot pick (never immediate-repeat)
    int lastPlayGrooveSlot = -1;    // groove slot last committed in this section
    int lastFollowStateIndex = -1;  // follow-mode structure state for mel seed
    bool wasPlayOn = false;         // play-start edge detection (re-seed)
    // Play-mode count-in: 1 bar of click (kick 1, stick 2/3/4) before the form
    // starts, mirroring the Record-riff count-in.
    bool playCountInActive = false;
    bool playCountInWaitingBar = false;
    double playCountInStartBeat = 0.0;
    int lastSeenBarsElapsed = -1;   // loop/restart edge detection (re-seed on wrap)
    OutgoingFillArm playFillArm{};
    OutgoingFillArm riffAFillArm{};
    OutgoingFillArm riffBFillArm{};

    // ── Style steering (perception layer): inference-thread state ────────────
    // classifyStyle() returns a raw argmax once per mel window (~2 Hz: one 512 ms
    // audio window). The raw value is noisy, so we commit a style only after
    // kStyleStableWindows consecutive agreeing windows, then hold it for
    // kStyleHoldWindows windows before allowing a change. The *committed* style
    // drives steering, the groove-renderer condition and the display — never the
    // raw value — so a phrase doesn't flip back and forth between articulations.
    int styleRaw = 4;            // last raw classification (4 = silence)
    int styleAgreeCount = 0;     // consecutive windows agreeing with styleRaw
    int committedStyle = -1;     // smoothed style actually used downstream; -1 = none yet
    int styleHoldRemaining = 0;  // hold countdown after a commit

    // ── Display scope (audio thread → UI) ────────────────────────────────────
    std::array<float, kScopeSize> scopeSamples{};      // rolling decimated input
    std::atomic<int> scopeWriteIndex{ 0 };
    std::atomic<float> playheadFraction{ 0.0f };       // position within current bar

    // Phase 2: editable/persistent song form. The message thread parses the
    // serialized form into a shared SongForm and bumps `songFormVersion`; the
    // audio thread observes the version change and calls loadForm(). The
    // shared_ptr is exchanged with the C++11 free atomic_load/atomic_store
    // helpers (this libc++ predates the C++20 std::atomic<shared_ptr> type).
    std::shared_ptr<SongForm> pendingSongForm;
    std::atomic<int> songFormVersion{ 0 };
    int loadedSongFormVersion = -1;

    int64_t lastDrumPatternChangeSample = -1;
    StructureState lastCommittedStructureState = StructureState::SILENT;

    PlaybackGate playbackGate;

    // Guitarist-energy dynamic (drums + bass): a ~500 ms RMS swell mapped onto
    // [0.85, 1.20] so the accompaniment sits below unity when the guitarist eases
    // off and above it when they dig in. Audio-thread state.
    float prevBlockRms = 0.0f;
    float guitarEnergyRms_ = 0.0f;

    std::atomic<double> cachedSampleRate{ 44100.0 };
    std::atomic<int> debugPreviewSamplesRemaining{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentProcessor)
};
