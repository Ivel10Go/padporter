#pragma once

#include "Diagnostics.h"
#include "Model.h"
#include "Parts.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace padporter
{
struct ExportOptions
{
    bool copySamples = true;
    bool writeMidi = true;
    bool writeMapping = true;
    bool overwrite = false;     // allow writing into an existing, non-empty export folder
    bool splitParts = true;     // additionally write "- Drums" / "- Melodie" MIDI files per sequence

    /** Optional role per pad (e.g. user overrides); nullopt or unset = automatic classification. */
    std::function<std::optional<PartRole> (const Track&, const Pad&)> roleFor;
};

struct ExportResult
{
    std::filesystem::path outputDir;
    int samplesCopied = 0;
    int samplesMissing = 0;
    int midiFiles = 0;
    std::vector<std::filesystem::path> written;
    Diagnostics diagnostics;
};

/**
    Exports samples, one MIDI file per sequence and mapping.json into
    destRoot/<project name>/.

    Read-only guarantee: refuses (ExportError) if the output folder is inside the
    project's data folder or if any output file would replace a source file.
*/
ExportResult exportProject (const Project& project,
                            const std::filesystem::path& destRoot,
                            const ExportOptions& options = {},
                            const Diagnostics* loadDiagnostics = nullptr);
} // namespace padporter
