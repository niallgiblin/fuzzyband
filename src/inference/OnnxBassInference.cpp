#include "OnnxBassInference.h"

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include <array>
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
    if (impl == nullptr || impl->session == nullptr)
    {
        last = {};
        return;
    }

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
        {
            last = {};
            return;
        }

        const float* out = outputs[0].GetTensorData<float>();
        last.confidence = out[0];
        last.rootMidi = out[1];
        last.durationBeats = out[2];
        last.margin = out[3];
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
