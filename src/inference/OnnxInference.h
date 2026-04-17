#pragma once

/**
 * @file
 * @brief ONNX Runtime–based IInference (Phase 2 stub).
 */

#include "IInference.h"

/**
 * @brief Phase 2 placeholder — compiled when enabled but not used in Phase 1 builds.
 *
 * When `MA_ENABLE_ONNX` is on, links against ONNX Runtime; otherwise may remain a stub.
 */
class OnnxInference final : public IInference
{
public:
    void prepare(double sampleRate) override;
    int selectPattern(const FeatureVector& features) override;
    std::string getName() const override;
};
