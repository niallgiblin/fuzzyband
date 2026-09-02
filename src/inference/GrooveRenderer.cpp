#include "GrooveRenderer.h"
#include "midi/GrooveTemplate.h"

#include <algorithm>
#include <cmath>

#if defined(MA_ENABLE_ONNX) && defined(MA_BUNDLE_GROOVE_RENDERER)
#include "BinaryData.h"
#include <onnxruntime_cxx_api.h>
#include <array>
#endif

namespace
{
// SplitMix32 avalanche — deterministic per-bar draws (same construction as
// PatternRules::hashMix and the Tier-0 ornamentation barHash).
inline unsigned barHash(unsigned a, unsigned b) noexcept
{
    unsigned h = a * 0x9E3779B1u + b;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

// Deterministic [0,1) from a hash word (top 24 bits).
inline float unitFromHash(unsigned h) noexcept
{
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

// Deterministic N(0,1) draw (Box–Muller) from two hash seeds.
inline float gaussianNoise(unsigned a, unsigned b) noexcept
{
    const float u1 = std::max(1.0e-6f, unitFromHash(barHash(a, b)));
    const float u2 = unitFromHash(barHash(a, b ^ 0x9E3779B1u));
    return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * 3.14159265358979323846f * u2);
}
} // namespace

// ── Pure helpers (always compiled) ──────────────────────────────────────────

void GrooveRenderer::buildScoreGrid(const MidiPattern& pattern, ScoreGrid& out) noexcept
{
    for (auto& row : out)
        row.fill(0.0f);

    for (const auto& ev : pattern.drumEvents)
    {
        const int voice = GrooveGridUtil::voiceForNote(ev.note);
        if (voice < 0)
            continue;
        const float beatInBar = std::fmod(ev.beatOffset, 4.0f);
        const int step = GrooveGridUtil::stepForBeatInBar(beatInBar);
        out[static_cast<size_t>(voice)][static_cast<size_t>(step)] = 1.0f;
    }
}

void GrooveRenderer::buildCondition(const FeatureVector& f, int genreId, int64_t barNumber,
                                    int styleIndex, float out[GrooveGridUtil::kCondDim]) noexcept
{
    for (int i = 0; i < GrooveGridUtil::kCondDim; ++i)
        out[i] = 0.0f;

    out[0] = std::clamp((f.bpm - 40.0f) / 260.0f, 0.0f, 1.0f);          // bpm

    // v2 train/serve alignment: dims 1..11 (rms, centroid, density, style, state)
    // are GUITAR-derived, but the model was trained on GMD (drums-only) with these
    // held at 0. Feed neutral values here so the live guitar features don't push
    // the groove model out-of-distribution. Guitar reactivity lives in the
    // selection layer (diversifyPatternForStyle / refineByRhythm) and in
    // PatternPlayer::setGuitarEnergy, not in the groove renderer's condition.
    // (f.rmsEnergy / f.spectralCentroid / f.onsetDensityPerBeat / styleIndex /
    //  f.state are intentionally ignored here.)

    // Genre one-hot (12..16). The model was trained on the five original feel
    // slots (Rock/Hard Rock/Punk/Metal/Sludge), so a broader genre (e.g. Thrash,
    // Doom, Grunge) maps onto its closest slot rather than widening the input.
    const int genre = std::clamp(Groove::presetFor(genreId).grooveSlot, 0, 4);
    out[12 + genre] = 1.0f;

    const int64_t phase = ((barNumber % 4) + 4) % 4;                     // bar phase 0..3 -> [0, 0.75]
    out[17] = static_cast<float>(phase) / 4.0f;
}

// ── ONNX session (compiled only when the model is bundled) ──────────────────

#if defined(MA_ENABLE_ONNX) && defined(MA_BUNDLE_GROOVE_RENDERER)

namespace
{
Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "MetalAccompanimentGrooveRenderer");
    return env;
}
} // namespace

struct GrooveRenderer::Impl
{
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memInfo{ Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault) };
};

GrooveRenderer::GrooveRenderer() = default;
GrooveRenderer::~GrooveRenderer() = default;

bool GrooveRenderer::tryLoadModel()
{
    impl = std::make_unique<Impl>();

    const char* data = BinaryData::groove_renderer_onnx;
    const int size = BinaryData::groove_renderer_onnxSize;
    if (data == nullptr || size <= 0)
    {
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
            ortEnv(), reinterpret_cast<const void*>(data), static_cast<size_t>(size), opts);
    }
    catch (const Ort::Exception& e)
    {
        std::fprintf(stderr, "[GrooveRenderer] ORT load failed: %s\n", e.what());
        impl.reset();
        return false;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[GrooveRenderer] load failed: %s\n", e.what());
        impl.reset();
        return false;
    }
    return true;
}

