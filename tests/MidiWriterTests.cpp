#include "Fixtures.h"

#include "padporter/MidiWriter.h"
#include "padporter/XpjReader.h"

#include <doctest/doctest.h>

using namespace padporter;

namespace
{
struct ParsedEvent
{
    std::int64_t tick;
    std::vector<std::uint8_t> bytes;
};

std::uint32_t readBE (const std::vector<std::uint8_t>& d, std::size_t pos, int n)
{
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i)
        v = (v << 8) | d[pos + static_cast<std::size_t> (i)];
    return v;
}

/** Tiny SMF reader for verification (no running status - the writer never uses it). */
std::vector<std::vector<ParsedEvent>> readTracks (const std::vector<std::uint8_t>& d, int& format, int& division)
{
    REQUIRE (d.size() > 14);
    REQUIRE (std::string (d.begin(), d.begin() + 4) == "MThd");
    format = static_cast<int> (readBE (d, 8, 2));
    const auto ntrks = readBE (d, 10, 2);
    division = static_cast<int> (readBE (d, 12, 2));

    std::vector<std::vector<ParsedEvent>> tracks;
    std::size_t pos = 14;
    for (std::uint32_t t = 0; t < ntrks; ++t)
    {
        REQUIRE (std::string (d.begin() + static_cast<std::ptrdiff_t> (pos), d.begin() + static_cast<std::ptrdiff_t> (pos) + 4) == "MTrk");
        const auto len = readBE (d, pos + 4, 4);
        std::size_t p = pos + 8;
        const std::size_t end = p + len;
        std::int64_t tick = 0;
        std::vector<ParsedEvent> events;

        while (p < end)
        {
            std::uint32_t delta = 0;
            std::uint8_t b;
            do { b = d[p++]; delta = (delta << 7) | (b & 0x7f); } while (b & 0x80);
            tick += delta;

            const auto status = d[p];
            std::size_t size;
            if (status == 0xff)
            {
                std::uint32_t l = 0;
                std::size_t q = p + 2;
                do { b = d[q++]; l = (l << 7) | (b & 0x7f); } while (b & 0x80);
                size = (q - p) + l;
            }
            else
            {
                size = 3;
            }
            events.push_back ({ tick, { d.begin() + static_cast<std::ptrdiff_t> (p), d.begin() + static_cast<std::ptrdiff_t> (p + size) } });
            p += size;
        }
        tracks.push_back (std::move (events));
        pos = end;
    }
    return tracks;
}
} // namespace

TEST_CASE ("sequence is written as a valid format-1 MIDI file")
{
    const auto r = XpjReader::parse (XpjReader::decode (fixtures::makeXpj (fixtures::sampleProjectJson())), "x.xpj");
    const auto& seq = r.project.sequences[0]; // "Intro", 128 BPM, 2 bars 4/4
    const auto bytes = writeSequenceMidi (r.project, seq);

    int format = 0, division = 0;
    const auto tracks = readTracks (bytes, format, division);
    CHECK (format == 1);
    CHECK (division == 960);
    REQUIRE (tracks.size() == 2);

    // Conductor: name, time signature, tempo, end of track at the sequence end.
    bool foundTempo = false, foundSig = false;
    for (const auto& e : tracks[0])
    {
        if (e.bytes[1] == 0x51)
        {
            foundTempo = true;
            const auto us = (e.bytes[3] << 16) | (e.bytes[4] << 8) | e.bytes[5];
            CHECK (us == 468750); // 60e6 / 128
        }
        if (e.bytes[1] == 0x58)
        {
            foundSig = true;
            CHECK (e.bytes[3] == 4);
            CHECK (e.bytes[4] == 2);
        }
    }
    CHECK (foundTempo);
    CHECK (foundSig);
    CHECK (tracks[0].back().tick == 2 * 4 * 960);

    // Notes: 3 playable notes (muted one skipped) -> 3 on + 3 off.
    int ons = 0, offs = 0;
    for (const auto& e : tracks[1])
    {
        if ((e.bytes[0] & 0xf0) == 0x90) ++ons;
        if ((e.bytes[0] & 0xf0) == 0x80) ++offs;
    }
    CHECK (ons == 3);
    CHECK (offs == 3);

    const auto& firstOn = tracks[1][1];
    CHECK (firstOn.tick == 0);
    CHECK (firstOn.bytes[1] == 37);
    CHECK (firstOn.bytes[2] == 127);
}

TEST_CASE ("overlapping notes of the same pitch are truncated")
{
    Project p;
    Sequence s;
    s.name = "Overlap";
    Clip c;
    c.trackName = "T";
    c.notes = { { 0, 36, 1.0, 1000 }, { 500, 36, 1.0, 100 } };
    s.clips.push_back (c);

    int format = 0, division = 0;
    const auto tracks = readTracks (writeSequenceMidi (p, s), format, division);

    std::vector<std::pair<std::int64_t, int>> noteEvents;
    for (const auto& e : tracks[1])
        if (e.bytes[0] != 0xff)
            noteEvents.emplace_back (e.tick, e.bytes[0] & 0xf0);

    REQUIRE (noteEvents.size() == 4);
    CHECK (noteEvents[0] == std::make_pair<std::int64_t, int> (0, 0x90));
    CHECK (noteEvents[1] == std::make_pair<std::int64_t, int> (500, 0x80));
    CHECK (noteEvents[2] == std::make_pair<std::int64_t, int> (500, 0x90));
    CHECK (noteEvents[3] == std::make_pair<std::int64_t, int> (600, 0x80));
}

TEST_CASE ("MIDI file names are sanitised")
{
    CHECK (midiFileName (3, "Intro/Verse?") == "03 - Intro_Verse_.mid");
    CHECK (midiFileName (1, "A", "Drum 001") == "01 - A - Drum 001.mid");
}
