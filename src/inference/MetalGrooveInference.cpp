#include "MetalGrooveInference.h"

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include "pattern_embeddings.h"
#include "pattern_rules.h"
#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>
#include <onnxruntime_cxx_api.h>
#endif

#if defined(MA_ENABLE_ONNX)

namespace
{

Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MetalAccompanimentGroove");
    return env;
}

/** Cosine similarity between two 128-dim vectors. Returns value in [-1, 1]. */
inline float cosineSimilarity(const float* a, const float* b, int dim)
{
    float dot = 0.0f, normA = 0.0f, normB = 0.0f;
    for (int i = 0; i < dim; ++i)
    {
        dot += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    float denom = std::sqrt(normA) * std::sqrt(normB);
    return (denom > 1e-8f) ? (dot / denom) : 0.0f;
}

} // namespace

struct MetalGrooveInference::Impl
{
    std::unique_ptr<Ort::Session> session;

    // Cached memory info
    Ort::MemoryInfo memInfo{ Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault) };

    // Input/output names (discovered at load time)
    std::string inputName;
    std::string outputNameBottleneck;
    std::string outputNameStyle;
    std::string outputNameGrooveEmb;   // may be unused but available
};

MetalGrooveInference::MetalGrooveInference() = default;
MetalGrooveInference::~MetalGrooveInference() = default;

