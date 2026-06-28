#include "PlayingStyleClassifier.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#if defined(MA_ENABLE_ONNX)
#include "BinaryData.h"
#include <juce_core/juce_core.h>
#include <onnxruntime_cxx_api.h>

namespace
{
Ort::Env& classifierEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MetalAccompanimentClassifier");
    return env;
}
} // namespace

struct PlayingStyleClassifier::Impl
{
    std::unique_ptr<Ort::Session> session;
};

PlayingStyleClassifier::PlayingStyleClassifier() = default;
PlayingStyleClassifier::~PlayingStyleClassifier() = default;

bool PlayingStyleClassifier::tryLoadModel()
{
    impl = std::make_unique<Impl>();

    const char* data = BinaryData::classifier_onnx;
    const int size = BinaryData::classifier_onnxSize;
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
            classifierEnv(),
            reinterpret_cast<const char*>(data),
            static_cast<size_t>(size),
            opts);

        // ── Validate input contract: (1, 1, 64, 32) float32 (dim 0 may be -1 for dynamic batch) ──
        const auto inputTypeInfo = impl->session->GetInputTypeInfo(0);
        const auto& inputShapeInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto inputShape = inputShapeInfo.GetShape();
        const auto inputElemType = inputShapeInfo.GetElementType();

        const bool inputBatchOk = (inputShape[0] == 1 || inputShape[0] == -1);
        if (inputElemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            inputShape.size() != 4 ||
            !inputBatchOk || inputShape[1] != 1 ||
            inputShape[2] != kMelBands || inputShape[3] != kTimeFrames)
        {
            jassertfalse; // contract mismatch
            loadErrorCount.fetch_add(1, std::memory_order_relaxed);
            impl.reset();
            return false;
        }

        // ── Validate output contract: (1, 5) float32 (dim 0 may be -1) ──
        const auto outputTypeInfo = impl->session->GetOutputTypeInfo(0);
        const auto& outputShapeInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
        const auto outputShape = outputShapeInfo.GetShape();
        const auto outputElemType = outputShapeInfo.GetElementType();

        const bool outputBatchOk = (outputShape[0] == 1 || outputShape[0] == -1);
        if (outputElemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            outputShape.size() != 2 ||
            !outputBatchOk || outputShape[1] != kNumClasses)
        {
            jassertfalse; // contract mismatch
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

    loaded.store(true, std::memory_order_release);
    return true;
}

PlayingStyleResult PlayingStyleClassifier::classify(const float* melData) noexcept
{
    PlayingStyleResult result;
    result.classIndex = 0;
    result.confidence = 0.0f;
    result.className = classNameForIndex(0);

    if (impl == nullptr || impl->session == nullptr)
    {
        runErrorCount.fetch_add(1, std::memory_order_relaxed);
        return result;
    }

    // Pack input: (1, 1, 64, 32) = 2048 floats, row-major [melBand][timeFrame]
    std::array<float, kInputSize> inputData;
    std::copy(melData, melData + kInputSize, inputData.begin());

    std::array<int64_t, 4> inputShape = { 1, 1, kMelBands, kTimeFrames };

    try
    {
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            mem,
            inputData.data(),
            inputData.size(),
            inputShape.data(),
            inputShape.size());

        const char* inputNames[] = { "input" };
        const char* outputNames[] = { "logits" };
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
            return result;
        }

        // Softmax over logits for confidence
        const float* logits = outputs[0].GetTensorData<float>();

        // ── DEBUG: log first 5 classifications ────────────────────
        {
            static int debugCount = 0;
            if (debugCount < 5)
            {
                DBG("PlayingStyleClassifier::classify #" << debugCount
                    << " logits=[" << logits[0] << "," << logits[1] << ","
                    << logits[2] << "," << logits[3] << "," << logits[4] << "]"
                    << " mel[0..3]=[" << melData[0] << "," << melData[1]
                    << "," << melData[2] << "," << melData[3] << "]");
                ++debugCount;
            }
        }
        // ── END DEBUG ──────────────────────────────────────────────

        std::array<float, kNumClasses> probs;
        float maxLogit = logits[0];
        for (int i = 1; i < kNumClasses; ++i)
            maxLogit = std::max(maxLogit, logits[i]);

        float sum = 0.0f;
        for (int i = 0; i < kNumClasses; ++i)
        {
            probs[static_cast<size_t>(i)] = std::exp(logits[i] - maxLogit);
            sum += probs[static_cast<size_t>(i)];
        }

        int bestClass = 0;
        float bestProb = 0.0f;
        for (int i = 0; i < kNumClasses; ++i)
        {
            probs[static_cast<size_t>(i)] /= sum;
            if (probs[static_cast<size_t>(i)] > bestProb)
            {
                bestProb = probs[static_cast<size_t>(i)];
                bestClass = i;
            }
        }

        result.classIndex = bestClass;
        result.confidence = bestProb;
        result.className = classNameForIndex(bestClass);
    }
    catch (...)
    {
        runErrorCount.fetch_add(1, std::memory_order_relaxed);
    }

    return result;
}

std::string PlayingStyleClassifier::classNameForIndex(int index)
{
    switch (index)
    {
        case 0: return "Palm Mute";
        case 1: return "Open Chord";
        case 2: return "Single Note";
        case 3: return "Sustain";
        case 4: return "Silence";
        default: return "Unknown";
    }
}

uint64_t PlayingStyleClassifier::getLoadErrorCount() const noexcept
{
    return loadErrorCount.load(std::memory_order_relaxed);
}

uint64_t PlayingStyleClassifier::getRunErrorCount() const noexcept
{
    return runErrorCount.load(std::memory_order_relaxed);
}

#else // !MA_ENABLE_ONNX

struct PlayingStyleClassifier::Impl {};

PlayingStyleClassifier::PlayingStyleClassifier() = default;
PlayingStyleClassifier::~PlayingStyleClassifier() = default;

bool PlayingStyleClassifier::tryLoadModel() { return false; }

PlayingStyleResult PlayingStyleClassifier::classify(const float*) noexcept
{
    PlayingStyleResult result;
    result.classIndex = 0;
    result.confidence = 0.0f;
    result.className = classNameForIndex(0);
    return result;
}

std::string PlayingStyleClassifier::classNameForIndex(int index)
{
    (void)index;
    return "ONNX disabled";
}

uint64_t PlayingStyleClassifier::getLoadErrorCount() const noexcept { return 0; }
uint64_t PlayingStyleClassifier::getRunErrorCount() const noexcept { return 0; }

#endif
