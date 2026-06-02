#include <catch2/catch_test_macros.hpp>

#if defined(MA_ENABLE_ONNX)

#include "BinaryData.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#include <onnxruntime_cxx_api.h>

namespace
{

Ort::Env& sharedEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MetalAccompanimentLatencyBench");
    return env;
}

Ort::SessionOptions productionSessionOptions()
{
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetInterOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    return opts;
}

std::unique_ptr<Ort::Session> loadSession(const char* data, int size)
{
    return std::make_unique<Ort::Session>(
        sharedEnv(),
        reinterpret_cast<const char*>(data),
        static_cast<size_t>(size),
        productionSessionOptions());
}

struct LatencyStats
{
    double minMs = 0.0;
    double meanMs = 0.0;
    double p99Ms = 0.0;
    double maxMs = 0.0;
};

LatencyStats measureRunLatencyMs(const std::function<void()>& runFn, int warmup, int iterations)
{
    for (int i = 0; i < warmup; ++i)
        runFn();

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iterations));
    for (int i = 0; i < iterations; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        runFn();
        const auto t1 = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        samples.push_back(ms);
    }

    std::sort(samples.begin(), samples.end());
    LatencyStats stats;
    stats.minMs = samples.front();
    stats.maxMs = samples.back();
    stats.meanMs = 0.0;
    for (double v : samples)
        stats.meanMs += v;
    stats.meanMs /= static_cast<double>(samples.size());
    const size_t p99Index = std::min(samples.size() - 1, static_cast<size_t>(samples.size() * 0.99));
    stats.p99Ms = samples[p99Index];
    return stats;
}

void logStats(const char* label, const LatencyStats& stats)
{
    std::printf(
        "%s latency ms: min=%.3f mean=%.3f p99=%.3f max=%.3f\n",
        label,
        stats.minMs,
        stats.meanMs,
        stats.p99Ms,
        stats.maxMs);
}

} // namespace

TEST_CASE("ONNX pattern model latency p99 under 5ms", "[onnx][latency][benchmark]")
{
    auto session = loadSession(BinaryData::accompaniment_model_onnx, BinaryData::accompaniment_model_onnxSize);
    std::array<float, 7> inputData{ 120.f, 0.2f, 9000.f, 0.2f, 0.f, 1.f, 0.f };
    std::array<int64_t, 2> shape{ 1, 7 };

    const auto stats = measureRunLatencyMs(
        [&]() {
            auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                mem, inputData.data(), inputData.size(), shape.data(), shape.size());
            const char* inputNames[] = { "X" };
            const char* outputNames[] = { "Y" };
            auto outputs = session->Run(Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
            REQUIRE_FALSE(outputs.empty());
        },
        100,
        10000);

    logStats("pattern", stats);
    REQUIRE(stats.p99Ms < 5.0);
}

TEST_CASE("ONNX structure model latency p99 under 5ms", "[onnx][latency][benchmark]")
{
    auto session = loadSession(BinaryData::structure_model_onnx, BinaryData::structure_model_onnxSize);
    std::array<float, 1 * 12 * 7> inputData{};
    for (size_t i = 0; i < inputData.size(); ++i)
        inputData[i] = 0.2f;
    inputData[0] = 120.f;
    std::array<int64_t, 3> shape{ 1, 12, 7 };

    const auto stats = measureRunLatencyMs(
        [&]() {
            auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                mem, inputData.data(), inputData.size(), shape.data(), shape.size());
            const char* inputNames[] = { "X_struct" };
            const char* outputNames[] = { "Y_struct" };
            auto outputs = session->Run(Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
            REQUIRE_FALSE(outputs.empty());
        },
        100,
        10000);

    logStats("structure", stats);
    REQUIRE(stats.p99Ms < 5.0);
}

TEST_CASE("ONNX bass model latency p99 under 5ms", "[onnx][latency][benchmark]")
{
    auto session = loadSession(BinaryData::bass_model_onnx, BinaryData::bass_model_onnxSize);
    std::array<float, 7> inputData{ 120.f, 0.2f, 9000.f, 0.2f, 1.f, 40.f, 0.8f };
    std::array<int64_t, 2> shape{ 1, 7 };

    const auto stats = measureRunLatencyMs(
        [&]() {
            auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                mem, inputData.data(), inputData.size(), shape.data(), shape.size());
            const char* inputNames[] = { "X_bass" };
            const char* outputNames[] = { "Y_bass" };
            auto outputs = session->Run(Ort::RunOptions{ nullptr }, inputNames, &inputTensor, 1, outputNames, 1);
            REQUIRE_FALSE(outputs.empty());
        },
        100,
        10000);

    logStats("bass", stats);
    REQUIRE(stats.p99Ms < 5.0);
}

#endif
