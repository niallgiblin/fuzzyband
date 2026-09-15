#pragma once

/**
 * @file
 * @brief Guitar attack (pick-onset) detection for the live bass mirror.
 *
 * One module for the four-term attack predicate and all of its state: the
 * decay→rise edge, the trough it must clear, the amplitude floor, and the
 * minimum interval between attacks. Extracted from @ref PhraseLearner so the
 * predicate can be measured per block (see `docs/BASS_MIRRORING.md` H1/H2)
 * instead of inferred from cumulative counters.
 *
 * Deliberately **not** named `OnsetDetector`: that class does not exist in this
 * repo (`docs/PITFALLS_AND_INVARIANTS.md` §1.2).
 */

#include <cstdint>

/**
 * @brief Why an attack was or was not accepted on one block.
 *
 * Diagnostic only — no behaviour depends on the reason. `accepted == true`
 * implies `blockedBy == None`.
 */
struct AttackVerdict
{
    enum class Blocked : std::uint8_t
    {
        None = 0,             // accepted
        NoRecentFall,         // no decay within the decay-recency window armed the detector
        NoSharpRise,          // level did not rise sharply vs the EMA or the previous block
        TroughTooShallow,     // rise did not clear the trough by the required margin
        BelowAmplitudeFloor,  // block was below the attack amplitude floor
        NotTransient,         // no broadband (pick) transient — a sustained-note ripple
        MinIntervalGate       // a real edge, but the min-interval gate was still closed
    };

    bool accepted = false;
    Blocked blockedBy = Blocked::None;

    // Predicate-term truth table (the H1 experiment reads these).
    bool armed = false;
    bool sharpRise = false;
    bool clearsFloor = false;
    bool aboveFloor = false;
    bool transient = true;
    bool gateOpen = false;

    // Numeric margins, for the same experiment.
    float troughMargin = 0.0f;  // rms - (trough * kFloorRise + kFloorAbs); > 0 clears
    float riseRatio = 0.0f;     // rms / max(rmsSmooth, eps)
};

/**
 * @brief The attack predicate and its envelope/trough state.
 *
 * Call @ref prepare once, then @ref classify per audio block (audio thread).
 * Real-time safe: no allocation, no locks.
 */
class AttackDetector
{
public:
    /** @brief Cumulative diagnostic counters (was @ref PhraseLearner::AttackDebug). */
    struct AttackDebug
    {
        std::int64_t riseEdges = 0;      // armed + sharp rise + above the level floor
        std::int64_t clearedFloor = 0;   // …and the rise also cleared the trough
        std::int64_t blockedByFloor = 0; // rise edge rejected by the trough test
        std::int64_t accepted = 0;       // actually accepted as an attack
    };

    /** @brief Derive the time-based window from the sample rate (A1). */
    void prepare(double sampleRate) noexcept;

    void reset() noexcept;

    /**
     * @brief Advance the envelope by one block and classify it.
     * @param rms        onset RMS for this block (see @ref EnergyAnalyser::getOnsetRmsEnergy)
     * @param sampleTime absolute sample position of the block start
     * @param hfFlux     >2 kHz spectral flux, or negative when unknown. A real
     *        pick is a broadband transient; the RMS ripple of a sustained low
     *        note is not. When supplied, the transient gate keeps a fast pick
     *        from being rejected by the level-only trough test without letting a
     *        sustained note's ripple through.
     */
    AttackVerdict classify(float rms, std::int64_t sampleTime, float hfFlux = -1.0f) noexcept;

    /** @brief Sample of the last accepted attack (0 before the first). */
    std::int64_t getLastAttackSample() const noexcept { return lastAttackSample_; }

    /** @brief Decay-recency window in samples (diagnostics/tests). */
    std::int64_t getFallWindowSamples() const noexcept { return fallWindowSamples_; }

    const AttackDebug& getDebug() const noexcept { return debug_; }
    void resetDebug() noexcept { debug_ = AttackDebug{}; }

private:
    double sampleRate_ = 48000.0;

    float prevRms_ = 0.0f;
    float rmsSmooth_ = 0.0f;      // fast EMA for the rise-vs-level ratio
    std::int64_t lastFallSample_ = -1;
    std::int64_t fallWindowSamples_ = 9600;
    float rmsFloorSinceArm_ = 0.0f;
    bool risePending_ = false;
    std::int64_t lastAttackSample_ = 0;
    float hfFluxAvg_ = 0.0f;   // slow average of the pick-transient flux

    AttackDebug debug_{};

    static constexpr std::int64_t kMinAttackIntervalSamples = 2000;  // ~40 ms
    static constexpr double kFallWindowSeconds = 0.2;
    static constexpr float kRiseVsSmooth = 1.15f;
    static constexpr float kRiseVsPrev = 1.20f;
    static constexpr float kFloorRise = 1.10f;
    // Absolute trough term dropped: with the transient gate below, the trough
    // test no longer has to reject sustained-note ripple, so it can be
    // relative-only and stop rejecting real picks. (Was 1.15 + 0.005, which
    // rejected 68% of a real DI's rise edges.)
    static constexpr float kFloorAbs = 0.0f;
    // Pick-transient gate: flux must exceed a slow average of itself.
    static constexpr float kFluxRel = 1.0f;
    static constexpr float kFluxAbs = 1.0e-3f;
    static constexpr float kAmplitudeFloor = 0.01f;
    static constexpr float kSilenceFloor = 0.002f;
};
