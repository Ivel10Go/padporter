#include "Fixtures.h"

#include "padporter/PathUtil.h"
#include "padporter/SampleResolver.h"
#include "padporter/WavInfo.h"
#include "padporter/XpjReader.h"

#include <doctest/doctest.h>

using namespace padporter;
namespace fs = std::filesystem;

TEST_CASE ("WAV header and atem slices are read")
{
    const std::string atem = R"({"value0":{"Num slices":3,"Slice 0":{"Start":0,"End":100},"Slice 1":{"Start":100,"End":250},"Slice 2":{"Start":250,"End":400}}})";
    const auto wav = fixtures::makeWav (400, 2, 48000, atem);
    const auto info = parseWavInfo (wav.data(), wav.size());

    REQUIRE (info.valid);
    CHECK (info.channels == 2);
    CHECK (info.sampleRate == 48000);
    CHECK (info.bitsPerSample == 16);
    CHECK (info.frames == 400);
    REQUIRE (info.slices.size() == 3);
    CHECK (info.slices[1].start == 100);
    CHECK (info.slices[1].end == 250);
}

TEST_CASE ("invalid WAV data is reported, not thrown")
{
    const std::vector<std::uint8_t> junk { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' ' };
    const auto info = parseWavInfo (junk.data(), junk.size());
    CHECK_FALSE (info.valid);
    CHECK_FALSE (info.error.empty());

    CHECK (parseAtemSlices ("not json").empty());
}

TEST_CASE ("samples are resolved case-insensitively and with unicode names")
{
    fixtures::TempDir tmp;
    const auto xpj = fixtures::createProjectOnDisk (tmp.path, "Böse Beats", fixtures::sampleProjectJson());
    const auto r = XpjReader::load (xpj);

    CHECK (fs::is_directory (r.project.dataDir));
    SampleResolver resolver (r.project);

    const auto kick = resolver.resolve ("Kick 1.wav");
    CHECK (kick.found);
    CHECK (kick.how == "direct");

    CHECK (resolver.resolve ("KICK 1.WAV").found);
    CHECK (resolver.resolve ("Kick 1").how == "extension");
    CHECK (resolver.resolve ("Snäre.wav").found);
    CHECK (resolver.resolve ("/media/sd/Some Folder/Hat Closed.wav").found);
    CHECK_FALSE (resolver.resolve ("does-not-exist.wav").found);
}

TEST_CASE ("path helpers")
{
    CHECK (sanitizeFileName ("a/b:c*?.wav") == "a_b_c__.wav");
    CHECK (sanitizeFileName ("CON") == "_CON");
    CHECK (sanitizeFileName ("  ") == "_");
    CHECK (midiNoteName (36) == "C1");
    CHECK (midiNoteName (60) == "C3");
    CHECK (isSameOrInside ("C:/a/b/c.txt", "C:/A/b"));
    CHECK_FALSE (isSameOrInside ("C:/a/bc", "C:/a/b"));
    CHECK (pathToUtf8 (pathFromUtf8 ("Grüße.wav")) == "Grüße.wav");
}
