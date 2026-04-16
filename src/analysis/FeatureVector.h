#pragma once

#include "StructureTagger.h"
#include <cstdint>

struct FeatureVector
{
    float bpm = 120.0f;
    float rmsEnergy = 0.0f;
    float spectralCentroid = 0.0f;
    float highFreqFlux = 0.0f;
    StructureState state = StructureState::SILENT;
    int64_t sampleTimestamp = 0;
};