bool MetalGrooveInference::tryLoadModel()
{
    impl = std::make_unique<Impl>();

    const char* data = BinaryData::metal_groove_onnx;
    const int size = BinaryData::metal_groove_onnxSize;
    if (data == nullptr || size <= 0)
    {
        loadErrorCount.fetch_add(1, std::memory_order_relaxed);
        impl.reset();
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
            reinterpret_cast<const void*>(data),
            static_cast<size_t>(size),
            opts);

        // ── Validate inputs ──────────────────────────────────────────
        const size_t numInputs = impl->session->GetInputCount();
        if (numInputs != 1)
        {
            loadErrorCount.fetch_add(1, std::memory_order_relaxed);
            impl.reset();
            return false;
        }

        // Input/output names are known from model export
        impl->inputName = "mel";
        impl->outputNameBottleneck = "bottleneck";
        impl->outputNameStyle = "style_logits";
        // impl->outputNameGrooveEmb left empty — unused for centroid lookup

        // Validate input shape
        const auto inputTypeInfo = impl->session->GetInputTypeInfo(0);
        const auto& inputShapeInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto inputShape = inputShapeInfo.GetShape();
        const auto inputElemType = inputShapeInfo.GetElementType();

        // Expect: [batch, 1, 64, 32] — batch dim may be symbolic (-1)
        bool inputOk = (inputElemType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            && (inputShape.size() == 4)
            && (inputShape[1] == 1 || inputShape[1] == -1)
            && (inputShape[2] == 64 || inputShape[2] == -1)
            && (inputShape[3] == 32 || inputShape[3] == -1);

        if (!inputOk)
        {
            loadErrorCount.fetch_add(1, std::memory_order_relaxed);
            impl.reset();
            return false;
        }

        // Validate output count
        const size_t numOutputs = impl->session->GetOutputCount();
        if (numOutputs < 2)
        {
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

void MetalGrooveInference::prepare(double /*sampleRate*/)
{
    // No sample-rate-dependent state.
}

int MetalGrooveInference::selectPattern(const FeatureVector& f, int excludeIndex)
{
    // Scalar-feature fallback when mel data not available.
    const int fallback = PatternRules::rulePatternForState(f);
    return PatternRules::applyExclusion(fallback, excludeIndex, f.state, fallback);
}

int MetalGrooveInference::selectPatternFromMel(const float* melData, int excludeIndex)
{
    if (impl == nullptr || impl->session == nullptr)
        return 0;

    // ── Pack mel data as [1, 1, 64, 32] tensor ───────────────────────
    // melData is 2048 floats row-major [64 bands × 32 frames]
    // ONNX expects shape [batch=1, channels=1, height=64, width=32]
    std::array<int64_t, 4> inputShape{ 1, 1, 64, 32 };

    try
    {
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            impl->memInfo,
            const_cast<float*>(melData),   // ONNX won't modify input
            2048,
            inputShape.data(),
            inputShape.size());

        const char* inputNames[] = { impl->inputName.c_str() };
        const char* outputNames[] = {
            impl->outputNameBottleneck.c_str(),
            impl->outputNameStyle.c_str(),
        };

        auto outputs = impl->session->Run(
            Ort::RunOptions{ nullptr },
            inputNames,
            &inputTensor,
            1,
            outputNames,
            2);

        if (outputs.size() < 2)
        {
            runErrorCount.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        // ── Get bottleneck [1, 128] ──────────────────────────────────
        const float* bottleneck = outputs[0].GetTensorData<float>();
        const auto& bottleneckShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const int bottleneckDim = (bottleneckShape.size() >= 2)
            ? static_cast<int>(bottleneckShape[1]) : 128;

        // ── Cosine similarity nearest-neighbor lookup ─────────────────
        float bestSim = -2.0f;
        int bestIdx = 0;

        for (int p = 0; p < PatternEmbeddings::kNumPatterns; ++p)
        {
            if (p == excludeIndex) continue;

            float sim = cosineSimilarity(
                bottleneck,
                PatternEmbeddings::kCentroids[p].data(),
                PatternEmbeddings::kEmbeddingDim);

            if (sim > bestSim)
            {
                bestSim = sim;
                bestIdx = p;
            }
        }

        return bestIdx;
    }
    catch (...)
    {
        runErrorCount.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
}

int MetalGrooveInference::classifyStyle(const float* melData)
{
    if (impl == nullptr || impl->session == nullptr)
        return 4;  // default: silence

    std::array<int64_t, 4> inputShape{ 1, 1, 64, 32 };

    try
    {
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            impl->memInfo,
            const_cast<float*>(melData),
            2048,
            inputShape.data(),
            inputShape.size());

        const char* inputNames[] = { impl->inputName.c_str() };
        const char* outputNames[] = { impl->outputNameStyle.c_str() };

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
            return 4;
        }

        const float* logits = outputs[0].GetTensorData<float>();
        const auto& shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const int nClasses = (shape.size() >= 2) ? static_cast<int>(shape[1]) : 5;

        int bestClass = 0;
        float bestVal = logits[0];
        for (int i = 1; i < nClasses; ++i)
        {
            if (logits[i] > bestVal)
            {
                bestVal = logits[i];
                bestClass = i;
            }
        }

        return bestClass;
    }
    catch (...)
    {
        runErrorCount.fetch_add(1, std::memory_order_relaxed);
        return 4;
    }
}

uint64_t MetalGrooveInference::getLoadErrorCount() const noexcept
{
    return loadErrorCount.load(std::memory_order_relaxed);
}

uint64_t MetalGrooveInference::getRunErrorCount() const noexcept
{
    return runErrorCount.load(std::memory_order_relaxed);
}

std::string MetalGrooveInference::getName() const
{
    return "MetalGrooveInference";
}

#else // !MA_ENABLE_ONNX

struct MetalGrooveInference::Impl {};

MetalGrooveInference::MetalGrooveInference() = default;
MetalGrooveInference::~MetalGrooveInference() = default;

bool MetalGrooveInference::tryLoadModel() { return false; }
void MetalGrooveInference::prepare(double) {}
int MetalGrooveInference::selectPattern(const FeatureVector&, int) { return 0; }
int MetalGrooveInference::selectPatternFromMel(const float*, int) { return 0; }
int MetalGrooveInference::classifyStyle(const float*) { return 4; }
uint64_t MetalGrooveInference::getLoadErrorCount() const noexcept { return 0; }
uint64_t MetalGrooveInference::getRunErrorCount() const noexcept { return 0; }
std::string MetalGrooveInference::getName() const { return "MetalGrooveInference (disabled)"; }

#endif
