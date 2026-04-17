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
    static constexpr float kBreakdownCentroidHz = 1000.0f;
    static constexpr float kVerseCentroidHz = 2200.0f;
    static constexpr double kHoldSilentSec    = 0.0;  // D-07: immediate exit from SILENT
    static constexpr double kHoldVerseSec     = 2.0;
    static constexpr double kHoldChorusSec    = 2.5;
    static constexpr double kHoldBreakdownSec = 2.5;
};
