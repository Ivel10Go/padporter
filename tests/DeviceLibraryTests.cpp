#include "Fixtures.h"

#include "padporter/DeviceLibrary.h"
#include "padporter/PathUtil.h"
#include "padporter/SampleResolver.h"
#include "padporter/XpjReader.h"

#include <doctest/doctest.h>

using namespace padporter;
namespace fs = std::filesystem;

namespace
{
/** Builds an SD card like the one written by the device: <root>/MPC-Sample/{Projects,Samples}. */
fs::path makeCard (const fs::path& root)
{
    const auto projects = root / "MPC-Sample" / "Projects";
    fixtures::createProjectOnDisk (projects, "Beat One", fixtures::sampleProjectJson());
    fixtures::createProjectOnDisk (projects, "alpha", fixtures::sampleProjectJson());
    fixtures::writeFile (root / "MPC-Sample" / "Samples" / "Kits" / "unused.wav", fixtures::makeWav (10, 1, 44100));
    fixtures::writeFile (root / "System Volume Information" / "x.xpj", { 1, 2, 3 });
    return projects;
}
} // namespace

TEST_CASE ("a device card is recognised and its projects are listed")
{
    fixtures::TempDir tmp;
    makeCard (tmp.path);

    const auto volume = inspectVolume (tmp.path);
    REQUIRE (volume.has_value());
    CHECK (volume->recognisedLayout);
    CHECK (volume->deviceName == "MPC-Sample");
    CHECK_FALSE (volume->samplesDir.empty());
    REQUIRE (volume->projects.size() == 2);
    CHECK (volume->projects[0].name == "alpha");          // sorted case-insensitively
    CHECK (volume->projects[1].name == "Beat One");
    CHECK (volume->projects[1].hasDataDir);
    CHECK (volume->projects[1].sampleFiles == 3);
    CHECK (volume->projects[1].modifiedUnix > 1600000000);
}

TEST_CASE ("generic volumes are searched, empty ones are ignored")
{
    fixtures::TempDir tmp;
    fixtures::createProjectOnDisk (tmp.path / "stick" / "Beats", "Loose", fixtures::sampleProjectJson());
    fs::create_directories (tmp.path / "empty" / "Music");

    const auto stick = inspectVolume (tmp.path / "stick");
    REQUIRE (stick.has_value());
    CHECK_FALSE (stick->recognisedLayout);
    CHECK (stick->projects.size() == 1);

    CHECK_FALSE (inspectVolume (tmp.path / "empty").has_value());
    CHECK_FALSE (inspectVolume (tmp.path / "does-not-exist").has_value());
}

TEST_CASE ("import copies the project with all samples and leaves the card untouched")
{
    fixtures::TempDir tmp;
    const auto card = tmp.path / "card";

    // One pad uses a sample from the device library that is not stored with the project.
    auto project = fixtures::sampleProjectJson();
    auto extra = project["data"]["tracks"][0]["program"]["drum"]["instruments"][0];
    extra["layersv"][0]["sampleFile"] = "Library Hit.wav";
    project["data"]["tracks"][0]["program"]["drum"]["instruments"][3] = extra;

    const auto projects = card / "MPC-Sample" / "Projects";
    const auto xpj = fixtures::createProjectOnDisk (projects, "Beat", project);
    fs::remove (projects / "Beat_[ProjectData]" / "Library Hit.wav");
    fixtures::writeFile (card / "MPC-Sample" / "Samples" / "Kits" / "Library Hit.wav", fixtures::makeWav (100, 1, 44100));

    const auto before = fixtures::snapshot ({ card });
    const auto library = tmp.path / "library";
    const auto result = importProject (xpj, library, { card / "MPC-Sample" / "Samples" });
    CHECK (fixtures::snapshot ({ card }) == before);

    CHECK (result.samplesRecovered == 1);
    CHECK (result.samplesMissing == 0);
    CHECK (result.filesCopied == 5);      // .xpj + 3 project samples + 1 library sample
    CHECK (result.projectFile == library / "Beat" / "Beat.xpj");
    CHECK (fs::exists (library / "Beat" / "Beat_[ProjectData]" / "Library Hit.wav"));

    const auto imported = XpjReader::load (result.projectFile);
    const SampleResolver resolver (imported.project);
    for (const auto& pad : imported.project.tracks[0].pads)
        for (const auto& layer : pad.layers)
            CHECK (resolver.resolve (layer.sampleFile).found);

    // Importing again replaces the earlier copy and leaves no staging folders behind.
    const auto again = importProject (xpj, library, { card / "MPC-Sample" / "Samples" });
    CHECK (again.replacedExisting);
    int entries = 0;
    for (const auto& e : fs::directory_iterator (library))
    {
        ++entries;
        CHECK (pathToUtf8 (e.path().filename()).rfind (".import-", 0) != 0);
    }
    CHECK (entries == 1);
    CHECK (findProjects (library, 1).size() == 1);
}

TEST_CASE ("missing samples are reported and import refuses unsafe targets")
{
    fixtures::TempDir tmp;
    const auto xpj = fixtures::createProjectOnDisk (tmp.path / "card", "Beat", fixtures::sampleProjectJson(), false);

    const auto result = importProject (xpj, tmp.path / "lib");
    CHECK (result.samplesMissing == 3);

    CHECK_THROWS_AS (importProject (xpj, tmp.path / "card" / "lib"), ImportError);
    CHECK_THROWS_AS (importProject (result.projectFile, tmp.path / "lib"), ImportError);
    CHECK_THROWS_AS (importProject (tmp.path / "nothing.xpj", tmp.path / "lib"), ImportError);
}
