#pragma once

enum class StructureState
{
    SILENT,
    VERSE,
    CHORUS,
    BREAKDOWN
};

class StructureTagger
{
public:
    void prepare(double sampleRate);

    /** Call once per block with latest analyser outputs. */
    StructureState update(float rms, float centroid, float highFreqFlux, int numSamples);

    StructureState getCurrentState() const { return currentState; }

private:
    StructureState computeDesiredState(float rms, float centroid) const;

    double sampleRate = 44100.0;
    double timeInStateSec = 0.0;
    StructureState currentState = StructureState::SILENT;
    static constexpr float kSilentRms = 0.05f;
    static constexpr float kBreakdownCentroidHz = 1200.0f;
    static constexpr float kVerseCentroidHz = 2400.0f;
    static constexpr double kHoldSilentSec    = 0.0;  // D-07: immediate exit from SILENT
    static constexpr double kHoldVerseSec     = 1.5;  // tune after jam session
    static constexpr double kHoldChorusSec    = 2.0;  // tune after jam session
    static constexpr double kHoldBreakdownSec = 3.0;  // tune after jam session
};
