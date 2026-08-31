#include "MetalGrooveInference.h"

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include "pattern_embeddings.h"
#include "pattern_rules.h"
#include <algorithm>
#include <array>
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

/** Run a style session (mel → style_logits) on a mel window and return the
 *  argmax class 0-4 (palm_mute, open_chord, single_note, sustain, silence).
 *  Shared by the end-to-end style CNN (style_cnn.onnx) and the metal_groove
 *  style_logits head. Returns 4 (silence) on any failure. */
inline int argmaxStyleIndex(Ort::Session& session, const std::string& inputName,
    const std::string& outputName, const float* melData,
    std::atomic<uint64_t>& runError)
{
    std::array<int64_t, 4> shape{ 1, 1, 64, 32 };
    try
    {
        Ort::MemoryInfo memInfo{ Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault) };
        Ort::Value input = Ort::Value::CreateTensor<float>(
            memInfo, const_cast<float*>(melData), 2048, shape.data(), shape.size());

        const char* inNames[] = { inputName.c_str() };
        const char* outNames[] = { outputName.c_str() };
        auto outputs = session.Run(Ort::RunOptions{ nullptr }, inNames, &input, 1, outNames, 1);

        if (outputs.empty())
        {
            runError.fetch_add(1, std::memory_order_relaxed);
            return 4;
        }

        const float* logits = outputs[0].GetTensorData<float>();
        const auto& s = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const int nClasses = (s.size() >= 2) ? static_cast<int>(s[1]) : 5;

        int best = 0;
        float bestVal = logits[0];
        for (int i = 1; i < nClasses; ++i)
        {
            if (logits[i] > bestVal)
            {
                bestVal = logits[i];
                best = i;
            }
        }
        return best;
    }
    catch (...)
    {
        runError.fetch_add(1, std::memory_order_relaxed);
        return 4;
    }
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

    // Optional end-to-end style classifier (PlayingStyleCNN → style_cnn.onnx).
    // Preferred by classifyStyle(); falls back to the metal_groove style_logits
    // head when this is absent (bundled model unavailable / older build).
    std::unique_ptr<Ort::Session> styleSession;
    std::string styleInputName;
    std::string styleOutputName;
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

    // ── Optional end-to-end style classifier (PlayingStyleCNN) ───────────────
    // Preferred by classifyStyle(). If the style model isn't bundled/loadable we
    // keep the metal_groove style_logits head as a fallback — never fail the
    // whole model load because the (secondary) style model is missing.
    {
        const char* sdata = BinaryData::style_cnn_onnx;
        const int ssize = BinaryData::style_cnn_onnxSize;
        if (sdata != nullptr && ssize > 0)
        {
            try
            {
                Ort::SessionOptions sOpts;
                sOpts.SetIntraOpNumThreads(1);
                sOpts.SetInterOpNumThreads(1);
                sOpts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

                impl->styleSession = std::make_unique<Ort::Session>(
                    ortEnv(),
                    reinterpret_cast<const void*>(sdata),
                    static_cast<size_t>(ssize),
                    sOpts);

                // Input/output names are fixed by the style CNN export.
                impl->styleInputName = "mel";
                impl->styleOutputName = "style_logits";
            }
            catch (...)
            {
                impl->styleSession.reset();
            }
        }
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
    return selectPatternFromMel(melData, excludeIndex, -1);
}

