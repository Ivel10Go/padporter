#pragma once

#include "Diagnostics.h"
#include "Model.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace padporter
{
struct LoadResult
{
    Project project;
    Diagnostics diagnostics;
};

/** Header + decompressed JSON text of an .xpj file. */
struct RawPayload
{
    XpjHeader header;
    std::string json;
};

/** Reads .xpj project files. Source files are only ever opened read-only. */
class XpjReader
{
public:
    /** Reads and decodes a file without interpreting the JSON. Throws ParseError. */
    static RawPayload readRaw (const std::filesystem::path& xpjFile);

    /** Decodes the bytes of an .xpj file (header + gzip or plain JSON). Throws ParseError. */
    static RawPayload decode (const std::vector<std::uint8_t>& bytes);

    /** Loads and interprets a project. Throws ParseError if the file is unusable. */
    static LoadResult load (const std::filesystem::path& xpjFile);

    /** Interprets an already decoded payload. 'xpjFile' is used for names and sample lookup. */
    static LoadResult parse (const RawPayload& payload, const std::filesystem::path& xpjFile);
};

/** "<dir>/<stem>_[ProjectData]" for a given .xpj path. */
std::filesystem::path projectDataDirFor (const std::filesystem::path& xpjFile);
} // namespace padporter
