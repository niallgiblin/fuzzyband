#include <juce_audio_basics/juce_audio_basics.h>
#include "midi/MidiPatternLibrary.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

// To render per-class reference audio for the rock patterns (22-27) and retrain
// the 28-class mel-CNN, you want one MIDI file per pattern so you can drop each
// onto a GM drum kit in a DAW, loop it for ~10-15s, and render a mono 44.1 kHz /
// 24-bit WAV per take. This tool writes BOTH:
//   * metal_accompaniment_patterns.mid        — all 28 patterns concatenated
//   * pattern_midi/pattern_%02d_<name>.mid    — one MIDI file per pattern
// (the concatenated file preserves the original behaviour; the per-pattern
// files are the new, convenient path).
//
// We serialize the SMF ourselves (instead of juce::MidiFile) because JUCE's
// MidiFile::writeTrack reads each MidiMessage's own timestamp, which collapses
// sub-tick times to zero and flattens the rhythm. Writing the delta-encoded
// Standard MIDI File bytes directly is deterministic and correct.

static constexpr uint16_t kTicksPerQuarter = 960;  // PPQ resolution

// One note-on / note-off event at an absolute tick.
struct Ev
{
    long tick = 0;
    uint8_t status = 0;
    uint8_t d1 = 0;
    uint8_t d2 = 0;
};

static void writeVarLen(std::vector<uint8_t>& out, uint32_t value)
{
    uint32_t buf = value & 0x7Fu;
    while ((value >>= 7) != 0)
    {
        buf <<= 8;
        buf |= ((value & 0x7Fu) | 0x80u);
    }
    for (;;)
    {
        out.push_back(static_cast<uint8_t>(buf & 0xFFu));
        if ((buf & 0x80u) == 0)
            break;
        buf >>= 8;
    }
}

static void writeBytes(std::vector<uint8_t>& out, const uint8_t* p, size_t n)
{
    out.insert(out.end(), p, p + n);
}

static void writeU32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void writeU16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

// Serialize a single-track SMF chunk from already delta-encoded event bytes.
static void writeTrackChunk(std::vector<uint8_t>& out, const std::vector<uint8_t>& eventBytes)
{
    // Ensure the chunk ends with the end-of-track meta (00 FF 2F 00).
    std::vector<uint8_t> data = eventBytes;
    const bool hasEot = data.size() >= 4
        && data[data.size() - 4] == 0x00
        && data[data.size() - 3] == 0xFF
        && data[data.size() - 2] == 0x2F
        && data[data.size() - 1] == 0x00;
    if (!hasEot)
    {
        data.push_back(0x00); data.push_back(0xFF); data.push_back(0x2F); data.push_back(0x00);
    }
    const uint8_t tag[4] = { 'M', 'T', 'r', 'k' };
    writeBytes(out, tag, 4);
    writeU32(out, static_cast<uint32_t>(data.size()));
    writeBytes(out, data.data(), data.size());
}

