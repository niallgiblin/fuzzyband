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
#include "inference/GrooveRenderer.h"
#include "inference/pattern_rules.h"
#include "midi/MidiPatternLibrary.h"
#include "midi/GrooveGrid.h"
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
    /** @brief Clear the learned/recorded riff and return to follow (message thread). */
    void requestRiffForget() noexcept { riffForget.store(true, std::memory_order_release); }
    bool isRiffCapturing() const noexcept { return riffCaptureActive.load(std::memory_order_relaxed); }
    int getRiffCaptureNoteCount() const noexcept { return riffCaptureNoteCount.load(std::memory_order_relaxed); }
    /** @brief 0 = count-in (or waiting for bar), 1–4 = recording bar. */
    int getRiffCaptureBar() const noexcept { return riffCaptureBar.load(std::memory_order_relaxed); }
    bool hasLearnedRiff() const noexcept { return riffHeld.load(std::memory_order_relaxed); }

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
    GrooveRenderer grooveRenderer;                  // Tier-1 conditional groove renderer (ONNX)

    std::string activeInferenceName = "None";

    moodycamel::ReaderWriterQueue<FeatureVector> featureQueue{ 4096 };
    moodycamel::ReaderWriterQueue<PatternPlayer::GrooveCommit> grooveCommitQueue{ 32 };
    moodycamel::ReaderWriterQueue<GrooveGrid> grooveGridQueue{ 32 };

    // Mel spectrogram queue: audio thread → inference thread (v0.8.0)
    static constexpr int kMelQueueCapacity = 32;
    struct MelWindow { std::array<float, MelSpectrogramExtractor::kOutputSize> data; };
    moodycamel::ReaderWriterQueue<MelWindow> melQueue{ kMelQueueCapacity };
    std::vector<float> melScratch;  // preallocated: 22050 samples for window read

    std::atomic<int> latestPatternIndex{ 0 };
    std::atomic<bool> resetDrumHoldRequested{ false };

    // Mel spectrogram queue: audio thread → inference thread (v0.8.0)

    std::atomic<float> displayBpm{ 120.0f };
    std::atomic<int> displayStateIndex{ 0 };
    std::atomic<int> displayPatternIndex{ 0 };
    std::atomic<int> displayStyle{ 4 };  // 0=palm_mute,1=open_chord,2=single_note,3=sustain,4=silence
    std::atomic<float> displayRms{ 0.0f };
    std::atomic<float> displayCentroid{ 0.0f };
    std::atomic<float> displayHfFlux{ 0.0f };
    std::atomic<float> displayNoiseFloor{ 0.012f };

    std::atomic<bool> inferenceRunning{ false };
    std::atomic<bool> inferencePaused{ false };
    std::thread inferenceThread;
    std::mutex inferenceDrainMutex;

    int64_t hostSampleTime = 0;

    // Generative groove lock (Phase: lock-in). The audio thread runs the lock
    // state machine; the inference thread and UI read grooveLocked.
    std::atomic<bool> grooveLocked{ false };
    bool grooveLockActive = false;     // audio-thread lock state
    int64_t grooveLockEndSample = -1;  // hold ends here (hostSampleTime frame)
    int64_t grooveLockStartSample = -1; // hold began here (hostSampleTime frame)
    int64_t lastRiffMatchSample = std::numeric_limits<int64_t>::min() / 2;  // last riff-grid attack
    bool prevPhraseLocked = false;     // phrase-lock edge detection
    bool grooveLockReleaseArmed = false;  // P0/R4: arm a transition fill at lock expiry

    // Listening-bass hysteresis (audio thread). While the guitarist is audibly
    // picking, the bass mirrors each detected attack (play-along) instead of the
    // fixed beat 1/3 root drone. Armed on any recent attack and held for a brief
    // grace so sparse playing does not flap back and forth to the drone. Compared
    // against the monotonic audio-thread clock hostSampleTime; self-expiring.
    int64_t bassListenArmedUntilSample = -1;  // -1 = not armed

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
    // When a groove lock expires, instead of releasing straight back to the
    // listener, the engine holds a *contrast* section (pickNextSectionAfterLock)
    // for `transitionBars` bars, then re-engages the riff (A → B → A). The
    // section number counts B, C, … so the UI can show which transition we are
    // in.
    enum class PostLockPhase { Idle, TransitionHold };
    PostLockPhase postLockPhase = PostLockPhase::Idle;
    int64_t transitionEndSample = -1;       // hostSampleTime when the hold ends
    int64_t transitionStartSample = -1;     // hostSampleTime when the hold began (pool rotation)
    PatternRules::SectionPatternPool transitionPool{};  // patterns to rotate during hold
    const char* transitionSectionNameStr = "VERSE";
    int transitionSectionNumberLocal = 0;   // audio-thread section counter
    int transitionBarsTotalLocal = 0;       // hold length in bars (from APVTS)
    std::atomic<bool> transitionSectionActive{ false };
    std::atomic<const char*> transitionSectionName{ "VERSE" };
    std::atomic<int> transitionBarsRemaining{ 0 };
    std::atomic<int> transitionBarsTotal{ 0 };
    std::atomic<int> transitionSectionNumber{ 0 };

    // ── Pool phrasing & seeded rotation (variety): audio-thread state ────────
    // Each section instance seeds its pool rotation from the global bar count
    // at entry, holds each groove for barsPerGrooveForSection bars, and never
    // repeats the immediately-previous groove. Reseeded on section change and
    // on play start, so verse 1 ≠ verse 2 and every Play session re-variates.
    int lastSectionIndex = -1;      // section we last seeded for
    int sectionEntryBar = 0;        // global bar count at current section entry
    bool wasPlayOn = false;         // play-start edge detection (re-seed)
    int lastSeenBarsElapsed = -1;   // loop/restart edge detection (re-seed on wrap)
    int lastPlayedPoolPattern = -1; // immediate-repeat exclusion (play + post-lock)
    int lastRotationSlot = -1;      // phrase slot the rotation was last computed for
    int cachedPoolPick = -1;        // the rotation's pick for lastRotationSlot
    int lastTransitionSlot = -1;    // post-lock hold slot the rotation was computed for
    int cachedTransitionPick = -1;  // the post-lock rotation's pick

    // ── Style steering (perception layer): inference-thread state ────────────
    // The style head (classifyStyle) steers follow-mode selection only after a
    // style has been stable for kStyleStableWindows consecutive windows; the
    // 2-bar commit hold does the rest of the smoothing.
    int lastStyleIndex = 4;         // 4 = silence
    int styleStabilityCount = 0;    // consecutive windows of the same style

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

    // Guitarist-energy dynamic (drums + bass): a slowly-smoothed multiplier driven
    // by the live RMS, so the accompaniment swells when the guitarist digs in and
    // relaxes when they ease off. Audio-thread state.
    float prevBlockRms = 0.0f;
    float guitarEnergySmooth_ = 1.0f;

    std::atomic<double> cachedSampleRate{ 44100.0 };
    std::atomic<int> debugPreviewSamplesRemaining{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentProcessor)
};
