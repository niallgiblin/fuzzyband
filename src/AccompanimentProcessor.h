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

    /**
     * @brief Test/debug: how many bass note-ons each producer has emitted.
     *
     * The recurring "bass doesn't mirror" bug is a ratio question. `learned`
     * counts mirror / frozen-snapshot notes, `grid` the authored/harmonic
     * fallback. Mirror-primary means learned must dominate grid while the
     * guitarist is playing.
     */
    int getLearnedBassNoteCount() const noexcept { return patternPlayer.getLearnedBassNoteCount(); }
    int getGridBassNoteCount() const noexcept { return patternPlayer.getGridBassNoteCount(); }

    /** @brief Test/debug: detector predicate counters (see PhraseLearner::AttackDebug). */
    PhraseLearner::AttackDebug getAttackDebug() const noexcept { return phraseLearner.getAttackDebug(); }
    void resetAttackDebug() noexcept { phraseLearner.resetAttackDebug(); }

    /** @brief Test/debug: note-ons emitted by a specific bass producer (provenance). */
    int getBassProducerCount(BassVoice::Producer p) const noexcept
    {
        return patternPlayer.getBassProducerCount(p);
    }
    /** @brief Test/debug: producer of the most recent bass note-on. */
    BassVoice::Producer getLastBassProducer() const noexcept
    {
        return patternPlayer.getLastBassProducer();
    }

    /** @brief Test/debug: recent bass note-ons with producer provenance. */
    int getRecentBassNoteOns(BassVoice::NoteOn* out, int maxCount) const noexcept
    {
        return patternPlayer.getRecentBassNoteOns(out, maxCount);
    }

    // ── Step 2: Play-mode per-section riff learning (test/debug surface) ─────
    /** @brief True while Play is capturing the current section's riff (first pass). */
    bool isPlaySectionCapturing() const noexcept { return playTakeActive; }
    /** @brief True while Play is replaying a stored section riff (a return). */
    bool isPlaySectionReplaying() const noexcept { return playTakeReplaying; }
    /** @brief Section name of the active Play take/replay (empty when idle). */
    const char* getPlayTakeSectionName() const noexcept { return playTakeSectionName; }
    /** @brief Occupied 16th slots in the stored Play riff for @p name (0 if none). */
    int getStoredSectionRiffOccupiedCount(const char* name) const noexcept;
    /** @brief Occupancy of 16th slot @p slot in the stored Play riff for @p name. */
    bool getStoredSectionSlotOccupied(const char* name, int slot) const noexcept;
    /** @brief Bass MIDI of 16th slot @p slot in the stored Play riff for @p name. */
    int getStoredSectionSlotMidi(const char* name, int slot) const noexcept;

    /** @brief Test/debug: occupied 16ths in the Record A snapshot (0 if none). */
    int getRiffAOccupiedCount() const noexcept;
    bool getRiffASlotOccupied(int slot) const noexcept;
    int getRiffASlotMidi(int slot) const noexcept;
    int getRiffASlotGate(int slot) const noexcept;

    int getDrumA() const noexcept;
    int getDrumB0() const noexcept;
    int getDrumB() const noexcept;
    bool isRiffBLocked() const noexcept;

    int getRiffBOccupiedCount() const noexcept;
    bool getRiffBSlotOccupied(int slot) const noexcept;
    int getRiffBSlotMidi(int slot) const noexcept;
    int getRiffBSlotGate(int slot) const noexcept;

    /**
     * @brief Test/debug: bar-locked playback origin of the frozen riff, in the
     *        monotonic `hostSampleTime` frame (-1 when the riff is not playing).
     *
     *        This is the bar line the loop is measured from. It is NOT the block
     *        boundary that detected the lock: the detecting block may straddle the
     *        bar line, so the two can differ by up to one block.
     */
    int64_t getRiffAPlayOriginSample() const noexcept;
    int64_t getRiffBPlayOriginSample() const noexcept;

    // ── Post-lock transition grammar (A5.2) ──────────────────────────────────
    // After a groove lock expires, the engine plays a *contrast* section for a
    // few bars before firmly returning to the locked riff (A). UI reads these
    // atomics.

    /** @brief True while a post-lock transition section is playing (drums on the section pool). */
    bool isTransitionSectionActive() const noexcept { return transitionSectionActive.load(std::memory_order_relaxed); }
    /** @brief True while a learned contrast riff is replaying (Record mode). */
    bool isTransitionTakeReplaying() const noexcept { return transitionTakeReplaying; }
    /** @brief True while the current contrast slot is being captured. */
    bool isTransitionTakeActive() const noexcept { return transitionTakeActive; }
    /** @brief Monotonic sample the contrast replay loops from (-1 if none). */
    int64_t getTransitionReplayOriginSample() const noexcept { return transitionReplayOriginMono; }
    /** @brief Occupied 16ths in contrast slot @p slot 's memory (0 = none). */
    int getTransitionRiffOccupiedCount(int slot) const noexcept;
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

    /**
     * @brief True while Play's 1-bar click count-in is running, before the song
     *        form starts.
     *
     * The editor shows the same count-in status as Record riff while this is
     * true. SectionPhase stays Idle during the count-in (nothing is playing
     * yet), so the count-in needs its own flag.
     */
    bool isPlayCountingIn() const noexcept { return playCountInActive.load(std::memory_order_relaxed); }

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
    /** @brief Emit onset slots from a learned-riff snapshot, gated by T5.2. */
    // 16th-slot mask (tiled over the 4-bar loop) of the active drum pattern's
    // kick/snare positions. Frozen bass notes that already land on one are
    // accented so the bass locks with the kit instead of sitting flat on top of
    // it. Rebuilt only when the active pattern changes (docs/BASS_MIRRORING §22).
    std::uint64_t drumAccentMask = 0;
    int lastAccentPatternIdx = -1;
    std::uint64_t buildDrumAccentMask(int patternIdx) const noexcept;

    void emitFrozenRiff(const PhraseLearner::LearnedRiff& riff, int64_t originSample,
                        int numSamples, double bpm, double sr,
                        int64_t clockSample, int bassTranspose) noexcept;
    void stampLearnerGridSlots(const float* in, int numSamples,
                               double beatStart, double beatEnd, double samplesPerBeat,
                               double originBeat, int bassMidi, bool wrapLoop) noexcept;

    // ── Step 2: per-section riff memory (Play mode) ──────────────────────────
    // Keyed by section NAME, not index: the same name recurs at different form
    // indices, and those recurrences are exactly what must replay the learned
    // riff. A CHORUS riff is not a VERSE riff. Fixed array — audio thread safe.
    static constexpr int kSectionRiffSlots = 8;
    struct SectionRiffMemory
    {
        bool valid = false;
        char name[16] {};                        // "VERSE", "CHORUS", ...
        PhraseLearner::LearnedRiff riff {};
        int64_t learnedAtMono = -1;              // diagnostics
    };
    std::array<SectionRiffMemory, kSectionRiffSlots> sectionRiffs {};
    int sectionRiffWrite = 0;                    // ring for overflow

    // Play-mode section take state (audio thread).
    bool playTakeActive = false;                 // capturing this section (first pass)
    bool playTakeReplaying = false;              // replaying a stored riff (a return)
    char playTakeSectionName[16] {};
    int64_t playTakeOriginMono = -1;             // monotonic replay-loop origin (bar-aligned)
    double playTakeOriginBeat = 0.0;             // transport beat of the section's bar 1

    /** @brief Step 2: store the active take, then start capture/replay for a new section. */
    void beginPlaySectionTake(const char* name, int64_t clockSample, double samplesPerBeat) noexcept;
    /** @brief Step 2: snapshot + store the active Play take, then clear take state. */
    void storePlaySectionTake() noexcept;
    /** @brief Step 2: locate a stored snapshot by section name (nullptr if none). */
    const PhraseLearner::LearnedRiff* findSectionRiff(const char* name) const noexcept;
    /** @brief Step 2: drop all per-section memory and take state (Play start). */
    void resetSectionRiffMemory() noexcept;

    // ── Record-mode transition riff memory (docs/BASS_MIRRORING.md §17) ───────
    // Each contrast slot (B/C/D/E) learns the riff played through its FIRST
    // visit and replays it on every return, exactly like Riff A. Before 1.0.22
    // the transition only mirrored the live player, so the bass rested (went
    // silent) the moment the player stopped and the contrast was never
    // remembered. Keyed by the pinned slot index, which `resetTransitionCycle`
    // clears together with the slot->family pins.
    void beginTransitionTake(int slot, double samplesPerBeat) noexcept;
    void storeTransitionTake() noexcept;
    void storeTransitionRiff(int slot, const PhraseLearner::LearnedRiff& riff) noexcept;
    void clearTransitionMemory() noexcept;
    const PhraseLearner::LearnedRiff* findTransitionRiff(int slot) const noexcept;

    // ── Onset-aligned mirror pitch (docs/BASS_MIRRORING.md §10) ──────────────
    // The block-level YIN estimate ends at the current sample, so at a pick it
    // is still dominated by the PREVIOUS note and the mirror plays one note
    // behind (measured pitch-class match ~50% vs 97% for an onset window).
    // Mirror notes are deferred by `mirrorPitchWindow` samples and pitched from
    // the window that STARTS at the pick, so the bass plays the note actually
    // picked. The delay is a fixed number of samples (buffer-invariant).
    static constexpr int kMaxPendingMirror = 64;
    struct PendingMirror
    {
        int64_t targetAbs = 0;   // grid-snapped absolute sample of the pick
        float velocity = 0.58f;
        int fallbackNote = 36;   // learner note if the onset estimate fails
    };
    static constexpr int kMaxOnsetWindow = 2048;   // analysis cap (samples)
    std::array<PendingMirror, kMaxPendingMirror> pendingMirror{};
    int pendingMirrorCount = 0;
    int mirrorPitchWindow = 1024;   // LATENCY in samples; set in prepareToPlay
    int lastOnsetMirrorMidi = -1;   // most recent onset-resolved mirror note
    int mirrorHeldPc = -1;          // pitch class of the currently held mirror note (-1 none)
    int legatoPcPending = INT_MIN;  // fixed-hop legato pitch candidate
    int legatoHops = 0;             // consecutive hops the candidate has held
    // Absolute hop sample of the last detected attack. The legato follow is
    // suppressed for one onset window after a pick, so the hops between the pick
    // and its (deferred) flush cannot re-emit the same note. HOP-driven, not
    // `pendingMirrorCount == 0`: the queue drains once per block, so a
    // queue-based guard made the legato note count depend on the host buffer.
    int64_t lastMirrorAttackHopAbs = std::numeric_limits<int64_t>::min();

    // Recent detected attacks (absolute samples) — the riff capture marks a 16th
    // as an onset when a real pick falls inside it, instead of merging re-picks
    // into one long gate (the "learned riff is a legato drone" bug).
    static constexpr int kMaxRecentAttacks = 64;
    std::array<int64_t, kMaxRecentAttacks> recentAttackAbs{};
    int recentAttackWrite = 0;
    int recentAttackCount = 0;
    int64_t captureSlotStartAbs = -1;
    int64_t captureSlotSamples = 0;

    void enqueueMirrorTrigger(int64_t targetAbs, float velocity, int fallbackNote) noexcept;
    void flushMirrorTriggers(int numSamples, int64_t blockEndAbs, int bassTranspose,
                             int durationSamples, bool emit) noexcept;
    void recordRecentAttack(int64_t abs) noexcept;
    bool attackInCaptureSlot(int64_t fromAbs, int64_t toAbs) const noexcept;
    void clearPendingMirror() noexcept { pendingMirrorCount = 0; }
    void resetSlotOnsetTracker() noexcept;
    void flushPendingCaptureSlot() noexcept;
    /** @brief Capture bar phase from the transport clock and stamp lockOriginMono. */
    void latchLockClock(int64_t transportSample, double samplesPerBeat) noexcept;
    /** @brief Frozen-riff origin in the monotonic frame (bar-phase aligned). */
    int64_t frozenRiffOriginMono(double samplesPerBeat) const noexcept;
    /** @brief On a host seek/loop wrap, re-latch bar phase; keep remaining duration. */
    void reanchorLockClockOnJump(int64_t transportSample, double samplesPerBeat) noexcept;
    /** @brief Publish an immutable riff/phase snapshot for the UI (T8.2). */
    void publishRiffUiSnapshot() noexcept;
    struct OutgoingFillArm
    {
        bool lastBar = false;
        bool penultimate = false;
        bool deferred19 = false;
        void clear() noexcept { lastBar = penultimate = deferred19 = false; }
    };
    void updateOutgoingFill(OutgoingFillArm& arm, bool isLast, bool isPenultimate,
                            float rms, unsigned seed, double lastBarOriginBeat) noexcept;
    /** @brief Host-grid beat of the current Play section's last-bar downbeat (T7.2). */
    double playLastBarOriginBeat(int64_t clockSample, double samplesPerBeat) const noexcept;

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
    int64_t lastLearnerHopAbs = -1;    // absolute sample of the last fixed-hop learner call
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
    // T8.2: UI/tests read riffA/riffB/enginePhase via a triple-buffer snapshot
    // so the audio thread never takes a lock. The reader pins the published
    // slot; the writer always copies into a slot that is neither published
    // nor currently being read.
    struct RiffUiSnapshot
    {
        PhraseLearner::LearnedRiff riffA{};
        PhraseLearner::LearnedRiff riffB{};
        EnginePhase enginePhase = EnginePhase::Idle;
        int drumA = 0;
        int drumB0 = 0;
        int drumB = 0;
    };
    static constexpr int kRiffUiSlots = 3;
    std::array<RiffUiSnapshot, kRiffUiSlots> riffUiSlots{};
    std::atomic<int> riffUiPublished{ 0 };
    mutable std::atomic<int> riffUiReading{ -1 };
    struct RiffUiRead
    {
        explicit RiffUiRead(const AccompanimentProcessor& p) noexcept;
        ~RiffUiRead() noexcept;
        const RiffUiSnapshot& get() const noexcept;
        RiffUiRead(const RiffUiRead&) = delete;
        RiffUiRead& operator=(const RiffUiRead&) = delete;
    private:
        const AccompanimentProcessor& proc;
        int slot = 0;
    };
    // T5.2: previous 16th's peak / trailing level for re-attack detection.
    float prevSlotPeak_ = 0.0f;
    float prevSlotEnd_ = 0.0f;
    bool prevSlotOccupied_ = false;
    int captureSlotIndex_ = -1;
    float captureSlotPeak_ = 0.0f;
    float captureSlotEnd_ = 0.0f;
    int captureSlotMidi_ = 36;
    // Per-16th pitch vote (docs/BASS_MIRRORING.md §16). The stored slot pitch
    // used to be "the last attack's pitch", stamped on every sustained 16th, so
    // one bad estimate poisoned a whole held note and a slot with no fresh pick
    // inherited an unrelated stale note. Instead every confident fixed-hop
    // estimate inside the slot gets a vote and the majority wins. The hop pitch
    // comes from the trailing 2048-sample window, so it is available
    // continuously — not only at attacks.
    static constexpr float kCaptureVoteConf = 0.30f;
    std::array<std::uint16_t, 12> captureVoteCounts_{};
    void voteCaptureSlotPitch(float midi, float conf) noexcept;
    int64_t riffAPlayOriginMono = -1;   // monotonic hostSampleTime-frame origin
    int64_t riffBPlayOriginMono = -1;
    int drumA = 0;
    int drumB0 = 0;
    int drumB = 0;
    std::atomic<int> playSectionIndex{ -1 };
    std::atomic<bool> requestBLockPick{ false };
    std::atomic<int> bLockPick{ -1 };
    int64_t guitarSilentSamples = 0;
    // Pitch class (0–11, C = drop-C root) the guitarist last played, or INT_MIN.
    // Keeps the harmony fallback in key once the tracker resets on silence.
    int lastBassPitchClassOffset = INT_MIN;
    bool grooveLockActive = false;     // derived: enginePhase == RiffA (UI/tests)
    bool riffLoopActive = false;       // derived: any Riff* phase
    int64_t lockOriginMono = -1;       // hostSampleTime when the current A/B cycle began
    double  lockBarPhaseBeats = 0.0;   // fmod(transport beats at engage, 4) — bar alignment
    int64_t grooveLockEndMono = -1;    // hold ends here (monotonic hostSampleTime frame)
    int64_t grooveLockStartMono = -1;  // hold began here (monotonic hostSampleTime frame)
    int64_t lastRiffMatchSample = std::numeric_limits<int64_t>::min() / 2;  // T6.2: last A-riff match (cut-short)
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
    // Learned contrast riff per slot (see beginTransitionTake).
    std::array<SectionRiffMemory, kMaxTransitionSlots> transitionRiffs {};
    bool transitionTakeActive = false;      // capturing the current contrast slot
    bool transitionTakeReplaying = false;   // replaying that slot's stored riff
    int transitionTakeSlot = -1;
    int64_t transitionReplayOriginMono = -1;
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
    // Play rotates the section pool (T4.1); Record B-listen rotates the
    // contrast pool (T6.3) and freezes drumB only after a B lock.
    int lastSectionIndex = -1;      // section we last seeded for
    std::atomic<int> sectionEntryBar{ 0 };  // bar count at section/state entry (mel seed)
    int lastPlayedPoolPattern = -1; // previous Play-slot pick (never immediate-repeat)
    int lastPlayGrooveSlot = -1;    // groove slot last committed in this section
    int lastFollowStateIndex = -1;  // follow-mode structure state for mel seed
    int lastBListenPoolPattern = -1; // previous B-listen pool pick (T6.3)
    int lastBListenGrooveSlot = -1;
    bool transitionCutShortArmed = false;  // T6.2: same-riff exit at next bar
    int sameRiffMatchCount = 0;            // consecutive A-riff matches during B
    int64_t lastRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
    int64_t firstRefMatchMono = std::numeric_limits<int64_t>::min() / 2;
    bool wasPlayOn = false;         // play-start edge detection (re-seed)
    // Play-mode count-in: 1 bar of click (kick 1, stick 2/3/4) before the form
    // starts, mirroring the Record-riff count-in. Atomic because the editor
    // reads it (isPlayCountingIn) to show the count-in status.
    std::atomic<bool> playCountInActive{ false };
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

    bool lastLoopValue = false;  // per-instance; was a function-local static (T8.2)

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
