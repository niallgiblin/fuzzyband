#pragma once

/**
 * @file
 * @brief Tier-1 conditional groove renderer (ONNX).
 *
 * Loads assets/groove_renderer.onnx (a heteroscedastic model: per-cell mean +
 * std for velocity and microtiming) and, for a given authored drum score +
 * playing context, renders one bar of data-derived groove by sampling N(mean,std)
 * deterministically per bar. This replaces the fixed Groove::Template hierarchy
 * with a learned, per-bar groove; when the model is absent or fails,
 * PatternPlayer falls back to the template unchanged.
 *
 * The pure helpers (buildScoreGrid / buildCondition) are header-declared and
 * always compiled so they can be unit-tested without ONNX.
 */

#include "analysis/FeatureVector.h"
#include "midi/GrooveGrid.h"
#include "midi/MidiPatternLibrary.h"
#include <atomic>
#include <cstdint>
#include <memory>

class GrooveRenderer final
{
public:
    GrooveRenderer();
    ~GrooveRenderer();

    /** @brief Load groove_renderer.onnx from BinaryData (no-op when not bundled). */
    bool tryLoadModel();
    bool isLoaded() const noexcept;

    /** @brief No sample-rate-dependent state. */
    void prepare(double sampleRate);

    // ── Pure helpers (deterministic, allocation-free; testable without ONNX) ──

    /** @brief Quantize a pattern's drum events into a 10-voice x 16-step score grid. */
    static void buildScoreGrid(const MidiPattern& pattern, ScoreGrid& out) noexcept;

    /** @brief Build the 18-dim condition vector from live features + genre + bar phase. */
    static void buildCondition(const FeatureVector& f, int genreId, int64_t barNumber,
                               int styleIndex, float out[GrooveGridUtil::kCondDim]) noexcept;

    /**
     * @brief Run the model for one bar: sample velocity/offset from the predicted
     *  (mean, std) per cell, deterministically per bar. Returns valid=false on any
     *  failure or when the model is not loaded (caller falls back to the template).
     */
    GrooveGrid render(const ScoreGrid& score, const float condition[GrooveGridUtil::kCondDim],
                      int64_t barNumber, int patternIndex) const;

    uint64_t getRunErrorCount() const noexcept { return runErrorCount.load(std::memory_order_relaxed); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    mutable std::atomic<uint64_t> runErrorCount{ 0 };  // mutable: incremented in const render()
};
