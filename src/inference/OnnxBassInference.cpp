#include "OnnxBassInference.h"

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <onnxruntime_cxx_api.h>
#endif

#if defined(MA_ENABLE_ONNX)

namespace
{
Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MetalAccompanimentBass");
    return env;
}
} // namespace

struct OnnxBassInference::Impl
{
    std::unique_ptr<Ort::Session> session;
};

OnnxBassInference::OnnxBassInference() = default;

OnnxBassInference::~OnnxBassInference() = default;

bool OnnxBassInference::tryLoadModel()
{
    impl = std::make_unique<Impl>();
    const char* data = BinaryData::bass_model_onnx;
    const int size = BinaryData::bass_model_onnxSize;
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

        // Strict contract validation: D-23-01 / D-23-03
        const auto inputTypeInfo = impl->session->GetInputTypeInfo(0);
        const auto& inputShapeInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto inputShape = inputShapeInfo.GetShape();
        const auto inputElemType = inputShapeInfo.GetElementType();

        if (inputElemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            inputShape.size() != 2 || inputShape[0] != 1 || inputShape[1] != 7)
        {
            jassert(false); // D-23-01: contract mismatch on X_bass [1,7]
            impl.reset();
            return false;
        }

        const auto outputTypeInfo = impl->session->GetOutputTypeInfo(0);
        const auto& outputShapeInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto outputShape = outputShapeInfo.GetShape();
        const auto outputElemType = outputShapeInfo.GetElementType();

        if (outputElemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            outputShape.size() != 2 || outputShape[0] != 1 || outputShape[1] != 32)
        {
            jassert(false); // D-23-01: contract mismatch on Y_bass [1,32]
            impl.reset();
            return false;
        }
    }
    catch (const Ort::Exception& e)
    {
        std::fprintf(stderr, "[OnnxBassInference] ORT load failed: %s\n", e.what());
        impl.reset();
        return false;
    }
    catch (...)
    {
        impl.reset();
        return false;
    }

    return true;
}

void OnnxBassInference::prepare(double /*sampleRate*/)
{
    reset();
}

void OnnxBassInference::reset()
{
    last = {};
}

void OnnxBassInference::propose(const FeatureVector& f)
{
    last = {};

    if (impl == nullptr || impl->session == nullptr)
        return;

    std::array<float, 7> inputData = {
        f.bpm,
        f.rmsEnergy,
        f.spectralCentroid,
        f.highFreqFlux,
        static_cast<float>(static_cast<int>(f.state)),
        f.pitchRootMidi,
        f.pitchConfidence,
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

        const char* inputNames[] = { "X_bass" };
        const char* outputNames[] = { "Y_bass" };
        auto outputs = impl->session->Run(
            Ort::RunOptions{ nullptr },
            inputNames,
            &inputTensor,
            1,
            outputNames,
            1);

        if (outputs.empty())
            return;

        const float* out = outputs[0].GetTensorData<float>();

        // Decode [1,32] interleaved piano-roll: 16 × [pitch_offset_n, velocity_n]
        bool allFinite = true;
        for (int i = 0; i < 32; ++i)
        {
            if (!std::isfinite(out[i]))
            {
                allFinite = false;
                break;
            }
        }

        if (!allFinite)
            return;

        for (int step = 0; step < BassOnnxProposal::kSteps; ++step)
        {
            last.pitchOffset[step] = std::clamp(out[step * 2], -12.0f, 12.0f);
            last.velocity[step]   = std::clamp(out[step * 2 + 1], 0.0f, 127.0f);
        }
        last.valid = true;
    }
    catch (const Ort::Exception& e)
    {
        std::fprintf(stderr, "[OnnxBassInference] ORT Run failed: %s\n", e.what());
        last = {};
    }
    catch (...)
    {
        last = {};
    }
}

#else // !MA_ENABLE_ONNX

OnnxBassInference::OnnxBassInference() = default;
OnnxBassInference::~OnnxBassInference() = default;

bool OnnxBassInference::tryLoadModel()
{
    return false;
}

void OnnxBassInference::prepare(double /*sampleRate*/)
{
    reset();
}

void OnnxBassInference::reset()
{
    last = {};
}

void OnnxBassInference::propose(const FeatureVector& /*fv*/)
{
    last = {};
}

#endif
