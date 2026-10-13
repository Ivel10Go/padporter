#pragma once

#include "Model.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace padporter
{
struct ResolvedSample
{
    std::string sampleFile;         // as referenced by the project
    std::filesystem::path path;     // resolved location (valid if found)
    bool found = false;
    std::string how;                // "direct", "case-insensitive", "extension", "project folder"
};

/** Finds sample files referenced by a project inside its "_[ProjectData]" folder. */
class SampleResolver
{
public:
    explicit SampleResolver (const Project& project);

    ResolvedSample resolve (const std::string& sampleFile) const;

private:
    std::filesystem::path dataDir_;
    std::filesystem::path xpjDir_;
    std::unordered_map<std::string, std::filesystem::path> byName_;   // lower-case file name
    std::unordered_map<std::string, std::filesystem::path> byStem_;   // lower-case stem
};
} // namespace padporter
