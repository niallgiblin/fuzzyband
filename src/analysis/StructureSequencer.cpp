#include "StructureSequencer.h"
#include <algorithm>
#include <sstream>

namespace
{
const std::vector<const char*> kSectionNames = {
    "INTRO", "VERSE", "CHORUS", "BREAKDOWN", "SOLO", "OUTRO",
};
} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// Presets
// ══════════════════════════════════════════════════════════════════════════════

std::vector<SongForm> StructureSequencer::buildPresets()
{
    using S = SongSection;
    std::vector<SongForm> forms;

    // ── Preset 0: Standard Metal ──────────────────────────────────────────
    {
        SongForm f;
        f.name = "Standard Metal";
        f.sections = {
            S{ "INTRO",      4 },
            S{ "VERSE",      8 },
            S{ "CHORUS",     8 },
            S{ "VERSE",      8 },
            S{ "CHORUS",     8 },
            S{ "BREAKDOWN",  8 },
            S{ "SOLO",       8 },
            S{ "CHORUS",     8 },
            S{ "OUTRO",      4 },
        };
        forms.push_back(std::move(f));
    }

    // ── Preset 1: Sludge / Drone ──────────────────────────────────────────
    {
        SongForm f;
        f.name = "Sludge / Drone";
        f.sections = {
            S{ "VERSE",      8 },
            S{ "VERSE",      8 },
            S{ "CHORUS",     4 },
            S{ "BREAKDOWN",  12 },
            S{ "VERSE",      8 },
            S{ "BREAKDOWN",  12 },
            S{ "OUTRO",      4 },
        };
        forms.push_back(std::move(f));
    }

    // ── Preset 2: Short Punk ──────────────────────────────────────────────
    {
        SongForm f;
        f.name = "Short Punk";
        f.sections = {
            S{ "INTRO",      2 },
            S{ "VERSE",      8 },
            S{ "CHORUS",     8 },
            S{ "VERSE",      8 },
            S{ "CHORUS",     8 },
            S{ "OUTRO",      2 },
        };
        forms.push_back(std::move(f));
    }

    return forms;
}

const std::vector<SongForm> StructureSequencer::presets = StructureSequencer::buildPresets();

// ══════════════════════════════════════════════════════════════════════════════
// Constructor
// ══════════════════════════════════════════════════════════════════════════════

StructureSequencer::StructureSequencer()
{
    if (!presets.empty())
        loadForm(presets[0]);
}

void StructureSequencer::loadForm(const SongForm& form)
{
    currentForm = form;
    reset();
}

void StructureSequencer::loadPreset(int presetIndex)
{
    const int idx = std::clamp(presetIndex, 0, static_cast<int>(presets.size()) - 1);
    loadForm(presets[static_cast<size_t>(idx)]);
}

void StructureSequencer::reset()
{
    currentSection = 0;
    barsElapsed = 0;
    globalBarCount = 0;
    barAccumulator = 0.0;
    samplesPerBar = 0.0;
    complete = false;
}

// ══════════════════════════════════════════════════════════════════════════════
// Advance
// ══════════════════════════════════════════════════════════════════════════════

void StructureSequencer::advance(int numSamples, float bpm, double sampleRate)
{
    if (currentForm.sections.empty() || complete) return;

    // Recompute samplesPerBar when BPM or sampleRate changes (cheap check)
    const double newSamplesPerBar = (60.0 / static_cast<double>(bpm)) * sampleRate * 4.0;
    if (std::abs(newSamplesPerBar - samplesPerBar) > 0.5)
    {
        samplesPerBar = newSamplesPerBar;
    }

    if (samplesPerBar <= 0.0) return;

    barAccumulator += static_cast<double>(numSamples);

    while (barAccumulator >= samplesPerBar)
    {
        barAccumulator -= samplesPerBar;
        ++barsElapsed;
        ++globalBarCount;

        const int barsInSection = getBarsInSection();
        if (barsElapsed >= barsInSection)
        {
            barsElapsed = 0;
            ++currentSection;

            // Loop back or stop
            if (currentSection >= static_cast<int>(currentForm.sections.size()))
            {
                if (looping)
                    currentSection = 0;
                else
                {
                    complete = true;
                    return;
                }
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// Accessors
// ══════════════════════════════════════════════════════════════════════════════

const char* StructureSequencer::getCurrentSectionName() const noexcept
{
    if (currentForm.sections.empty()) return "?";
    const auto idx = static_cast<size_t>(currentSection);
    if (idx >= currentForm.sections.size()) return "?";
    return currentForm.sections[idx].name.c_str();
}

int StructureSequencer::getBarsInSection() const noexcept
{
    if (currentForm.sections.empty()) return 8;
    const auto idx = static_cast<size_t>(currentSection);
    if (idx >= currentForm.sections.size()) return 8;
    return currentForm.sections[idx].bars;
}

bool StructureSequencer::isLastBar() const noexcept
{
    return barsElapsed == getBarsInSection() - 1;
}

// ══════════════════════════════════════════════════════════════════════════════
// Custom-form serialization (Phase 2)
// ══════════════════════════════════════════════════════════════════════════════

const std::vector<const char*>& StructureSequencer::sectionNames() noexcept
{
    return kSectionNames;
}

bool StructureSequencer::isValidSectionName(const std::string& name) noexcept
{
    for (const char* n : kSectionNames)
        if (name == n)
            return true;
    return false;
}

std::string StructureSequencer::serializeForm(const SongForm& form)
{
    std::string out;
    for (size_t i = 0; i < form.sections.size(); ++i)
    {
        if (i > 0)
            out += ',';
        out += form.sections[i].name;
        out += ':';
        out += std::to_string(form.sections[i].bars);
    }
    return out;
}

SongForm StructureSequencer::parseFormString(const std::string& serialized)
{
    SongForm form;
    form.name = "Custom";

    // Split on ',' then each token on ':'. Unknown names and unparsable bar
    // counts are skipped; a fully invalid string falls back to preset 0.
    std::stringstream ss(serialized);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        const size_t colon = token.find(':');
        if (colon == std::string::npos)
            continue;

        std::string name = token.substr(0, colon);
        const std::string barsStr = token.substr(colon + 1);

        // Trim surrounding whitespace on the name.
        name.erase(0, name.find_first_not_of(" \t\r\n"));
        name.erase(name.find_last_not_of(" \t\r\n") + 1);

        if (!isValidSectionName(name))
            continue;

        int bars = 0;
        try
        {
            bars = std::stoi(barsStr);
        }
        catch (...)
        {
            continue;
        }

        form.sections.push_back(SongSection{ name, std::clamp(bars, 1, 64) });
    }

    if (form.sections.empty())
        return presets[0];
    return form;
}
