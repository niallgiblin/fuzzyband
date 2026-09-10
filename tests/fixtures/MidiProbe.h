#pragma once

/**
 * @file
 * @brief Block-size-independent MIDI capture for PatternPlayer / processor tests.
 *
 * Phase 0 (T0.4). High-value tests in the playability plan assert on rendered
 * MIDI at absolute sample positions, not on a single giant process() call.
 * `absoluteEventSamples()` is the primitive: render the same musical span at
 * two block sizes and compare every event's host-sample time.
 */

#include "midi/PatternPlayer.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct MidiProbe
{
    struct NoteOn
    {
        int note = 0;
        int channel = 0;
        int velocity = 0;
        int64_t sample = 0;
    };

    struct NoteOff
    {
        int note = 0;
        int channel = 0;
        int64_t sample = 0;
    };

    struct Event
    {
        int64_t sample = 0;
        bool isNoteOn = false;
        bool isNoteOff = false;
        int channel = 0;
        int note = 0;
        int velocity = 0;
        int status = 0;
    };

    using FeedFn = std::function<void(juce::MidiBuffer&, int /*numSamples*/, int64_t /*hostSample*/)>;

    static void collect(const juce::MidiBuffer& midi, int64_t blockStart, std::vector<Event>& out)
    {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            Event e;
            e.sample = blockStart + meta.samplePosition;
            e.channel = msg.getChannel();
            e.note = msg.getNoteNumber();
            e.velocity = msg.getVelocity();
            e.status = msg.getRawData()[0];
            e.isNoteOn = msg.isNoteOn();
            e.isNoteOff = msg.isNoteOff();
            out.push_back(e);
        }
    }

    static std::vector<Event> renderWith(int blocks, int blockSize, int64_t startSample, const FeedFn& feed)
    {
        std::vector<Event> out;
        out.reserve(static_cast<size_t>(blocks) * 8);
        int64_t pos = startSample;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            feed(midi, blockSize, pos);
            collect(midi, pos, out);
            pos += blockSize;
        }
        return out;
    }

    static std::vector<Event> render(PatternPlayer& player, int blocks, int blockSize,
                                     int64_t startSample, int64_t* endSample = nullptr)
    {
        auto events = renderWith(blocks, blockSize, startSample,
            [&](juce::MidiBuffer& midi, int n, int64_t pos)
            {
                player.process(midi, n, pos, true);
            });
        if (endSample != nullptr)
            *endSample = startSample + static_cast<int64_t>(blocks) * blockSize;
        return events;
    }

    static std::vector<NoteOn> noteOns(const std::vector<Event>& events)
    {
        std::vector<NoteOn> out;
        for (const auto& e : events)
        {
            if (e.isNoteOn)
                out.push_back({ e.note, e.channel, e.velocity, e.sample });
        }
        return out;
    }

    static std::vector<NoteOn> renderNoteOns(PatternPlayer& player, int blocks, int blockSize,
                                             int64_t startSample, int64_t* endSample = nullptr)
    {
        return noteOns(render(player, blocks, blockSize, startSample, endSample));
    }

    static std::vector<NoteOff> noteOffs(const std::vector<Event>& events)
    {
        std::vector<NoteOff> out;
        for (const auto& e : events)
        {
            if (e.isNoteOff)
                out.push_back({ e.note, e.channel, e.sample });
        }
        return out;
    }

    static std::vector<int64_t> absoluteEventSamples(const std::vector<Event>& events)
    {
        std::vector<int64_t> out;
        out.reserve(events.size());
        for (const auto& e : events)
            out.push_back(e.sample);
        std::sort(out.begin(), out.end());
        return out;
    }

    static std::vector<int64_t> absoluteEventSamples(PatternPlayer& player, int blocks, int blockSize,
                                                     int64_t startSample = 0)
    {
        return absoluteEventSamples(render(player, blocks, blockSize, startSample));
    }

    /** Canonical event key for buffer-size diffs (sample, kind, ch, note, vel). */
    static std::vector<std::string> fingerprint(const std::vector<Event>& events)
    {
        std::vector<Event> sorted = events;
        std::sort(sorted.begin(), sorted.end(), [](const Event& a, const Event& b)
        {
            if (a.sample != b.sample) return a.sample < b.sample;
            if (a.isNoteOn != b.isNoteOn) return a.isNoteOn && !b.isNoteOn;
            if (a.channel != b.channel) return a.channel < b.channel;
            if (a.note != b.note) return a.note < b.note;
            return a.velocity < b.velocity;
        });
        std::vector<std::string> keys;
        keys.reserve(sorted.size());
        for (const auto& e : sorted)
        {
            const char* kind = e.isNoteOn ? "on" : (e.isNoteOff ? "off" : "other");
            keys.push_back(std::to_string(e.sample) + "\t" + kind + "\t"
                           + std::to_string(e.channel) + "\t"
                           + std::to_string(e.note) + "\t"
                           + std::to_string(e.velocity));
        }
        return keys;
    }

    static bool writeTsv(const std::string& path, const std::vector<Event>& events)
    {
        std::ofstream f(path);
        if (!f)
            return false;
        f << "sample\tkind\tchannel\tnote\tvelocity\n";
        for (const auto& line : fingerprint(events))
            f << line << '\n';
        return static_cast<bool>(f);
    }

    static bool writeMidi(const std::string& path, const std::vector<Event>& events,
                          double sampleRate = 48000.0, double bpm = 120.0, int ticksPerBeat = 480)
    {
        juce::MidiMessageSequence seq;
        const double samplesPerBeat = sampleRate * 60.0 / bpm;
        for (const auto& e : events)
        {
            const double ticks = static_cast<double>(e.sample) / samplesPerBeat * ticksPerBeat;
            if (e.isNoteOn)
                seq.addEvent(juce::MidiMessage::noteOn(e.channel, e.note,
                                                       static_cast<juce::uint8>(juce::jlimit(1, 127, e.velocity))),
                             ticks);
            else if (e.isNoteOff)
                seq.addEvent(juce::MidiMessage::noteOff(e.channel, e.note), ticks);
        }
        seq.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote(ticksPerBeat);
        file.addTrack(seq);
        juce::File out(path);
        out.getParentDirectory().createDirectory();
        out.deleteFile();
        juce::FileOutputStream stream(out);
        if (stream.failedToOpen())
            return false;
        return file.writeTo(stream);
    }
};
