#include "padporter/DeviceLibrary.h"
#include "padporter/PathUtil.h"
#include "padporter/SampleResolver.h"
#include "padporter/XpjReader.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

namespace padporter
{
namespace
{
constexpr std::size_t kMaxIndexedLibraryFiles = 500000;

bool isSkippedDirectory (const fs::path& p)
{
    const auto name = asciiLower (pathToUtf8 (p.filename()));
    if (name.empty())
        return false;
    if (name.front() == '.' || name.front() == '$')
        return true;
    if (name == "system volume information" || name == "lost.dir")
        return true;
    return name.ends_with ("_[projectdata]");
}

std::int64_t toUnixSeconds (fs::file_time_type t)
{
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock> (t);
    return std::chrono::duration_cast<std::chrono::seconds> (sys.time_since_epoch()).count();
}

ProjectEntry makeEntry (const fs::path& xpj)
{
    ProjectEntry e;
    e.xpjPath = xpj;
    e.name = pathToUtf8 (xpj.stem());

    std::error_code ec;
    e.sizeBytes = fs::file_size (xpj, ec);
    if (ec)
        e.sizeBytes = 0;

    const auto time = fs::last_write_time (xpj, ec);
    if (! ec)
        e.modifiedUnix = toUnixSeconds (time);

    const auto dataDir = projectDataDirFor (xpj);
    if (fs::is_directory (dataDir, ec))
    {
        e.hasDataDir = true;
        for (fs::directory_iterator it (dataDir, ec), end; ! ec && it != end; it.increment (ec))
        {
            std::error_code e2;
            if (it->is_regular_file (e2))
                ++e.sampleFiles;
        }
    }
    return e;
}

fs::path findChildDirectory (const fs::path& dir, const std::string& lowerName)
{
    std::error_code ec;
    for (fs::directory_iterator it (dir, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
    {
        std::error_code e2;
        if (it->is_directory (e2) && asciiLower (pathToUtf8 (it->path().filename())) == lowerName)
            return it->path();
    }
    return {};
}
} // namespace

std::vector<ProjectEntry> findProjects (const fs::path& dir, int maxDepth)
{
    std::vector<ProjectEntry> out;
    std::error_code ec;
    if (! fs::is_directory (dir, ec))
        return out;

    for (fs::recursive_directory_iterator it (dir, fs::directory_options::skip_permission_denied, ec), end;
         ! ec && it != end; it.increment (ec))
    {
        const auto& p = it->path();
        std::error_code e2;

        if (it->is_directory (e2))
        {
            if (it.depth() >= maxDepth || isSkippedDirectory (p))
                it.disable_recursion_pending();
            continue;
        }

        if (pathToUtf8 (p.filename()).rfind ("._", 0) == 0) // macOS metadata files
            continue;
        if (asciiLower (pathToUtf8 (p.extension())) != ".xpj" || ! it->is_regular_file (e2))
            continue;

        out.push_back (makeEntry (p));
    }

    std::sort (out.begin(), out.end(), [] (const ProjectEntry& a, const ProjectEntry& b) {
        return asciiLower (a.name) < asciiLower (b.name);
    });
    return out;
}

std::optional<DeviceVolume> inspectVolume (const fs::path& root)
{
    std::error_code ec;
    if (! fs::is_directory (root, ec))
        return std::nullopt;

    DeviceVolume volume;
    volume.root = root;

    for (fs::directory_iterator it (root, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
    {
        std::error_code e2;
        if (! it->is_directory (e2))
            continue;

        const auto name = asciiLower (pathToUtf8 (it->path().filename()));
        if (name.rfind ("mpc", 0) != 0)
            continue;

        const auto projects = findChildDirectory (it->path(), "projects");
        if (projects.empty())
            continue;

        volume.deviceDir = it->path();
        volume.deviceName = pathToUtf8 (it->path().filename());
        volume.projectsDir = projects;
        volume.samplesDir = findChildDirectory (it->path(), "samples");
        volume.recognisedLayout = true;
        volume.projects = findProjects (projects, 2);
        return volume;
    }

    volume.projects = findProjects (root, 3);
    if (volume.projects.empty())
        return std::nullopt;

    volume.projectsDir = root;
    return volume;
}

ImportResult importProject (const fs::path& sourceXpj, const fs::path& libraryDir, const ImportOptions& options)
{
    std::error_code ec;
    if (! fs::is_regular_file (sourceXpj, ec))
        throw ImportError ("Project file not found: " + pathToUtf8 (sourceXpj));

    if (isSameOrInside (libraryDir, sourceXpj.parent_path()))
        throw ImportError ("The library folder must not be inside the source project folder");
    if (isSameOrInside (sourceXpj, libraryDir))
        throw ImportError ("This project is already in the library");

    // Validates the file before anything is copied (throws ParseError).
    const auto loaded = XpjReader::load (sourceXpj);

    const auto sourceData = projectDataDirFor (sourceXpj);
    const auto targetDir = libraryDir / sourceXpj.stem();

    fs::create_directories (libraryDir, ec);
    if (ec)
        throw ImportError ("Cannot create library folder: " + pathToUtf8 (libraryDir) + " (" + ec.message() + ")");

    std::random_device rd;
    const auto staging = libraryDir / pathFromUtf8 (".import-" + std::to_string (rd()) + std::to_string (rd()));

    ImportResult result;

    try
    {
        fs::create_directories (staging);

        auto copyVerified = [&result] (const fs::path& from, const fs::path& to) {
            fs::create_directories (to.parent_path());
            fs::copy_file (from, to, fs::copy_options::overwrite_existing);
            const auto size = fs::file_size (to);
            if (size != fs::file_size (from))
                throw ImportError ("Copy verification failed: " + pathToUtf8 (from));
            result.bytesCopied += size;
            ++result.filesCopied;
        };

        const auto stagedXpj = staging / sourceXpj.filename();
        copyVerified (sourceXpj, stagedXpj);

        const auto stagedData = projectDataDirFor (stagedXpj);
        fs::create_directories (stagedData);

        if (fs::is_directory (sourceData, ec))
        {
            for (fs::recursive_directory_iterator it (sourceData, fs::directory_options::skip_permission_denied), end; it != end; ++it)
                if (it->is_regular_file())
                    copyVerified (it->path(), stagedData / fs::relative (it->path(), sourceData));
        }
        else
        {
            result.diagnostics.warning ("import", "project has no sample folder: " + pathToUtf8 (sourceData));
        }

        // Samples referenced by the project but not stored with it (e.g. loaded from the
        // device library) are looked up in the device sample library and copied in.
        Project staged = loaded.project;
        staged.xpjPath = stagedXpj;
        staged.dataDir = stagedData;
        const SampleResolver resolver (staged);

        std::unordered_map<std::string, fs::path> libraryIndex;
        bool indexed = false;
        std::set<std::string> handled;

        for (const auto& track : loaded.project.tracks)
            for (const auto& pad : track.pads)
                for (const auto& layer : pad.layers)
                {
                    if (! handled.insert (layer.sampleFile).second || resolver.resolve (layer.sampleFile).found)
                        continue;

                    if (! indexed)
                    {
                        indexed = true;
                        if (! options.sampleLibraryDir.empty() && fs::is_directory (options.sampleLibraryDir, ec))
                            for (fs::recursive_directory_iterator it (options.sampleLibraryDir, fs::directory_options::skip_permission_denied, ec), end;
                                 ! ec && it != end && libraryIndex.size() < kMaxIndexedLibraryFiles; it.increment (ec))
                            {
                                std::error_code e2;
                                if (it->is_regular_file (e2))
                                    libraryIndex.emplace (asciiLower (pathToUtf8 (it->path().filename())), it->path());
                            }
                    }

                    const auto key = asciiLower (pathToUtf8 (pathFromUtf8 (layer.sampleFile).filename()));
                    if (const auto it = libraryIndex.find (key); it != libraryIndex.end())
                    {
                        copyVerified (it->second, stagedData / it->second.filename());
                        ++result.samplesRecovered;
                        result.diagnostics.info ("import", "sample taken from the device library: " + pathToUtf8 (it->second));
                    }
                    else
                    {
                        ++result.samplesMissing;
                        result.diagnostics.warning ("import", "sample not found on the device: " + layer.sampleFile);
                    }
                }

        if (fs::exists (targetDir, ec))
        {
            if (! isSameOrInside (targetDir, libraryDir) || isSamePath (targetDir, libraryDir))
                throw ImportError ("Unexpected target folder: " + pathToUtf8 (targetDir));
            fs::remove_all (targetDir);
            result.replacedExisting = true;
        }

        fs::rename (staging, targetDir);
    }
    catch (...)
    {
        fs::remove_all (staging, ec);
        throw;
    }

    result.projectDir = targetDir;
    result.projectFile = targetDir / sourceXpj.filename();
    return result;
}
} // namespace padporter
