#pragma once

/**
 * @file
 * @brief JUCE @c AudioProcessor: guitar analysis, inference, and MIDI pattern output.
 */

#include <JuceHeader.h>
#include "analysis/OnsetDetector.h"
#include "analysis/EnergyAnalyser.h"
#include "analysis/StructureTagger.h"
#include "analysis/PitchEstimator.h"
#include "analysis/BeatTracker.h"
#include "analysis/TempoStabiliser.h"
#include "analysis/FeatureVector.h"
#include "inference/IInference.h"
#include "inference/IStructureInference.h"
#include "inference/OnnxBassInference.h"
#include "midi/MidiPatternLibrary.h"
#include "midi/PatternPlayer.h"
#include "readerwriterqueue.h"
#include <atomic>
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
    int getDisplayStateIndex() const noexcept { return displayStateIndex.load(std::memory_order_relaxed); }
    int getDisplayPatternIndex() const noexcept { return displayPatternIndex.load(std::memory_order_relaxed); }
    float getDisplayRms() const noexcept { return displayRms.load(std::memory_order_relaxed); }
    float getDisplayCentroid() const noexcept { return displayCentroid.load(std::memory_order_relaxed); }
    float getDisplayHfFlux() const noexcept { return displayHfFlux.load(std::memory_order_relaxed); }
    float getDisplayBeatConfidence() const noexcept { return displayBeatConfidence.load(std::memory_order_relaxed); }

    void bumpDebugPattern();

    /** @brief Human-readable name of the active inference backend (for UI label per D-26-09). */
    const std::string& getActiveInferenceName() const noexcept { return activeInferenceName; }

    // Phase 23 rejection signal: written by message thread (Phase 24 button), read/decremented by inference thread.
    std::atomic<int> patternRejectionCount{ 0 };

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

    OnsetDetector onsetDetector;
    EnergyAnalyser energyAnalyser;
    StructureTagger structureTagger;
    PitchEstimator pitchEstimator;
    BeatTracker beatTracker;
    TempoStabiliser tempoStabiliser;
    MidiPatternLibrary patternLibrary;
    PatternPlayer patternPlayer;

    std::unique_ptr<IInference> inference;
    std::unique_ptr<IStructureInference> structureInference;
    std::unique_ptr<OnnxBassInference> bassInference;

    std::string activeInferenceName = "None";

    moodycamel::ReaderWriterQueue<FeatureVector> featureQueue{ 4096 };
    std::atomic<int> latestPatternIndex{ 0 };

    std::atomic<bool> useGenerativeBass{ false };

    // Phase 23 piano-roll bass: written by inference thread, read by audio thread.
    // Single-producer single-consumer — inference writes, audio reads + clears.
    float genBassPitchOffsets[16] = {};
    float genBassVelocities[16] = {};
    float genBassRootMidi = 40.0f;
    std::atomic<bool> genBassStepsReady{ false };

    std::atomic<float> displayBpm{ 120.0f };
    std::atomic<int> displayStateIndex{ 0 };
    std::atomic<int> displayPatternIndex{ 0 };

    std::atomic<float> displayRms{ 0.0f };
    std::atomic<float> displayCentroid{ 0.0f };
    std::atomic<float> displayHfFlux{ 0.0f };
    std::atomic<float> displayBeatConfidence{ 0.0f };

    std::atomic<bool> inferenceRunning{ false };
    /** Starts true so inference does not run until prepareToPlay() completes. */
    std::atomic<bool> inferencePaused{ true };
    std::thread inferenceThread;
    std::mutex inferenceDrainMutex;

    int64_t hostSampleTime = 0;
    int64_t lastFeatureSampleTs = -1;

    float heldPitchRootMidi = 40.0f;
    float heldPitchConfidence = 0.0f;
    bool pitchHoldValid = false;
    int pitchStableCounterSamples = 0;
    float lastStablePitchMidi = 40.0f;
    int64_t lastBassUpdateSample = -1;   // -1 = never committed
    int64_t lastDrumPatternChangeSample = -1; // -1 = never committed; inference thread

    /** @brief When true, drummer may sound (tracker gate replaces 4-onset count-in). */
    bool playbackGateOpen     = false;
    bool prevPlaybackGateOpen = false;

    float prevBlockRms = 0.0f;

    int  silenceSamples     = 0;
    int  activeNonSilentSamples = 0;
    bool inPhraseBreath     = false;
    bool prevStructureSilent = false;

    bool pendingBeatSnap = false;
    double prevBeatPhase01 = 0.0;
    int64_t pendingBeatSnapSamples = 0;

    std::atomic<double> cachedSampleRate{ 44100.0 };
    std::atomic<int> debugPreviewSamplesRemaining{ 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccompanimentProcessor)
};
