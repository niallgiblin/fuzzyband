#include "RuleBasedInference.h"

void RuleBasedInference::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;
}

int RuleBasedInference::selectPattern(const FeatureVector& f)
{
    (void)sampleRate;

    switch (f.state)
    {
        case StructureState::SILENT:
            return 0;
        case StructureState::VERSE:
            if (f.bpm < 120.0f)
                return 1;
            if (f.bpm < 160.0f)
                return 2;
            return 3;
        case StructureState::CHORUS:
            if (f.bpm < 160.0f)
                return 4;
            return 5;
        case StructureState::BREAKDOWN:
            return 6;
    }

    return 0;
}