// Write a Standard MIDI File containing a single pattern (drums ch 10, bass ch 2).
static bool writePatternSmf(const MidiPattern& pattern, int bpm, const juce::File& outFile)
{
    std::vector<Ev> evs;

    // Collect note events at absolute ticks (beats * PPQ).
    // channel here is the 1-indexed GM channel (10 = drums, 2 = bass); the SMF
    // status low nibble is 0-indexed, so subtract 1.
    auto addList = [&](const std::vector<MidiEvent>& list, int channel) {
        for (const auto& ev : list)
        {
            const auto tOn  = static_cast<long>(ev.beatOffset * static_cast<float>(kTicksPerQuarter) + 0.5f);
            const auto tOff = static_cast<long>((ev.beatOffset + ev.durationBeats) * static_cast<float>(kTicksPerQuarter) + 0.5f);
            const auto lo = static_cast<uint8_t>(channel - 1);
            evs.push_back({ tOn,  static_cast<uint8_t>(0x90 | lo), ev.note, ev.velocity });
            evs.push_back({ tOff, static_cast<uint8_t>(0x80 | lo), ev.note, 0 });
        }
    };
    addList(pattern.drumEvents, 10);
    addList(pattern.bassEvents, 2);

    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick; });

    // ── Track 0: tempo meta ──────────────────────────────────────────────
    std::vector<uint8_t> bytes;
    const uint32_t microsPerQuarter = static_cast<uint32_t>(60000000.0 / bpm);
    bytes.push_back(0x00);  // delta 0
    bytes.push_back(0xFF);  // meta
    bytes.push_back(0x51);  // set tempo
    bytes.push_back(0x03);
    bytes.push_back(static_cast<uint8_t>((microsPerQuarter >> 16) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((microsPerQuarter >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(microsPerQuarter & 0xFF));
    bytes.push_back(0x00); bytes.push_back(0xFF); bytes.push_back(0x2F); bytes.push_back(0x00);  // end of track

    // ── Track 1: notes ───────────────────────────────────────────────────
    std::vector<uint8_t> noteBytes;
    long lastTick = 0;
    for (const auto& e : evs)
    {
        const auto delta = static_cast<uint32_t>(e.tick - lastTick);
        writeVarLen(noteBytes, delta);
        noteBytes.push_back(e.status);
        noteBytes.push_back(e.d1);
        noteBytes.push_back(e.d2);
        lastTick = e.tick;
    }
    noteBytes.push_back(0x00); noteBytes.push_back(0xFF); noteBytes.push_back(0x2F); noteBytes.push_back(0x00);  // EOT

    // ── Header ───────────────────────────────────────────────────────────
    std::vector<uint8_t> header;
    const uint8_t thd[4] = { 'M', 'T', 'h', 'd' };
    writeBytes(header, thd, 4);
    writeU32(header, 6);
    writeU16(header, 1);                    // format 1
    writeU16(header, 2);                    // 2 tracks
    writeU16(header, kTicksPerQuarter);     // division
    writeTrackChunk(header, bytes);
    writeTrackChunk(header, noteBytes);

    juce::FileOutputStream stream(outFile);
    if (!stream.openedOk())
    {
        std::cerr << "Failed to open " << outFile.getFullPathName().toStdString() << "\n";
        return false;
    }
    stream.write(header.data(), header.size());
    std::cout << "Wrote " << outFile.getFullPathName().toStdString() << " (" << header.size() << " bytes)\n";
    return true;
}

// Convert a pattern name to a class-style slug: lowercase, non-alphanumeric
// becomes '_', runs collapsed and trimmed. "Rock Half-Time" -> "rock_half_time".
static juce::String slugify(const juce::String& name)
{
    juce::String s = name.toLowerCase();
    juce::String out;
    bool lastUnderscore = true;  // suppress leading separator
    const int len = s.length();
    for (int i = 0; i < len; ++i)
    {
        const juce::juce_wchar c = s[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum)
        {
            out += c;
            lastUnderscore = false;
        }
        else if (!lastUnderscore)
        {
            out += (juce::juce_wchar) '_';
            lastUnderscore = true;
        }
    }
    while (out.length() > 0 && out.endsWithChar('_'))
        out = out.substring(0, out.length() - 1);
    return out;
}

int main(int argc, char* argv[])
{
    juce::ignoreUnused(argc, argv);

    const int bpm = 120;
    MidiPatternLibrary lib;

    juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                            .getParentDirectory();

    const juce::File midiDir = exeDir.getChildFile("pattern_midi");
    midiDir.createDirectory();

    for (int i = 0; i < lib.patternCount(); ++i)
    {
        const juce::String name = juce::String::formatted("pattern_%02d_%s.mid",
                                                          i, slugify(lib.getPattern(i).name).toRawUTF8());
        writePatternSmf(lib.getPattern(i), bpm, midiDir.getChildFile(name));
    }

    // Concatenated file: a single note track with every pattern in sequence.
    // Reuse the per-pattern serializer by accumulating all events into one file.
    std::vector<Ev> all;
    double timeTicks = 0.0;
    auto addList = [&](const std::vector<MidiEvent>& list, int channel) {
        for (const auto& ev : list)
        {
            const auto tOn  = static_cast<long>(timeTicks + ev.beatOffset * static_cast<float>(kTicksPerQuarter) + 0.5f);
            const auto tOff = static_cast<long>(timeTicks + (ev.beatOffset + ev.durationBeats) * static_cast<float>(kTicksPerQuarter) + 0.5f);
            const auto lo = static_cast<uint8_t>(channel - 1);
            all.push_back({ tOn,  static_cast<uint8_t>(0x90 | lo), ev.note, ev.velocity });
            all.push_back({ tOff, static_cast<uint8_t>(0x80 | lo), ev.note, 0 });
        }
    };
    for (int i = 0; i < lib.patternCount(); ++i)
    {
        const auto& p = lib.getPattern(i);
        addList(p.drumEvents, 10);
        addList(p.bassEvents, 2);
        timeTicks += static_cast<double>(p.lengthInBars) * 4.0 * static_cast<double>(kTicksPerQuarter);
    }
    std::stable_sort(all.begin(), all.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick; });

    std::vector<uint8_t> noteBytes;
    long lastTick = 0;
    for (const auto& e : all)
    {
        const auto delta = static_cast<uint32_t>(e.tick - lastTick);
        writeVarLen(noteBytes, delta);
        noteBytes.push_back(e.status);
        noteBytes.push_back(e.d1);
        noteBytes.push_back(e.d2);
        lastTick = e.tick;
    }
    noteBytes.push_back(0x00); noteBytes.push_back(0xFF); noteBytes.push_back(0x2F); noteBytes.push_back(0x00);

    const uint32_t microsPerQuarter = static_cast<uint32_t>(60000000.0 / bpm);
    std::vector<uint8_t> metaBytes;
    metaBytes.push_back(0x00); metaBytes.push_back(0xFF); metaBytes.push_back(0x51); metaBytes.push_back(0x03);
    metaBytes.push_back(static_cast<uint8_t>((microsPerQuarter >> 16) & 0xFF));
    metaBytes.push_back(static_cast<uint8_t>((microsPerQuarter >> 8) & 0xFF));
    metaBytes.push_back(static_cast<uint8_t>(microsPerQuarter & 0xFF));
    metaBytes.push_back(0x00); metaBytes.push_back(0xFF); metaBytes.push_back(0x2F); metaBytes.push_back(0x00);

    std::vector<uint8_t> header;
    const uint8_t thd[4] = { 'M', 'T', 'h', 'd' };
    writeBytes(header, thd, 4);
    writeU32(header, 6);
    writeU16(header, 1);
    writeU16(header, 2);
    writeU16(header, kTicksPerQuarter);
    writeTrackChunk(header, metaBytes);
    writeTrackChunk(header, noteBytes);

    const juce::File out = exeDir.getChildFile("metal_accompaniment_patterns.mid");
    juce::FileOutputStream stream(out);
    if (!stream.openedOk())
    {
        std::cerr << "Failed to open output file.\n";
        return 1;
    }
    stream.write(header.data(), header.size());
    std::cout << "Wrote " << out.getFullPathName().toStdString() << "\n";
    return 0;
}
