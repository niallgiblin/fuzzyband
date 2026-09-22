#pragma once

/**
 * @file
 * @brief Tier-1 groove grid: the data-derived "how to play each hit" surface
 *        produced by the conditional groove renderer (GrooveRenderer) and
 *        consumed by PatternPlayer in place of the fixed Groove::Template.
 *
 * A GrooveGrid is one bar of a 10-voice x 16-sixteenth grid:
 *   - velocity[voice][step] in [0,1] (1.0 = MIDI 127) — the *absolute* velocity
 *     the model predicts for that hit.
 *   - offset[voice][step] in [-1,1] — a tempo-independent *fraction of a 16th
 *     note* (positive = late). The renderer scales it to ms by
 *     (60000 / bpm / 4) at playback.
 *
 * The 10-voice mapping collapses the GM drum notes used by MidiPatternLibrary
 * into the kit voices the model was trained on (see
 * docs/TIER1_GROOVE_MODEL_CONTRACT.md §2.1). Header-only: no ONNX dependency.
 */

#include <array>
#include <cmath>
#include <cstdint>

namespace GrooveGridUtil
{
static constexpr int kVoiceCount = 10;
static constexpr int kSteps       = 16;
static constexpr int kCondDim     = 18;

/** @brief GM note -> kit voice index, or -1 when the note is not in the model. */
inline int voiceForNote(int note) noexcept
{
    switch (note)
    {
        case 35: case 36: return 0;   // kick
        case 38: case 40: return 1;   // snare
        case 42:          return 2;   // hat_closed
        case 46:          return 3;   // hat_open
        case 51:          return 4;   // ride
        case 53:          return 5;   // ride_bell
        case 49: case 52: case 55: return 6;  // crash / china / splash
        case 48:          return 7;   // tom_hi
        case 45:          return 8;   // tom_mid
        case 41:          return 9;   // tom_lo
        default:          return -1;
    }
}

/** @brief 16th step within a bar for an absolute beat offset (0..15). */
inline int stepForBeatInBar(float beatInBar) noexcept
{
    int cell = static_cast<int>(std::lround(beatInBar * 4.0f));
    return ((cell % 16) + 16) % 16;
}

} // namespace GrooveGridUtil

/** @brief One rendered bar of groove (velocity + microtiming) for a pattern. */
struct GrooveGrid
{
    std::array<std::array<float, GrooveGridUtil::kSteps>, GrooveGridUtil::kVoiceCount> velocity{};
    std::array<std::array<float, GrooveGridUtil::kSteps>, GrooveGridUtil::kVoiceCount> offset{};
    bool valid = false;       // false = not rendered (fall back to the template)
    int patternIndex = 0;     // which pattern this grid was rendered for
    int64_t barNumber = 0;    // which bar it was rendered for
};

/** @brief A quantized drum score (hit grid) — same shape as GrooveGridUtil::velocity. */
using ScoreGrid = std::array<std::array<float, GrooveGridUtil::kSteps>, GrooveGridUtil::kVoiceCount>;
