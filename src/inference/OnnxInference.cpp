#include "OnnxInference.h"
#include <string>

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include "pattern_rules.h"
#include <algorithm>
#include <array>
#include <juce_core/juce_core.h>
#include <onnxruntime_cxx_api.h>
#endif

#if defined(MA_ENABLE_ONNX)

namespace
{
Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MetalAccompaniment");
    return env;
}
} // namespace

struct OnnxInference::Impl
{
    std::unique_ptr<Ort::Session> session;
};

OnnxInference::OnnxInference() = default;

OnnxInference::~OnnxInference() = default;

bool OnnxInference::tryLoadModel()
{
    impl = std::make_unique<Impl>();
    const char* data = BinaryData::accompaniment_model_onnx;
    const int size = BinaryData::accompaniment_model_onnxSize;
    if (data == nullptr || size <= 0)
    {
        loadErrorCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    try
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetInterOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        impl->session = std::make_unique<Ort::Session>(
            ortEnv(),
            reinterpret_cast<const char*>(data),
            static_cast<size_t>(size),
            opts);

        // Strict contract validation: D-23-01 / D-23-03
        const auto inputTypeInfo = impl->session->GetInputTypeInfo(0);
        const auto& inputShapeInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto inputShape = inputShapeInfo.GetShape();
        const auto inputElemType = inputShapeInfo.GetElementType();

        if (inputElemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            inputShape.size() != 2 || inputShape[0] != 1 || inputShape[1] != 7)
        {
            jassert(false); // D-23-01: contract mismatch — no silent fallback
            loadErrorCount.fetch_add(1, std::memory_order_relaxed);
            impl.reset();
            return false;
        }

        const auto outputTypeInfo = impl->session->GetOutputTypeInfo(0);
        const auto& outputShapeInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto outputShape = outputShapeInfo.GetShape();
        const auto outputElemType = outputShapeInfo.GetElementType();

        if (outputElemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64 ||
            outputShape.size() != 1 || outputShape[0] != 1)
        {
            jassert(false); // D-23-01: contract mismatch — no silent fallback
            loadErrorCount.fetch_add(1, std::memory_order_relaxed);
            impl.reset();
            return false;
        }
    }
    catch (const Ort::Exception&)
    {
        loadErrorCount.fetch_add(1, std::memory_order_relaxed);
        impl.reset();
        return false;
    }
    catch (...)
    {
        loadErrorCount.fetch_add(1, std::memory_order_relaxed);
        impl.reset();
        return false;
    }

    return true;
}

void OnnxInference::prepare(double /*sampleRate*/)
{
    // Model has no sample-rate-dependent weights; session is ready after tryLoadModel().
}

int OnnxInference::selectPattern(const FeatureVector& f, int excludeIndex)
{
    if (impl == nullptr || impl->session == nullptr)
        return 0;

    // Pack [1,7] one-hot per docs/ONNX_IO.md: indices 4-6 = SILENT/SOFT/LOUD
    float stateSILENT = 0.0f;
    float stateSOFT = 0.0f;
    float stateLOUD = 0.0f;
    switch (f.state)
    {
        case StructureState::SILENT: stateSILENT = 1.0f; break;
        case StructureState::SOFT:   stateSOFT   = 1.0f; break;
        case StructureState::LOUD:   stateLOUD   = 1.0f; break;
    }

    std::array<float, 7> inputData = {
        PatternRules::adjustedBpm(f),
        f.rmsEnergy,
        f.spectralCentroid,
        f.highFreqFlux,
        stateSILENT,
        stateSOFT,
        stateLOUD,
    };
    std::array<int64_t, 2> shape{ 1, 7 };

    try
    {
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            mem,
            inputData.data(),
            inputData.size(),
            shape.data(),
            shape.size());

        const char* inputNames[] = { "X" };
        const char* outputNames[] = { "Y" };
        auto outputs = impl->session->Run(
            Ort::RunOptions{ nullptr },
            inputNames,
            &inputTensor,
            1,
            outputNames,
            1);

        if (outputs.empty())
        {
            runErrorCount.fetch_add(1, std::memory_order_relaxed);
            const int fallback = PatternRules::rulePatternForState(f);
            return PatternRules::applyExclusion(fallback, excludeIndex, f.state, fallback);
        }

        const int64_t* out = outputs[0].GetTensorData<int64_t>();
        const int64_t raw = out[0];
        const int clamped = static_cast<int>(std::clamp(raw, static_cast<int64_t>(0), static_cast<int64_t>(6)));
        const int result = PatternRules::isOnnxPatternAcceptable(clamped, f)
            ? clamped
            : PatternRules::rulePatternForState(f);

        const int fallback = PatternRules::rulePatternForState(f);
        return PatternRules::applyExclusion(result, excludeIndex, f.state, fallback);
    }
    catch (...)
    {
        runErrorCount.fetch_add(1, std::memory_order_relaxed);
        const int fallback = PatternRules::rulePatternForState(f);
        return PatternRules::applyExclusion(fallback, excludeIndex, f.state, fallback);
    }
}

std::string OnnxInference::getName() const
{
    return "OnnxInference";
}

#else // !MA_ENABLE_ONNX

struct OnnxInference::Impl
{
};

OnnxInference::OnnxInference() = default;
OnnxInference::~OnnxInference() = default;

bool OnnxInference::tryLoadModel()
{
    return false;
}

void OnnxInference::prepare(double /*sampleRate*/) {}

int OnnxInference::selectPattern(const FeatureVector& /*features*/, int /*excludeIndex*/)
{
    return 0;
}

std::string OnnxInference::getName() const
{
    return "OnnxInference (disabled)";
}

#endif
