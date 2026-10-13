#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fixtures
{
/** A synthetic project shaped like the documented format: 1 drum track, 3 pads, 2 sequences. */
nlohmann::json sampleProjectJson();

/**
    Serialises JSON into the .xpj container.
    Default = device layout: header and JSON compressed together in one gzip stream.
    headerInsideGzip = false: plain-text header followed by gzip (older layout).
    compressed = false: plain header + JSON.
*/
std::vector<std::uint8_t> makeXpj (const nlohmann::json& root, bool compressed = true, bool headerInsideGzip = true);

/** Creates a 16-bit PCM WAV file in memory, optionally with an "atem" chunk. */
std::vector<std::uint8_t> makeWav (int frames, int channels, int sampleRate,
                                   const std::optional<std::string>& atemJson = std::nullopt);

void writeFile (const std::filesystem::path& file, const std::vector<std::uint8_t>& data);

/** Writes <root>/<name>.xpj and <root>/<name>_[ProjectData]/ with the referenced samples. */
std::filesystem::path createProjectOnDisk (const std::filesystem::path& root, const std::string& name,
                                           const nlohmann::json& project, bool withSamples = true);

/** Size, timestamp and content hash of every file below a folder (or of a single file). */
struct FileState
{
    std::uintmax_t size = 0;
    std::filesystem::file_time_type time {};
    std::uint64_t hash = 0;
    bool operator== (const FileState&) const = default;
};
std::map<std::string, FileState> snapshot (const std::vector<std::filesystem::path>& roots);

/** Unique temporary folder, removed on destruction. */
struct TempDir
{
    TempDir();
    ~TempDir();
    TempDir (const TempDir&) = delete;
    TempDir& operator= (const TempDir&) = delete;
    std::filesystem::path path;
};
} // namespace fixtures
