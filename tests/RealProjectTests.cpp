// Runs the core library against the user's real projects in ./testprojekte (if present).

#include "Fixtures.h"

#include "padporter/Exporter.h"
#include "padporter/PathUtil.h"
#include "padporter/XpjReader.h"

#include <doctest/doctest.h>

using namespace padporter;
namespace fs = std::filesystem;

TEST_CASE ("real test projects load and export without modifying originals")
{
    const fs::path root = pathFromUtf8 (PADPORTER_TESTPROJECTS_DIR);
    std::error_code ec;
    if (! fs::is_directory (root, ec))
    {
        MESSAGE ("No ./testprojekte folder - real project tests skipped.");
        return;
    }

    std::vector<fs::path> projects;
    for (fs::recursive_directory_iterator it (root, ec), end; ! ec && it != end; it.increment (ec))
        if (it->is_regular_file() && asciiLower (pathToUtf8 (it->path().extension())) == ".xpj")
            projects.push_back (it->path());

    if (projects.empty())
        MESSAGE ("./testprojekte contains no .xpj files.");

    for (const auto& xpj : projects)
    {
        CAPTURE (pathToUtf8 (xpj));

        LoadResult loaded;
        REQUIRE_NOTHROW (loaded = XpjReader::load (xpj));
        CHECK_FALSE (loaded.project.tracks.empty());

        const auto before = fixtures::snapshot ({ xpj, loaded.project.dataDir });

        fixtures::TempDir out;
        ExportResult result;
        CHECK_NOTHROW (result = exportProject (loaded.project, out.path, {}, &loaded.diagnostics));

        CHECK (fixtures::snapshot ({ xpj, loaded.project.dataDir }) == before);
        MESSAGE (pathToUtf8 (xpj.filename()) << ": " << loaded.project.tracks.size() << " tracks, "
                                             << loaded.project.sequences.size() << " sequences, "
                                             << result.samplesCopied << " samples, " << result.samplesMissing << " missing, "
                                             << loaded.diagnostics.items().size() << " diagnostics");
    }
}
