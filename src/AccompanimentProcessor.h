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
    int64_t lastRiffMatchSample = std::numeric_limits<int64_t>::min() / 2;  // last riff-grid attack
    bool prevPhraseLocked = false;     // phrase-lock edge detection
    bool grooveLockReleaseArmed = false;  // P0/R4: arm a transition fill at lock expiry

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

    float prevBlockRms = 0.0f;

    std::atomic<double> cachedSampleRate{ 44100.0 };
    std::atomic<int> debugPreviewSamplesRemaining{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentProcessor)
};