int MetalGrooveInference::selectPatternFromMel(const float* melData, int excludeIndex, int seed)
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

        // ── Similarity to every pattern centroid ─────────────────────
        // (deterministic, allocation-free; store in a fixed local array)
        std::array<float, PatternEmbeddings::kNumPatterns> sim{};
        for (int p = 0; p < PatternEmbeddings::kNumPatterns; ++p)
        {
            if (p == excludeIndex)
            {
                sim[static_cast<size_t>(p)] = -2.0f;
                continue;
            }
            sim[static_cast<size_t>(p)] = cosineSimilarity(
                bottleneck,
                PatternEmbeddings::kCentroids[p].data(),
                PatternEmbeddings::kEmbeddingDim);
        }

        // Default (seed < 0): deterministic argmax — the single best fit.
        if (seed < 0)
        {
            float bestSim = -2.0f;
            int bestIdx = 0;
            for (int p = 0; p < PatternEmbeddings::kNumPatterns; ++p)
            {
                if (sim[static_cast<size_t>(p)] > bestSim)
                {
                    bestSim = sim[static_cast<size_t>(p)];
                    bestIdx = p;
                }
            }
            return bestIdx;
        }

        // ── Variety: weighted draw over the top-K nearest grooves ─────
        // The best fit stays dominant, but a neighbouring groove can win, so the
        // drums vary bar-to-bar while staying stylistically consistent (the ML
        // "alters slightly"). Temperature scales how tightly we stick to the best.
        constexpr int K = 3;
        constexpr float kTemp = 9.0f;   // higher = stick closer to the best fit
        std::array<int, K> topIdx{};
        std::array<float, K> topSim{};
        std::array<float, K> weight{};
        for (int i = 0; i < K; ++i)
        {
            topIdx[static_cast<size_t>(i)] = -1;
            topSim[static_cast<size_t>(i)] = -2.0f;
        }

        for (int p = 0; p < PatternEmbeddings::kNumPatterns; ++p)
        {
            const float s = sim[static_cast<size_t>(p)];
            for (int i = 0; i < K; ++i)
            {
                if (s > topSim[static_cast<size_t>(i)])
                {
                    for (int j = K - 1; j > i; --j)
                    {
                        topIdx[static_cast<size_t>(j)] = topIdx[static_cast<size_t>(j - 1)];
                        topSim[static_cast<size_t>(j)] = topSim[static_cast<size_t>(j - 1)];
                    }
                    topIdx[static_cast<size_t>(i)] = p;
                    topSim[static_cast<size_t>(i)] = s;
                    break;
                }
            }
        }

        if (topIdx[0] < 0)
            return 0;

        // Softmax-weight the top-K around the best similarity; normalise and
        // draw a single deterministic value from a hash of the bar seed.
        float wsum = 0.0f;
        for (int i = 0; i < K; ++i)
        {
            const float w = std::exp((topSim[static_cast<size_t>(i)] - topSim[0]) * kTemp);
            weight[static_cast<size_t>(i)] = w;
            wsum += w;
        }

        // Deterministic [0,1) drawn from the seed (SplitMix32-style).
        unsigned h = static_cast<unsigned>(seed) * 0x9E3779B1u;
        h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
        const float r = static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);

        float acc = 0.0f;
        for (int i = 0; i < K; ++i)
        {
            acc += weight[static_cast<size_t>(i)] / (wsum > 1e-8f ? wsum : 1.0f);
            if (r <= acc)
                return topIdx[static_cast<size_t>(i)];
        }
        return topIdx[0];
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

    // Preferred: the end-to-end PlayingStyleCNN (style_cnn.onnx) when bundled.
    // This classifies directly on the mel windows and generalises far better
    // than the former Linear(128→5) head on the groove-classification bottleneck.
    if (impl->styleSession != nullptr)
        return argmaxStyleIndex(*impl->styleSession, impl->styleInputName,
                                impl->styleOutputName, melData, runErrorCount);

    // Fallback: the metal_groove style_logits head (older builds / no style model).
    return argmaxStyleIndex(*impl->session, impl->inputName,
                            impl->outputNameStyle, melData, runErrorCount);
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
int MetalGrooveInference::selectPatternFromMel(const float*, int, int) { return 0; }
int MetalGrooveInference::classifyStyle(const float*) { return 4; }
uint64_t MetalGrooveInference::getLoadErrorCount() const noexcept { return 0; }
uint64_t MetalGrooveInference::getRunErrorCount() const noexcept { return 0; }
std::string MetalGrooveInference::getName() const { return "MetalGrooveInference (disabled)"; }

#endif
