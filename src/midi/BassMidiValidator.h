#pragma once

#include "analysis/StructureTagger.h"

/**
 * @brief Validates generative bass proposals before rank/select (Phase 13).
 *
 * Emitted bass uses channel @c kBassChannel (same value as @c PatternPlayer::kBassChannel).
 */
namespace BassMidiValidator
{
constexpr int kBassChannel = 2;
/** @brief @c structureSilentPolicy mirrors processor silence (SILENT + digital silence). */
bool validateBassProposal(float rootMidi,
                          float durationBeats,
                          StructureState st,
                          bool structureSilentPolicy);
} // namespace BassMidiValidator
