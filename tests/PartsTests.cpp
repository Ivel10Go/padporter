#include "Fixtures.h"

#include "padporter/Parts.h"
#include "padporter/XpjReader.h"

#include <doctest/doctest.h>

using namespace padporter;

namespace
{
PartRole roleOf (const std::string& name, double seconds, double tempo = 0.0)
{
    PadFacts f;
    f.name = name;
    f.durationSeconds = seconds;
    f.sampleTempo = tempo;
    return classifyPad (f);
}
} // namespace

TEST_CASE ("pads are classified as drums or melody (names from real device projects)")
{
    CHECK (roleOf ("Kick 3", 0.23) == PartRole::Drums);
    CHECK (roleOf ("TR-909Hat O 01", 0.03) == PartRole::Drums);
    CHECK (roleOf ("TR-909Kick ", 0.23) == PartRole::Drums);
    CHECK (roleOf ("ClHouse-Kik-Lanes1 EH", 0.37) == PartRole::Drums);
    CHECK (roleOf ("ClHouse-Prc-Lanes1 EH", 1.13) == PartRole::Drums);
    CHECK (roleOf ("Sample 002", 0.2) == PartRole::Drums);
    CHECK (roleOf ("Mystery", -1.0) == PartRole::Drums);

    CHECK (roleOf ("ClHouse-Bas-Lanes C EH", 0.87, 80.75) == PartRole::Melody);
    CHECK (roleOf ("VoxPh-Lanes C120 EH", 3.91, 120.0) == PartRole::Melody);
    CHECK (roleOf ("PianoPh-Lanes Cm 120 EH", 2.42, 120.0) == PartRole::Melody);
    CHECK (roleOf ("Keys-Lanes2 Gm EH ", 1.87, 120.0) == PartRole::Melody);
    CHECK (roleOf ("Lead Lp5 in Am", 18.86, 139.98) == PartRole::Melody);
    CHECK (roleOf ("Pad Lp 3 in Gm", 18.86, 87.89) == PartRole::Melody);
    CHECK (roleOf ("Bassline 10 in Am", 13.71, 139.96) == PartRole::Melody);
    CHECK (roleOf ("Sample 013", 5.24, 91.62) == PartRole::Melody);

    PadFacts chromatic;
    chromatic.name = "Sample 004";
    chromatic.durationSeconds = 0.9;
    chromatic.playedChromatically = true;
    CHECK (classifyPad (chromatic) == PartRole::Melody);
}

TEST_CASE ("16 Levels pitch offsets are read and exported as a chromatic melody")
{
    auto j = fixtures::sampleProjectJson();
    auto& events = j["data"]["sequences"][1]["value"]["trackClipMaps"][0][0]["value"]["eventList"]["events"];
    events[0]["note"]["modifierActiveState0"] = true;
    events[0]["note"]["modifierValue0"] = 0.5 + 4.0 / 240.0;     // +4 semitones
    events[2]["note"]["modifierActiveState0"] = true;
    events[2]["note"]["modifierValue0"] = 0.5 - 1.0 / 240.0;     // -1 semitone

    const auto r = XpjReader::parse (XpjReader::decode (fixtures::makeXpj (j)), "x.xpj");
    const auto& track = r.project.tracks[0];
    const auto& clip = r.project.sequences[0].clips[0];

    CHECK (clip.notes[0].tuneSemitones() == doctest::Approx (4.0));
    CHECK (clip.notes[1].tuneSemitones() == doctest::Approx (0.0));
    CHECK_FALSE (clip.notes[1].isTuned());
    CHECK (clip.notes[2].tuneSemitones() == doctest::Approx (-1.0));

    CHECK (hasTunedNotes (clip, track, 0));        // pad A01 (note 37) was played with 16 Levels
    CHECK_FALSE (hasTunedNotes (clip, track, 1));

    const auto melody = chromaticClipForPad (clip, track, 0);
    REQUIRE (melody.notes.size() == 2);
    CHECK (melody.notes[0].note == 64);            // C3 + 4
    CHECK (melody.notes[1].note == 59);            // C3 - 1
}

TEST_CASE ("sequences are split into drums, melody and unassigned notes")
{
    auto j = fixtures::sampleProjectJson();
    auto lead = j["data"]["tracks"][0]["program"]["drum"]["instruments"][0];
    lead["layersv"][0]["sampleFile"] = "Lead Lp1 in Am.wav";
    lead["layersv"][0]["sampleName"] = "Lead Lp1 in Am";
    j["data"]["tracks"][0]["program"]["drum"]["instruments"][3] = lead;   // note 39
    auto& events = j["data"]["sequences"][1]["value"]["trackClipMaps"][0][0]["value"]["eventList"]["events"];
    events.push_back ({ { "type", 3 }, { "time", 0 }, { "note", { { "note", 39 }, { "velocity", 1.0 }, { "length", 960 } } } });
    events.push_back ({ { "type", 3 }, { "time", 480 }, { "note", { { "note", 70 }, { "velocity", 1.0 }, { "length", 60 } } } });

    const auto r = XpjReader::parse (XpjReader::decode (fixtures::makeXpj (j)), "x.xpj");
    const auto& track = r.project.tracks[0];
    const auto roles = classifyTrack (r.project, track, [] (const Pad&) { return -1.0; });

    CHECK (roles[0] == PartRole::Drums);
    CHECK (roles[1] == PartRole::Drums);
    CHECK (roles[3] == PartRole::Melody);

    const auto& intro = r.project.sequences[0];
    const auto counts = countParts (intro.clips[0], track, roles);
    CHECK (counts.drums == 3);       // muted hat excluded
    CHECK (counts.melody == 1);
    CHECK (counts.unassigned == 1);

    CHECK (filterClip (intro.clips[0], track, roles, Part::Drums).notes.size() == 3);
    CHECK (filterClip (intro.clips[0], track, roles, Part::Melody).notes.size() == 1);
    CHECK (filterClip (intro.clips[0], track, roles, Part::All).notes.size() == 5);
}
