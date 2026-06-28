#include "RuleBasedBass.h"
#include <algorithm>
#include <cstring>
#include <cmath>

// ══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ══════════════════════════════════════════════════════════════════════════════

float RuleBasedBass::mapToBassRange(float guitarMidi) noexcept
{
    // Map guitar MIDI down one octave to bass range (drop C: C2-G4 → C1-G3)
    float bass = guitarMidi - 12.0f;
    return std::clamp(bass, kBassMinMidi, kBassMaxMidi);
}

int RuleBasedBass::notesPerBar(const char* sectionName) noexcept
{
    if (!sectionName) return 2;

    // Match section name to behavior
    if (std::strcmp(sectionName, "CHORUS") == 0)    return 4;  // driving 8ths
    if (std::strcmp(sectionName, "SOLO") == 0)      return 4;  // busy
    if (std::strcmp(sectionName, "VERSE") == 0)     return 2;  // roots on 1 and 3
    if (std::strcmp(sectionName, "INTRO") == 0)     return 1;  // held root
    if (std::strcmp(sectionName, "BREAKDOWN") == 0) return 1;  // crushing held root
    if (std::strcmp(sectionName, "OUTRO") == 0)     return 1;  // decaying held root

    return 2;  // default: roots on 1 and 3
}

float RuleBasedBass::baseVelocity(const char* sectionName) noexcept
{
    if (!sectionName) return 80.0f;

    if (std::strcmp(sectionName, "CHORUS") == 0)    return 90.0f;
    if (std::strcmp(sectionName, "SOLO") == 0)      return 88.0f;
    if (std::strcmp(sectionName, "BREAKDOWN") == 0) return 85.0f;
    if (std::strcmp(sectionName, "VERSE") == 0)     return 75.0f;
    if (std::strcmp(sectionName, "INTRO") == 0)     return 65.0f;
    if (std::strcmp(sectionName, "OUTRO") == 0)     return 55.0f;

    return 70.0f;  // default
}

// ══════════════════════════════════════════════════════════════════════════════
// Generate
// ══════════════════════════════════════════════════════════════════════════════

PatternPlayer::GrooveCommit RuleBasedBass::generate(
    float guitarRootMidi,
    const char* sectionName,
    bool isFirstBar,
    float bpm) noexcept
{
    (void)isFirstBar;
    (void)bpm;

    PatternPlayer::GrooveCommit commit{};
    commit.hasBassFrame = true;

    const float bassRoot = mapToBassRange(guitarRootMidi);
    commit.bassRootMidi = bassRoot;

    const int notes = notesPerBar(sectionName);
    const float vel = baseVelocity(sectionName) * kVelocityMultiplier;

    // Map note count to 16-step piano roll
    // 1 note: step 0 only (held root)
    // 2 notes: steps 0 and 8 (roots on 1 and 3)
    // 4 notes: steps 0, 4, 8, 12 (8th notes)
    const int stepInterval = 16 / notes;
    for (int i = 0; i < 16; ++i)
    {
        if (i % stepInterval == 0)
        {
            commit.bassPitchOffset[i] = 0.0f;  // root note
            commit.bassVelocity[i] = vel;
        }
        else
        {
            commit.bassPitchOffset[i] = 0.0f;
            commit.bassVelocity[i] = 0.0f;  // rest
        }
    }

    return commit;
}
