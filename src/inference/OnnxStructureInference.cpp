#include "OnnxStructureInference.h"

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <onnxruntime_cxx_api.h>
#endif

namespace
{
#if defined(MA_ENABLE_ONNX)
Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MetalAccompanimentStructure");
    return env;
}

void softmax4(const float* logits, float* probs)
{
    float m = std::max(std::max(logits[0], logits[1]), std::max(logits[2], logits[3]));
    float s = 0.f;
    for (int i = 0; i < 4; ++i)
    {
        probs[i] = std::exp(logits[i] - m);
        s += probs[i];
    }
    const float inv = (s > 0.f) ? (1.f / s) : 0.f;
    for (int i = 0; i < 4; ++i)
        probs[i] *= inv;
}

int argmax4(const float* probs)
{
    int k = 0;
    for (int i = 1; i < 4; ++i)
        if (probs[i] > probs[k])
            k = i;
    return k;
}

float secondLargest(const float* probs, int argmax)
{
    float best = -1.f;
    for (int i = 0; i < 4; ++i)
    {
        if (i == argmax)
            continue;
        best = std::max(best, probs[i]);
    }
    return best;
}

StructureState stateFromClassIndex(int k)
{
    switch (k)
    {
        case 0:
            return StructureState::SILENT;
        case 1:
            return StructureState::VERSE;
        case 2:
            return StructureState::CHORUS;
        case 3:
            return StructureState::BREAKDOWN;
        default:
            return StructureState::SILENT;
    }
}

void packSnapshot(const FeatureVector& fv, float* out7)
{
    out7[0] = fv.bpm;
    out7[1] = fv.rmsEnergy;
    out7[2] = fv.spectralCentroid;
    out7[3] = fv.highFreqFlux;
    out7[4] = static_cast<float>(static_cast<int>(fv.state));
    out7[5] = fv.pitchRootMidi;
    out7[6] = fv.pitchConfidence;
}
#endif
} // namespace

#if defined(MA_ENABLE_ONNX)

struct OnnxStructureInference::Impl
{
    std::unique_ptr<Ort::Session> session;
};

OnnxStructureInference::OnnxStructureInference() = default;

OnnxStructureInference::~OnnxStructureInference() = default;

