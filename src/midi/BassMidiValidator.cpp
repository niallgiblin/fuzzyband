#include "BassMidiValidator.h"
#include <cmath>

namespace BassMidiValidator
{
namespace
{
/** E1 bass through G3 — typical playable metal bass range for generative proposals. */
constexpr int kMinBassNote = 28;
constexpr int kMaxBassNote = 55;

constexpr float kMinDurationBeats = 0.0625f;
constexpr float kMaxDurationBeats = 4.0f;
} // namespace

bool validateBassProposal(float rootMidi,
                          float durationBeats,
                          StructureState /*st*/,
                          bool structureSilentPolicy)
{
    if (structureSilentPolicy)
        return false;

    const int r = static_cast<int>(std::lround(static_cast<double>(rootMidi)));
    if (r < kMinBassNote || r > kMaxBassNote)
        return false;

    if (durationBeats < kMinDurationBeats || durationBeats > kMaxDurationBeats)
        return false;

    return true;
}
} // namespace BassMidiValidator