bool GrooveRenderer::isLoaded() const noexcept
{
    return impl != nullptr && impl->session != nullptr;
}

void GrooveRenderer::prepare(double /*sampleRate*/) {}

GrooveGrid GrooveRenderer::render(const ScoreGrid& score, const float condition[GrooveGridUtil::kCondDim],
                                 int64_t barNumber, int patternIndex) const
{
    GrooveGrid grid;
    grid.barNumber = barNumber;
    grid.patternIndex = patternIndex;

    if (impl == nullptr || impl->session == nullptr)
        return grid;  // valid = false

    try
    {
        constexpr int64_t kScoreShape[3] = { 1, GrooveGridUtil::kVoiceCount, GrooveGridUtil::kSteps };
        constexpr int64_t kCondShape[2]  = { 1, GrooveGridUtil::kCondDim };

        std::array<float, GrooveGridUtil::kVoiceCount * GrooveGridUtil::kSteps> scoreFlat{};
        for (int v = 0; v < GrooveGridUtil::kVoiceCount; ++v)
            for (int s = 0; s < GrooveGridUtil::kSteps; ++s)
                scoreFlat[static_cast<size_t>(v * GrooveGridUtil::kSteps + s)] = score[static_cast<size_t>(v)][static_cast<size_t>(s)];

        std::array<float, GrooveGridUtil::kCondDim> condFlat{};
        for (int i = 0; i < GrooveGridUtil::kCondDim; ++i)
            condFlat[static_cast<size_t>(i)] = condition[i];

        Ort::Value scoreT = Ort::Value::CreateTensor<float>(impl->memInfo, scoreFlat.data(), scoreFlat.size(), kScoreShape, 3);
        Ort::Value condT  = Ort::Value::CreateTensor<float>(impl->memInfo, condFlat.data(), condFlat.size(), kCondShape, 2);

        const char* inNames[] = { "score", "condition" };
        const char* outNames[] = { "velocity_mean", "velocity_std", "offset_mean", "offset_std" };
        Ort::Value inputs[] = { std::move(scoreT), std::move(condT) };

        auto outputs = impl->session->Run(Ort::RunOptions{ nullptr }, inNames, inputs, 2, outNames, 4);
        if (outputs.size() < 4)
        {
            runErrorCount.fetch_add(1, std::memory_order_relaxed);
            return grid;
        }

        const float* velMean = outputs[0].GetTensorData<float>();
        const float* velStd  = outputs[1].GetTensorData<float>();
        const float* offMean = outputs[2].GetTensorData<float>();
        const float* offStd  = outputs[3].GetTensorData<float>();

        // Sample velocity/offset per cell from N(mean, std), deterministically
        // per (bar, voice, step): same bar -> same groove, different bar -> new take.
        for (int v = 0; v < GrooveGridUtil::kVoiceCount; ++v)
            for (int s = 0; s < GrooveGridUtil::kSteps; ++s)
            {
                const size_t idx = static_cast<size_t>(v * GrooveGridUtil::kSteps + s);
                const unsigned seed = barHash(static_cast<unsigned>(barNumber),
                                              static_cast<unsigned>(v * GrooveGridUtil::kSteps + s));
                const float ev = gaussianNoise(seed, 0xA5A5A5A5u);
                const float eo = gaussianNoise(seed, 0x5A5A5A5Au);
                // velocity is a *multiplier* on the authored velocity (v2).
                grid.velocity[static_cast<size_t>(v)][static_cast<size_t>(s)] =
                    std::clamp(velMean[idx] + ev * velStd[idx], 0.2f, 2.5f);
                grid.offset[static_cast<size_t>(v)][static_cast<size_t>(s)] =
                    std::clamp(offMean[idx] + eo * offStd[idx], -1.0f, 1.0f);
            }
        grid.valid = true;
    }
    catch (...)
    {
        runErrorCount.fetch_add(1, std::memory_order_relaxed);
        grid.valid = false;
    }
    return grid;
}

#else // !(MA_ENABLE_ONNX && MA_BUNDLE_GROOVE_RENDERER)

struct GrooveRenderer::Impl {};

GrooveRenderer::GrooveRenderer() = default;
GrooveRenderer::~GrooveRenderer() = default;
bool GrooveRenderer::tryLoadModel() { return false; }
bool GrooveRenderer::isLoaded() const noexcept { return false; }
void GrooveRenderer::prepare(double) {}
GrooveGrid GrooveRenderer::render(const ScoreGrid&, const float[GrooveGridUtil::kCondDim], int64_t, int) const { return {}; }

#endif
