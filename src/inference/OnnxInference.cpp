#include "OnnxInference.h"
#include <string>

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include "PolicyPatternMapper.h"
#include <algorithm>
#include <array>
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
        return false;

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
    }
    catch (...)
    {
        impl.reset();
        return false;
    }

    return true;
}

void OnnxInference::prepare(double /*sampleRate*/)
{
    // Model has no sample-rate-dependent weights; session is ready after tryLoadModel().
}

int OnnxInference::selectPattern(const FeatureVector& f)
{
    if (impl == nullptr || impl->session == nullptr)
        return 0;

    std::array<float, 5> inputData = {
        f.bpm,
        f.rmsEnergy,
        f.spectralCentroid,
        f.highFreqFlux,
        static_cast<float>(static_cast<int>(f.state)),
    };
    std::array<int64_t, 2> shape{ 1, 5 };

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
            return 0;

        const int64_t* out = outputs[0].GetTensorData<int64_t>();
        const int64_t raw = out[0];
        const int clamped = static_cast<int>(std::clamp(raw, static_cast<int64_t>(0), static_cast<int64_t>(6)));
        return PolicyPatternMapper::applyPatternPolicy(clamped, f);
    }
    catch (...)
    {
        return 0;
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

int OnnxInference::selectPattern(const FeatureVector& /*features*/)
{
    return 0;
}

std::string OnnxInference::getName() const
{
    return "OnnxInference (disabled)";
}

#endif
