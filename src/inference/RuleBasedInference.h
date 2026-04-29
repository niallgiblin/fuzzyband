#pragma once

/**
 * @file
 * @brief Threshold- and state-based @ref IInference for Phase 1.
 */

#include "IInference.h"

/**
 * @brief Rule-based pattern selection from structure state and BPM bands.
 *
 * Used when ML inference is disabled; see `ARCHITECTURE.md` for the feature pipeline.
 */
class RuleBasedInference final : public IInference
{
public:
    void prepare(double sampleRate) override;
    int selectPattern(const FeatureVector& features, int excludeIndex = -1) override;
    std::string getName() const override { return "RuleBasedInference"; }

private:
    double sampleRate = 44100.0;
};
