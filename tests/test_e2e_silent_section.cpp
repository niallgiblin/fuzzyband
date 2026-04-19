/**
 * E2E: Feed 5 s of silence through AccompanimentProcessor and assert:
 *   - No note-on MIDI events are emitted
 *   - Pattern index stays at 0 (SILENT → base 0 → PolicyPatternMapper with default params → 0)
 *   - Display state index stays at 0 (SILENT)
 *
 * Audio source: synthesised in memory (no external WAV file required).
 */

#include <catch2/catch_test_macros.hpp>
#include <JuceHeader.h>
#include "AccompanimentProcessor.h"

TEST_CASE("E2E: 5 s silence produces no note-on MIDI and pattern stays at 0", "[e2e][silent]")
{
    const double sr       = 48000.0;
    const int    block    = 512;
    const int    duration = static_cast<int>(5.0 * sr);  // 5 s
    const int    numBlocks = duration / block;

    AccompanimentProcessor proc;
    proc.prepareToPlay(sr, block);
    proc.pauseBackgroundInferenceForTests();

    bool anyNoteOn = false;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buf(2, block);
        buf.clear();

        juce::MidiBuffer midi;
        proc.processBlock(buf, midi);
        proc.flushBackgroundInferenceForTests();

        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                anyNoteOn = true;
    }

    REQUIRE_FALSE(anyNoteOn);
    REQUIRE(proc.getDisplayPatternIndex() == 0);
    REQUIRE(proc.getDisplayStateIndex()   == 0);  // 0 = SILENT

    proc.releaseResources();
}
