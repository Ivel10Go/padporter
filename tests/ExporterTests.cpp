#include "Fixtures.h"

#include "padporter/Exporter.h"
#include "padporter/PathUtil.h"
#include "padporter/XpjReader.h"

#include <doctest/doctest.h>

#include <fstream>

using namespace padporter;
namespace fs = std::filesystem;

TEST_CASE ("export writes samples, MIDI and mapping without touching the sources")
{
    fixtures::TempDir tmp;
    const auto xpj = fixtures::createProjectOnDisk (tmp.path / "source", "Beat", fixtures::sampleProjectJson());
    const auto loaded = XpjReader::load (xpj);

    const auto before = fixtures::snapshot ({ tmp.path / "source" });
    const auto result = exportProject (loaded.project, tmp.path / "out", {}, &loaded.diagnostics);
    const auto after = fixtures::snapshot ({ tmp.path / "source" });

    CHECK (before == after);
    CHECK (result.samplesCopied == 3);
    CHECK (result.samplesMissing == 0);
    CHECK (result.midiFiles == 4);   // per sequence: all notes + drums part (no melodic pads in the fixture)
    CHECK (fs::exists (tmp.path / "out" / "Beat" / "MIDI" / "01 - Intro - Drums.mid"));
    CHECK_FALSE (fs::exists (tmp.path / "out" / "Beat" / "MIDI" / "01 - Intro - Melodie.mid"));

    const auto out = tmp.path / "out" / "Beat";
    CHECK (fs::exists (out / "Samples" / "Kick 1.wav"));
    CHECK (fs::exists (out / "Samples" / pathFromUtf8 ("Snäre.wav")));
    CHECK (fs::exists (out / "MIDI" / "01 - Intro.mid"));
    CHECK (fs::exists (out / "MIDI" / "02 - Verse.mid"));
    CHECK (fs::exists (out / "export-log.txt"));

    std::ifstream in (out / "mapping.json");
    const auto mapping = nlohmann::json::parse (in);
    CHECK (mapping["format"] == "padporter-mapping");
    CHECK (mapping["tracks"][0]["pads"].size() == 3);
    CHECK (mapping["tracks"][0]["pads"][0]["midiNote"] == 37);
    CHECK (mapping["tracks"][0]["pads"][0]["layers"][0]["exportedFile"] == "Samples/Kick 1.wav");
    CHECK (mapping["sequences"][0]["midiFile"] == "MIDI/01 - Intro.mid");
}

TEST_CASE ("export refuses to write into the source project")
{
    fixtures::TempDir tmp;
    const auto xpj = fixtures::createProjectOnDisk (tmp.path, "Beat", fixtures::sampleProjectJson());
    const auto loaded = XpjReader::load (xpj);
    const auto before = fixtures::snapshot ({ tmp.path });

    CHECK_THROWS_AS (exportProject (loaded.project, loaded.project.dataDir), ExportError);
    CHECK_THROWS_AS (exportProject (loaded.project, loaded.project.dataDir / "sub"), ExportError);

    CHECK (fixtures::snapshot ({ tmp.path }) == before);
}

TEST_CASE ("export refuses a non-empty target unless overwrite is set")
{
    fixtures::TempDir tmp;
    const auto xpj = fixtures::createProjectOnDisk (tmp.path / "src", "Beat", fixtures::sampleProjectJson());
    const auto loaded = XpjReader::load (xpj);

    exportProject (loaded.project, tmp.path / "out");
    CHECK_THROWS_AS (exportProject (loaded.project, tmp.path / "out"), ExportError);

    ExportOptions overwrite;
    overwrite.overwrite = true;
    CHECK_NOTHROW (exportProject (loaded.project, tmp.path / "out", overwrite));
}

TEST_CASE ("missing samples are reported, not fatal")
{
    fixtures::TempDir tmp;
    const auto xpj = fixtures::createProjectOnDisk (tmp.path / "src", "Beat", fixtures::sampleProjectJson(), false);
    const auto loaded = XpjReader::load (xpj);
    const auto result = exportProject (loaded.project, tmp.path / "out");

    CHECK (result.samplesMissing == 3);
    CHECK (result.samplesCopied == 0);
    CHECK (result.diagnostics.count (Severity::Warning) == 3);
}