bool OnnxStructureInference::tryLoadModel()
{
    impl = std::make_unique<Impl>();
    const char* data = BinaryData::structure_model_onnx;
    const int size = BinaryData::structure_model_onnxSize;
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

void OnnxStructureInference::prepare(double sr)
{
    sampleRate = (sr > 0.0) ? sr : 44100.0;
    reset();
}

void OnnxStructureInference::reset()
{
    hold.reset();
    metrics = {};
    lastSampleTimestamp = -1;
    window = {};
}

void OnnxStructureInference::pushSnapshot(const FeatureVector& fv)
{
    for (size_t i = 0; i < 11; ++i)
        window[i] = window[i + 1];
    window[11] = fv;
}

void OnnxStructureInference::runRuleFallback(const FeatureVector& fv, double dtSec)
{
    const StructureState desired = fv.state;
    const StructureState smoothed = hold.advance(desired, dtSec);
    metrics.ruleState = fv.state;
    metrics.mlValid = false;
    metrics.smoothedState = smoothed;
    metrics.softmaxTop1 = 0.f;
    metrics.margin = 0.f;
    metrics.agreeRule = (smoothed == fv.state);
}

void OnnxStructureInference::process(const FeatureVector& fv, double dtSec)
{
    metrics = {};
    metrics.ruleState = fv.state;

    const int64_t prevTs = lastSampleTimestamp;
    double useDt = 0.02;
    if (prevTs >= 0 && fv.sampleTimestamp >= prevTs)
        useDt = static_cast<double>(fv.sampleTimestamp - prevTs) / sampleRate;
    else if (dtSec > 0.0)
        useDt = dtSec;

    pushSnapshot(fv);

    if (impl == nullptr || impl->session == nullptr)
    {
        runRuleFallback(fv, useDt);
        lastSampleTimestamp = fv.sampleTimestamp;
        return;
    }

    std::array<float, 1 * 12 * 7> inputData{};
    float* dst = inputData.data();
    for (int t = 0; t < 12; ++t)
    {
        float row[7];
        packSnapshot(window[static_cast<size_t>(t)], row);
        for (int f = 0; f < 7; ++f)
            dst[t * 7 + f] = row[f];
    }

    std::array<int64_t, 3> shape{ 1, 12, 7 };

    try
    {
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            mem,
            inputData.data(),
            inputData.size(),
            shape.data(),
            shape.size());

        const char* inputNames[] = { "X_struct" };
        const char* outputNames[] = { "Y_struct" };
        auto outputs = impl->session->Run(
            Ort::RunOptions{ nullptr },
            inputNames,
            &inputTensor,
            1,
            outputNames,
            1);

        if (outputs.empty())
        {
            runRuleFallback(fv, useDt);
            lastSampleTimestamp = fv.sampleTimestamp;
            return;
        }

        const float* logits = outputs[0].GetTensorData<float>();
        float probs[4];
        softmax4(logits, probs);
        const int am = argmax4(probs);
        const float pMax = probs[am];
        const float pSecond = secondLargest(probs, am);
        const float margin = pMax - pSecond;

        const bool gated = (pMax < 0.6f) || (margin < 0.1f);
        if (gated)
        {
            const StructureState cur = hold.getCurrentState();
            const StructureState smoothed = hold.advance(cur, useDt);
            metrics.mlValid = false;
            metrics.smoothedState = smoothed;
            metrics.softmaxTop1 = pMax;
            metrics.margin = margin;
            metrics.agreeRule = (smoothed == fv.state);
            lastSampleTimestamp = fv.sampleTimestamp;
            return;
        }

        const StructureState desired = stateFromClassIndex(am);
        const StructureState smoothed = hold.advance(desired, useDt);
        metrics.mlValid = true;
        metrics.smoothedState = smoothed;
        metrics.softmaxTop1 = pMax;
        metrics.margin = margin;
        metrics.agreeRule = (smoothed == fv.state);
    }
    catch (...)
    {
        runRuleFallback(fv, useDt);
        lastSampleTimestamp = fv.sampleTimestamp;
        return;
    }

    lastSampleTimestamp = fv.sampleTimestamp;
}

std::string OnnxStructureInference::getName() const
{
    return "OnnxStructureInference";
}

#else // !MA_ENABLE_ONNX

struct OnnxStructureInference::Impl
{
};

OnnxStructureInference::OnnxStructureInference() = default;
OnnxStructureInference::~OnnxStructureInference() = default;

bool OnnxStructureInference::tryLoadModel()
{
    return false;
}

void OnnxStructureInference::prepare(double sr)
{
    sampleRate = (sr > 0.0) ? sr : 44100.0;
    reset();
}

void OnnxStructureInference::reset()
{
    hold.reset();
    metrics = {};
    lastSampleTimestamp = -1;
    window = {};
}

void OnnxStructureInference::pushSnapshot(const FeatureVector& fv)
{
    for (size_t i = 0; i < 11; ++i)
        window[i] = window[i + 1];
    window[11] = fv;
}

void OnnxStructureInference::runRuleFallback(const FeatureVector& fv, double dtSec)
{
    const StructureState desired = fv.state;
    const StructureState smoothed = hold.advance(desired, dtSec);
    metrics.ruleState = fv.state;
    metrics.mlValid = false;
    metrics.smoothedState = smoothed;
    metrics.softmaxTop1 = 1.0f;
    metrics.margin = 1.0f;
    metrics.agreeRule = (smoothed == fv.state);
}

void OnnxStructureInference::process(const FeatureVector& fv, double dtSec)
{
    metrics = {};
    runRuleFallback(fv, dtSec);
}

std::string OnnxStructureInference::getName() const
{
    return "OnnxStructureInference (disabled)";
}

#endif
