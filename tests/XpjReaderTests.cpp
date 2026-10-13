#include "Fixtures.h"

#include "padporter/Gzip.h"
#include "padporter/XpjReader.h"

#include <doctest/doctest.h>

using namespace padporter;

namespace
{
LoadResult parseFixture (const nlohmann::json& j, bool compressed = true)
{
    return XpjReader::parse (XpjReader::decode (fixtures::makeXpj (j, compressed)), "C:/nowhere/My Beat.xpj");
}

bool hasDiagnostic (const Diagnostics& d, const std::string& text)
{
    for (const auto& item : d.items())
        if (item.message.find (text) != std::string::npos)
            return true;
    return false;
}
} // namespace

TEST_CASE ("gzip round trip")
{
    const std::string text = "hello hello hello hello - Grüße";
    const auto gz = gzip (reinterpret_cast<const std::uint8_t*> (text.data()), text.size());
    const auto back = gunzip (gz.data(), gz.size());
    CHECK (std::string (back.begin(), back.end()) == text);

    auto truncated = gz;
    truncated.resize (gz.size() / 2);
    CHECK_THROWS_AS (gunzip (truncated.data(), truncated.size()), ParseError);
}

TEST_CASE ("header is decoded for all container layouts")
{
    for (auto [compressed, inside] : { std::pair { true, true }, std::pair { true, false }, std::pair { false, false } })
    {
        CAPTURE (compressed);
        CAPTURE (inside);
        const auto raw = XpjReader::decode (fixtures::makeXpj (fixtures::sampleProjectJson(), compressed, inside));
        CHECK (raw.header.magic == "ACVS");
        CHECK (raw.header.version == "1.2.1.2");
        CHECK (raw.header.type == "SerialisableProjectData");
        CHECK (raw.header.encoding == "json");
        CHECK (raw.header.platform == "Linux");
        CHECK (raw.header.compressed == compressed);
        CHECK (raw.json.front() == '{');
    }
}

TEST_CASE ("garbage input is rejected with ParseError")
{
    CHECK_THROWS_AS (XpjReader::decode ({}), ParseError);
    CHECK_THROWS_AS (XpjReader::decode ({ 'h', 'e', 'l', 'l', 'o' }), ParseError);

    const std::string broken = "ACVS\n1\nX\njson\nLinux\n{ \"data\": [";
    CHECK_THROWS_AS (XpjReader::parse (XpjReader::decode ({ broken.begin(), broken.end() }), "x.xpj"), ParseError);

    const std::string noData = "{ \"foo\": 1 }";
    CHECK_THROWS_AS (XpjReader::parse (XpjReader::decode ({ noData.begin(), noData.end() }), "x.xpj"), ParseError);
}

TEST_CASE ("project structure is parsed")
{
    const auto r = parseFixture (fixtures::sampleProjectJson());
    const auto& p = r.project;

    CHECK (p.name == "My Beat");
    CHECK (p.masterTempo == doctest::Approx (93.5));
    REQUIRE (p.tracks.size() == 3);
    CHECK (p.defaultDrumTrack() == 0);
    CHECK (p.tracks[2].kind == TrackKind::Routing);
    CHECK_FALSE (hasDiagnostic (r.diagnostics, "'Submix 1' is not a drum program"));

    const auto& drum = p.tracks[0];
    CHECK (drum.kind == TrackKind::Drum);
    CHECK (drum.loadedPadCount() == 3);
    REQUIRE (drum.pads.size() == 4);

    const auto& kick = drum.pads[0];
    CHECK (kick.midiNote == 37);
    CHECK (kick.label() == "A01");
    CHECK (kick.oneShot);
    CHECK (kick.layers.front().sampleFile == "Kick 1.wav");
    CHECK (kick.layers.front().coarseTune == 2);
    CHECK (kick.layers.front().fineTune == -10);
    CHECK (kick.volume == doctest::Approx (0.70794576));
    CHECK (kick.ampEnvelope.release == doctest::Approx (0.1));

    CHECK_FALSE (drum.pads[1].oneShot);                 // gate mode
    CHECK (drum.pads[1].layers.front().sampleFile == "Snäre.wav");
    CHECK (drum.pads[2].muteGroup == 1);
    CHECK_FALSE (drum.pads[3].hasSample());
    CHECK (drum.padForNote (42) == &drum.pads[2]);

    CHECK (p.tracks[1].kind == TrackKind::Other);
    CHECK (hasDiagnostic (r.diagnostics, "not a drum program"));
}

