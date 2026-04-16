#include "OnnxInference.h"

void OnnxInference::prepare(double /*sampleRate*/)
{
    // TODO(Phase 2): load ONNX model from BinaryData, initialise ONNX Runtime session.
}

int OnnxInference::selectPattern(const FeatureVector& /*features*/)
{
    return 0;
}

std::string OnnxInference::getName() const
{
    return "OnnxInference (stub)";
}
