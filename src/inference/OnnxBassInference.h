#pragma once

#include "analysis/FeatureVector.h"
#include <atomic>
#include <cstdint>
#include <memory>

/**
 * @brief Phase 13 generative bass head over `bass_model.onnx` — background thread only.
 *
 * Decoded piano-roll: 16 sixteenth-note steps of [pitch_offset, velocity] pairs.
 * @see docs/BASS_ONNX_IO.md
 */
struct BassOnnxProposal
{
    static constexpr int kSteps = 16;

    /** Relative pitch offsets in semitones from conditioned root (index = step). */
    float pitchOffset[kSteps] = {};

    /** MIDI velocity per step; 0.0f = rest/silent step. */
    float velocity[kSteps] = {};

    /** True if the last proposal decoded successfully. */
    bool valid = false;
};

class OnnxBassInference
{
public:
    OnnxBassInference();
    ~OnnxBassInference();

    /** @brief Load from JUCE BinaryData (`bass_model.onnx`). */
    bool tryLoadModel();

    void prepare(double sampleRate);
    void reset();

    /** @brief Pack @a fv into `X_bass`, run ORT, store result in @ref getLastProposal. */
    void propose(const FeatureVector& fv);

    const BassOnnxProposal& getLastProposal() const noexcept { return last; }
    uint64_t getLoadErrorCount() const noexcept { return loadErrorCount.load(std::memory_order_relaxed); }
    uint64_t getRunErrorCount() const noexcept { return runErrorCount.load(std::memory_order_relaxed); }

private:
#if defined(MA_ENABLE_ONNX)
    struct Impl;
    std::unique_ptr<Impl> impl;
#endif
    BassOnnxProposal last{};
    std::atomic<uint64_t> loadErrorCount{ 0 };
    std::atomic<uint64_t> runErrorCount{ 0 };
};
