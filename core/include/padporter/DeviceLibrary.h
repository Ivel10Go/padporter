#pragma once

#include "Diagnostics.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace padporter
{
/** One .xpj project found on a device or in the local library. */
struct ProjectEntry
{
    std::filesystem::path xpjPath;
    std::string name;
    std::uintmax_t sizeBytes = 0;
    std::int64_t modifiedUnix = 0;   // seconds since 1970-01-01 (UTC)
    int sampleFiles = 0;             // files in the "_[ProjectData]" folder
    bool hasDataDir = false;
};

/** A mounted volume (e.g. the device's SD card in "SD Card Access" mode) that contains projects. */
struct DeviceVolume
{
    std::filesystem::path root;          // e.g. "F:\"
    std::filesystem::path deviceDir;     // e.g. "F:\MPC-Sample" (empty for a generic volume)
    std::string deviceName;              // folder name of deviceDir
    std::filesystem::path projectsDir;   // e.g. "F:\MPC-Sample\Projects"
    std::filesystem::path samplesDir;    // device sample library, e.g. "F:\MPC-Sample\Samples" (may be empty)
    bool recognisedLayout = false;       // "<device>/Projects" structure found
    std::vector<ProjectEntry> projects;
};

/** Lists .xpj files below 'dir' (sample folders are skipped). Sorted by name. */
std::vector<ProjectEntry> findProjects (const std::filesystem::path& dir, int maxDepth = 2);

/**
    Inspects a volume root. Recognises the device layout "<root>/MPC.../Projects" and falls back
    to a shallow search for .xpj files. Returns nullopt if the volume holds no projects.
    Only reads from the volume.
*/
std::optional<DeviceVolume> inspectVolume (const std::filesystem::path& root);

struct ImportOptions
{
    /** Searched for samples the project references but that are missing from its own folder. */
    std::filesystem::path sampleLibraryDir;
};

struct ImportResult
{
    std::filesystem::path projectFile;   // imported .xpj inside the library
    std::filesystem::path projectDir;
    int filesCopied = 0;
    int samplesRecovered = 0;            // copied from the device sample library
    int samplesMissing = 0;
    std::uintmax_t bytesCopied = 0;
    bool replacedExisting = false;
    Diagnostics diagnostics;
};

/**
    Copies a project and all of its samples into libraryDir/<project name>/.
    The source (e.g. the SD card) is only read. An earlier import of the same project is replaced
    atomically (the new copy is assembled in a staging folder first). Throws ImportError/ParseError.
*/
ImportResult importProject (const std::filesystem::path& sourceXpj,
                            const std::filesystem::path& libraryDir,
                            const ImportOptions& options = {});
} // namespace padporter