TEST_CASE ("sequences are sorted by key and carry timing")
{
    const auto r = parseFixture (fixtures::sampleProjectJson(), false);
    const auto& p = r.project;
    REQUIRE (p.sequences.size() == 2);

    const auto& intro = p.sequences[0];
    CHECK (intro.name == "Intro");
    CHECK (intro.lengthTicks() == 2 * 4 * 960);
    CHECK (p.tempoFor (intro) == doctest::Approx (128.0));
    REQUIRE (intro.clips.size() == 1);
    CHECK (intro.clips[0].trackName == "Drum 001");
    CHECK (intro.clips[0].notes.size() == 4);
    CHECK (intro.clips[0].playableNoteCount() == 3);
    CHECK (intro.clips[0].notes[1].velocity127() == 64);

    const auto& verse = p.sequences[1];
    CHECK (verse.timeSignature.beatsPerBar == 3);
    CHECK (verse.lengthTicks() == 3 * 960);
    CHECK (verse.clips[0].automation.size() == 1);    // the "invented" type-1 event is skipped
    CHECK (verse.number() == 2);

    CHECK (hasDiagnostic (r.diagnostics, "unknown type 7"));
    CHECK_FALSE (hasDiagnostic (r.diagnostics, "unknown type 1"));
}

TEST_CASE ("main output volume and master compressor are read")
{
    auto j = fixtures::sampleProjectJson();
    CHECK_FALSE (parseFixture (j).project.masterCompressor.present);

    j["data"]["mixer"] = { { "outputs", nlohmann::json::array ({ { { "mixable", {
        { "volume", 0.5 },
        { "inserts", { { "effects", nlohmann::json::array ({
            { { "enable", true }, { "plugin", { { "description", { { "name", "Flex Beat Player" } } } } } },
            { { "enable", true }, { "plugin", { { "description", { { "name", "Color Compressor" } } }, { "state", "119.AAAA" } } } },
        }) } } },
    } } } }) } };
    j["data"]["tracks"][0]["program"]["mixable"] = { { "volume", 0.25 } };
    j["data"]["tracks"][0]["program"]["drum"]["instruments"][0]["coarseTune"] = -2;

    const auto r = parseFixture (j);
    CHECK (r.project.masterVolume == doctest::Approx (0.5));
    CHECK (r.project.masterCompressor.present);
    CHECK (r.project.masterCompressor.active());
    CHECK_FALSE (r.project.masterCompressor.colorOn);
    CHECK (r.project.tracks[0].programVolume == doctest::Approx (0.25));
    CHECK (r.project.tracks[0].pads[0].instCoarseTune == -2);
}

TEST_CASE ("stored lengthPulses take precedence over bars")
{
    auto j = fixtures::sampleProjectJson();
    j["data"]["sequences"][1]["value"]["lengthPulses"] = 5000;
    const auto r = parseFixture (j);
    CHECK (r.project.sequences[0].lengthTicks() == 5000);
}

TEST_CASE ("master tempo overrides sequence tempo")
{
    auto j = fixtures::sampleProjectJson();
    j["data"]["masterTempoEnabled"] = true;
    const auto r = parseFixture (j);
    CHECK (r.project.tempoFor (r.project.sequences[0]) == doctest::Approx (93.5));
}

TEST_CASE ("missing optional fields fall back without crashing")
{
    const nlohmann::json minimal = { { "data", { { "tracks", nlohmann::json::array ({ { { "program", { { "drum", { { "instruments",
        nlohmann::json::array ({ { { "layersv", nlohmann::json::array ({ { { "sampleFile", "a.wav" } } }) } } }) } } } } } } }) } } } };

    const auto r = parseFixture (minimal);
    REQUIRE (r.project.tracks.size() == 1);
    CHECK (r.project.tracks[0].pads[0].midiNote == 36);
    CHECK (r.project.sequences.empty());
    CHECK (r.diagnostics.count (Severity::Warning) >= 2);
}
