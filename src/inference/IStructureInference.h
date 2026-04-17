#pragma once

/**
 * @file
 * @brief Shadow structure classification (parallel to @ref StructureTagger, non-authoritative in Phase 12).
 */

#include "analysis/FeatureVector.h"
#include "analysis/StructureTagger.h"
#include <string>

/**
 * @brief Last shadow metrics for one processed @ref FeatureVector (rule vs ML agreement, gates).
 */
struct StructureShadowMetrics
{
    /** @brief Authoritative rule state from @c FeatureVector::state (StructureTagger). */
    StructureState ruleState = StructureState::SILENT;

    /** @brief True when ONNX logits pass softmax confidence + margin gates (Onnx path only). */
    bool mlValid = false;

    /** @brief Structure class after hold smoothing (rule or ML path). */
    StructureState smoothedState = StructureState::SILENT;

    /** @brief Top-1 softmax probability after gating (0 if ML invalid / rule path). */
    float softmaxTop1 = 0.f;

    /** @brief Top-1 minus top-2 softmax probability (0 if ML invalid / rule path). */
    float margin = 0.f;

    /** @brief Whether @a smoothedState matches @a ruleState. */
    bool agreeRule = false;
};

/**
 * @brief Pluggable shadow structure head (rule mirror or ONNX windowed model).
 *
 * Threading: background inference thread only (same as @ref IInference).
 */
class IStructureInference
{
public:
    virtual ~IStructureInference() = default;

    virtual void prepare(double sampleRate) = 0;
    virtual void reset() = 0;

    /** @brief Consume one feature snapshot (same cadence as pattern inference). */
    virtual void process(const FeatureVector& fv, double dtSec) = 0;

    virtual const StructureShadowMetrics& getLastShadowMetrics() const = 0;
    virtual std::string getName() const = 0;
};
