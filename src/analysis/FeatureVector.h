#pragma once

/**
 * @file
 * @brief Feature bundle passed from the audio/analysis path to @ref IInference.
 */

#include "StructureTagger.h"
#include <cstdint>

/**
 * @brief Snapshot of guitar analysis used for pattern selection.
 *
 * Produced on the audio thread and consumed on the background inference thread via a lock-free queue.
 */
struct FeatureVector
{
    float bpm = 120.0f;
    float rmsEnergy = 0.0f;
    float spectralCentroid = 0.0f;
    float highFreqFlux = 0.0f;
    StructureState state = StructureState::SILENT;
    int64_t sampleTimestamp = 0;

    /** Monophonic root (MIDI, may be fractional). PITCH-02: snapshot for inference/playback policy. */
    float pitchRootMidi = 40.0f;
    /** PITCH-02: [0,1] pitch reliability; 0 triggers D-11-04 hold/snap and zero bass offset. */
    float pitchConfidence = 0.0f;

    /** PUI-01: intensity [0,1] — ONNX pattern input column 0 uses PatternRules::adjustedBpm(f),
     *  which applies (policyIntensity - 0.5) * 40 BPM offset alongside the rule path. */
    float policyIntensity = 0.5f;

    /** Signed RMS ratio vs previous block: (rms - prevRms) / max(prevRms, 1e-6). Used for energy-delta bypass. */
    float rmsDelta = 0.0f;

    /** S03: Sub-bass energy ratio (30–120 Hz / total). High for palm-mute chugs, low for clean arpeggios. */
    float subBassRatio = 0.0f;
};
