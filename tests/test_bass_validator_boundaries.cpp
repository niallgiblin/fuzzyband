#include <catch2/catch_test_macros.hpp>
#include "analysis/StructureTagger.h"
#include "midi/BassMidiValidator.h"

// BassMidiValidator::validateBassProposal(rootMidi, durationBeats, state, structureSilentPolicy)
// Valid range: rootMidi in [28, 55]; any rootMidi outside rejects.
// structureSilentPolicy == true always rejects.

// ─── MIDI note boundaries ─────────────────────────────────────────────────────

TEST_CASE("BassMidiValidator: note 28 (minimum valid) is accepted", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(28.0f, 1.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: note 27 (one below minimum) is rejected", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(27.0f, 1.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: note 55 (maximum valid) is accepted", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(55.0f, 1.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: note 56 (one above maximum) is rejected", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(56.0f, 1.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: note well below range (0) is rejected", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(0.0f, 1.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: note well above range (127) is rejected", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(127.0f, 1.0f, StructureState::SOFT, false));
}

// ─── Fractional MIDI note (should be evaluated by proximity to int range) ────

TEST_CASE("BassMidiValidator: fractional note inside range (40.5) is accepted", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(40.5f, 1.0f, StructureState::SOFT, false));
}

// ─── Silent policy override ──────────────────────────────────────────────────

TEST_CASE("BassMidiValidator: structureSilentPolicy overrides valid note", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(40.0f, 1.0f, StructureState::SOFT, true));
}

TEST_CASE("BassMidiValidator: structureSilentPolicy rejects even minimum-valid note", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(28.0f, 1.0f, StructureState::LOUD, true));
}

// ─── Non-silent StructureState values ────────────────────────────────────────

TEST_CASE("BassMidiValidator: valid note accepted in SOFT state", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(40.0f, 1.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: valid note accepted in LOUD state", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(40.0f, 1.0f, StructureState::LOUD, false));
}

// ─── Duration boundaries ─────────────────────────────────────────────────────

TEST_CASE("BassMidiValidator: minimum duration (0.0625 beats) is accepted", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(40.0f, 0.0625f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: duration below minimum is rejected", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(40.0f, 0.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: maximum duration (4.0 beats) is accepted", "[bass]")
{
    REQUIRE(BassMidiValidator::validateBassProposal(40.0f, 4.0f, StructureState::SOFT, false));
}

TEST_CASE("BassMidiValidator: duration above maximum is rejected", "[bass]")
{
    REQUIRE_FALSE(BassMidiValidator::validateBassProposal(40.0f, 5.0f, StructureState::SOFT, false));
}

// ─── StructureState is informational only (not checked by validator) ──────────

TEST_CASE("BassMidiValidator: StructureState::SILENT with policy=false and valid note returns true", "[bass]")
{
    // The validator ignores the StructureState argument; only structureSilentPolicy gates on silence.
    REQUIRE(BassMidiValidator::validateBassProposal(40.0f, 1.0f, StructureState::SILENT, false));
}
