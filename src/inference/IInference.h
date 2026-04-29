#pragma once

/**
 * @file
 * @brief Abstract pattern-selection strategy (`IInference`).
 */

#include "analysis/FeatureVector.h"
#include <string>

/**
 * @brief Pluggable mapping from guitar analysis features to a pattern index.
 *
 * Implementations may be swapped for Phase 2 (e.g. ONNX). Callers must respect
 * threading: @ref AccompanimentProcessor runs inference on a background thread,
 * not the audio callback.
 */
class IInference
{
public:
    virtual ~IInference() = default;

    /** @brief One-time setup after sample rate is known. */
    virtual void prepare(double sampleRate) = 0;

    /** @brief Choose a pattern index from the current feature vector.
        @param excludeIndex Pattern index to exclude from result; -1 (default) = no exclusion. */
    virtual int selectPattern(const FeatureVector& features, int excludeIndex = -1) = 0;

    /** @brief Human-readable name for logging and UI. */
    virtual std::string getName() const = 0;
};
