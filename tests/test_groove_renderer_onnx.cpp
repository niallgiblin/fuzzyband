#include <catch2/catch_test_macros.hpp>
#include "midi/GrooveGrid.h"
#include "midi/MidiPatternLibrary.h"
#include "inference/GrooveRenderer.h"

// Compiled only when MA_BUNDLE_GROOVE_RENDERER=ON (see CMakeLists.txt): exercises
// the real ONNX session path, not the no-model stub.

TEST_CASE("GrooveRenderer loads the bundled model and renders a valid grid", "[groove_renderer][onnx]")
{
    GrooveRenderer renderer;
    REQUIRE(renderer.tryLoadModel());
    REQUIRE(renderer.isLoaded());

    MidiPatternLibrary lib;
    ScoreGrid score{};
    GrooveRenderer::buildScoreGrid(lib.getPattern(1), score);  // Verse Groove

    float cond[GrooveGridUtil::kCondDim]{};
    cond[0] = 0.5f;   // ~170 BPM
    cond[12] = 1.0f;  // Rock genre one-hot

    const GrooveGrid grid = renderer.render(score, cond, /*barNumber*/ 7, /*patternIndex*/ 1);
    REQUIRE(grid.valid);

    // Sampled values must stay in range for every authored hit.
    bool anyHit = false;
    for (int v = 0; v < GrooveGridUtil::kVoiceCount; ++v)
        for (int s = 0; s < GrooveGridUtil::kSteps; ++s)
            if (score[static_cast<size_t>(v)][static_cast<size_t>(s)] > 0.5f)
            {
                REQUIRE(grid.velocity[static_cast<size_t>(v)][static_cast<size_t>(s)] >= 0.2f);
                REQUIRE(grid.velocity[static_cast<size_t>(v)][static_cast<size_t>(s)] <= 2.5f);
                REQUIRE(grid.offset[static_cast<size_t>(v)][static_cast<size_t>(s)] >= -1.0f);
                REQUIRE(grid.offset[static_cast<size_t>(v)][static_cast<size_t>(s)] <= 1.0f);
                anyHit = true;
            }
    REQUIRE(anyHit);

    // Determinism: same bar -> same sampled grid.
    const GrooveGrid grid2 = renderer.render(score, cond, 7, 1);
    for (int v = 0; v < GrooveGridUtil::kVoiceCount; ++v)
        for (int s = 0; s < GrooveGridUtil::kSteps; ++s)
        {
            REQUIRE(grid.velocity[static_cast<size_t>(v)][static_cast<size_t>(s)]
                    == grid2.velocity[static_cast<size_t>(v)][static_cast<size_t>(s)]);
            REQUIRE(grid.offset[static_cast<size_t>(v)][static_cast<size_t>(s)]
                    == grid2.offset[static_cast<size_t>(v)][static_cast<size_t>(s)]);
        }
}
