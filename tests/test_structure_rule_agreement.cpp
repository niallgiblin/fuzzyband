#include <catch2/catch_test_macros.hpp>
#include "inference/RuleStructureInference.h"

TEST_CASE("RuleStructureInference agrees with steady rule state", "[structure][shadow]")
{
    RuleStructureInference r;
    r.prepare(48000.0);
    r.reset();

    FeatureVector fv;
    fv.bpm = 120.f;
    fv.rmsEnergy = 0.2f;
    fv.spectralCentroid = 2000.f;
    fv.highFreqFlux = 0.1f;
    fv.pitchRootMidi = 40.f;
    fv.pitchConfidence = 0.9f;
    fv.state = StructureState::VERSE;
    fv.sampleTimestamp = 0;

    for (int i = 0; i < 50; ++i)
    {
        r.process(fv, 0.05);
        REQUIRE(r.getLastShadowMetrics().agreeRule);
        fv.sampleTimestamp += 512;
    }
}
