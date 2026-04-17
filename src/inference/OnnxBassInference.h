#pragma once

#include "analysis/FeatureVector.h"
#include <memory>

/**
 * @brief Phase 13 generative bass head over `bass_model.onnx` — background thread only.
 *
 * @see docs/BASS_ONNX_IO.md
 */
struct BassOnnxProposal
{
    float confidence = 0.f;
    float rootMidi = 40.f;
    float durationBeats = 1.f;
    float margin = 0.f;
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

private:
#if defined(MA_ENABLE_ONNX)
    struct Impl;
    std::unique_ptr<Impl> impl;
#endif
    BassOnnxProposal last{};
};
