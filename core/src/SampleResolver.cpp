#include "padporter/SampleResolver.h"
#include "padporter/PathUtil.h"

namespace fs = std::filesystem;

namespace padporter
{
SampleResolver::SampleResolver (const Project& project)
    : dataDir_ (project.dataDir), xpjDir_ (project.xpjPath.parent_path())
{
    std::error_code ec;
    if (! fs::is_directory (dataDir_, ec))
        return;

    // Sample folders are flat in all known projects; one extra level is tolerated.
    for (fs::recursive_directory_iterator it (dataDir_, fs::directory_options::skip_permission_denied, ec), end;
         ! ec && it != end; it.increment (ec))
    {
        if (it.depth() > 1)
        {
            it.disable_recursion_pending();
            continue;
        }

        if (! it->is_regular_file (ec))
            continue;

        const auto& p = it->path();
        byName_.emplace (asciiLower (pathToUtf8 (p.filename())), p);
        byStem_.emplace (asciiLower (pathToUtf8 (p.stem())), p);
    }
}

ResolvedSample SampleResolver::resolve (const std::string& sampleFile) const
{
    ResolvedSample r;
    r.sampleFile = sampleFile;
    if (sampleFile.empty())
        return r;

    std::error_code ec;
    const auto ref = pathFromUtf8 (sampleFile);
    const auto fileName = ref.filename();

    auto accept = [&r] (const fs::path& p, const char* how) {
        r.path = p;
        r.found = true;
        r.how = how;
        return r;
    };

    if (ref.is_relative() && ! ref.has_root_name())
    {
        const auto direct = dataDir_ / ref;
        if (fs::is_regular_file (direct, ec))
            return accept (direct, "direct");
    }

    if (auto it = byName_.find (asciiLower (pathToUtf8 (fileName))); it != byName_.end())
        return accept (it->second, "case-insensitive");

    if (! fileName.has_extension())
        if (auto it = byStem_.find (asciiLower (pathToUtf8 (fileName))); it != byStem_.end())
            return accept (it->second, "extension");

    const auto besideProject = xpjDir_ / fileName;
    if (fs::is_regular_file (besideProject, ec))
        return accept (besideProject, "project folder");

    return r;
}
} // namespace padporter
