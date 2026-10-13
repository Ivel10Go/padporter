#include "padporter/MidiWriter.h"
#include "padporter/PathUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace padporter
{
namespace
{
struct TimedEvent
{
    std::int64_t tick = 0;
    int order = 0;                  // 0 = note-off, 1 = note-on (offs first at equal ticks)
    std::vector<std::uint8_t> bytes;
};

void writeVarLen (std::vector<std::uint8_t>& out, std::uint32_t value)
{
    std::uint8_t buffer[5];
    int n = 0;
    buffer[n++] = static_cast<std::uint8_t> (value & 0x7f);
    while ((value >>= 7) != 0)
        buffer[n++] = static_cast<std::uint8_t> ((value & 0x7f) | 0x80);
    while (n > 0)
        out.push_back (buffer[--n]);
}

void writeBE (std::vector<std::uint8_t>& out, std::uint32_t value, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        out.push_back (static_cast<std::uint8_t> (value >> (8 * i)));
}

std::vector<std::uint8_t> metaText (std::uint8_t type, const std::string& text)
{
    std::vector<std::uint8_t> b { 0xff, type };
    writeVarLen (b, static_cast<std::uint32_t> (text.size()));
    b.insert (b.end(), text.begin(), text.end());
    return b;
}

void appendTrack (std::vector<std::uint8_t>& file, std::vector<TimedEvent> events, std::int64_t endTick)
{
    std::stable_sort (events.begin(), events.end(), [] (const TimedEvent& a, const TimedEvent& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });

    std::vector<std::uint8_t> data;
    std::int64_t last = 0;
    for (const auto& e : events)
    {
        writeVarLen (data, static_cast<std::uint32_t> (e.tick - last));
        data.insert (data.end(), e.bytes.begin(), e.bytes.end());
        last = e.tick;
    }

    writeVarLen (data, static_cast<std::uint32_t> (std::max<std::int64_t> (0, endTick - last)));
    data.insert (data.end(), { 0xff, 0x2f, 0x00 });

    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    writeBE (file, static_cast<std::uint32_t> (data.size()), 4);
    file.insert (file.end(), data.begin(), data.end());
}

int log2Exact (int v)
{
    int n = 0;
    while (v > 1)
    {
        v >>= 1;
        ++n;
    }
    return n;
}
} // namespace

std::vector<std::uint8_t> writeSequenceMidi (const Project& project, const Sequence& sequence,
                                             const std::vector<const Clip*>& clips, const MidiExportOptions& options)
{
    const auto channel = static_cast<std::uint8_t> (std::clamp (options.channel, 0, 15));
    std::int64_t endTick = sequence.lengthTicks();

    // Collect note tracks first so that the end tick covers all notes.
    std::vector<std::pair<std::string, std::vector<TimedEvent>>> noteTracks;

    for (const auto* clip : clips)
    {
        if (clip == nullptr)
            continue;

        // Per pitch: sort, drop exact duplicates, truncate overlaps.
        std::map<int, std::vector<NoteEvent>> byPitch;
        for (const auto& n : clip->notes)
            if (! n.muted)
                byPitch[n.note].push_back (n);

        std::vector<TimedEvent> events;
        for (auto& [pitch, notes] : byPitch)
        {
            std::stable_sort (notes.begin(), notes.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });

            for (std::size_t i = 0; i < notes.size(); ++i)
            {
                if (i + 1 < notes.size() && notes[i + 1].tick == notes[i].tick)
                {
                    notes[i + 1].velocity = std::max (notes[i + 1].velocity, notes[i].velocity);
                    continue; // duplicate at same tick
                }

                auto off = notes[i].tick + std::max<std::int64_t> (1, notes[i].length);
                if (i + 1 < notes.size())
                    off = std::min (off, notes[i + 1].tick);

                const auto on = notes[i].tick;
                events.push_back ({ on, 1, { static_cast<std::uint8_t> (0x90 | channel), static_cast<std::uint8_t> (pitch),
                                             static_cast<std::uint8_t> (notes[i].velocity127()) } });
                events.push_back ({ off, 0, { static_cast<std::uint8_t> (0x80 | channel), static_cast<std::uint8_t> (pitch), 0x40 } });
                endTick = std::max (endTick, off);
            }
        }

        noteTracks.emplace_back (clip->trackName, std::move (events));
    }

    std::vector<std::uint8_t> file { 'M', 'T', 'h', 'd' };
    writeBE (file, 6, 4);
    writeBE (file, 1, 2);
    writeBE (file, static_cast<std::uint32_t> (1 + noteTracks.size()), 2);
    writeBE (file, kTicksPerQuarter, 2);

    // Conductor track
    std::vector<TimedEvent> conductor;
    conductor.push_back ({ 0, 0, metaText (0x03, sequence.name) });

    if (options.includeTimeSignature)
    {
        const auto& ts = sequence.timeSignature;
        conductor.push_back ({ 0, 0, { 0xff, 0x58, 0x04,
                                       static_cast<std::uint8_t> (std::clamp (ts.beatsPerBar, 1, 255)),
                                       static_cast<std::uint8_t> (log2Exact (ts.beatUnit)),
                                       24, 8 } });
    }

    if (options.includeTempo)
    {
        const auto bpm = std::clamp (project.tempoFor (sequence), 10.0, 999.0);
        const auto usPerQuarter = static_cast<std::uint32_t> (std::lround (60'000'000.0 / bpm));
        conductor.push_back ({ 0, 0, { 0xff, 0x51, 0x03,
                                       static_cast<std::uint8_t> (usPerQuarter >> 16),
                                       static_cast<std::uint8_t> (usPerQuarter >> 8),
                                       static_cast<std::uint8_t> (usPerQuarter) } });
    }

    appendTrack (file, std::move (conductor), endTick);

    for (auto& [name, events] : noteTracks)
    {
        events.insert (events.begin(), TimedEvent { 0, -1, metaText (0x03, name) });
        appendTrack (file, std::move (events), endTick);
    }

    return file;
}

std::vector<std::uint8_t> writeSequenceMidi (const Project& project, const Sequence& sequence, const MidiExportOptions& options)
{
    std::vector<const Clip*> clips;
    for (const auto& c : sequence.clips)
        clips.push_back (&c);
    return writeSequenceMidi (project, sequence, clips, options);
}

std::string midiFileName (int sequenceNumber, const std::string& sequenceName, const std::string& suffix)
{
    char prefix[16];
    std::snprintf (prefix, sizeof (prefix), "%02d - ", sequenceNumber);
    auto name = std::string (prefix) + sequenceName;
    if (! suffix.empty())
        name += " - " + suffix;
    return sanitizeFileName (name) + ".mid";
}
} // namespace padporter
