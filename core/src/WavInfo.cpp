#include "padporter/WavInfo.h"
#include "padporter/PathUtil.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace padporter
{
namespace
{
constexpr std::uint32_t kMaxAtemBytes = 1024 * 1024;

std::uint32_t le32 (const std::uint8_t* p)
{
    return static_cast<std::uint32_t> (p[0]) | static_cast<std::uint32_t> (p[1]) << 8
         | static_cast<std::uint32_t> (p[2]) << 16 | static_cast<std::uint32_t> (p[3]) << 24;
}

std::uint16_t le16 (const std::uint8_t* p)
{
    return static_cast<std::uint16_t> (p[0] | (p[1] << 8));
}

/** Minimal random-access source so the same parser works on files and memory. */
struct Source
{
    virtual ~Source() = default;
    virtual std::uint64_t size() const = 0;
    virtual bool read (std::uint64_t offset, void* dest, std::size_t n) = 0;
};

struct MemorySource final : Source
{
    MemorySource (const std::uint8_t* d, std::size_t s) : data (d), length (s) {}
    std::uint64_t size() const override { return length; }
    bool read (std::uint64_t offset, void* dest, std::size_t n) override
    {
        if (offset + n > length)
            return false;
        std::memcpy (dest, data + offset, n);
        return true;
    }
    const std::uint8_t* data;
    std::size_t length;
};

struct FileSource final : Source
{
    explicit FileSource (const fs::path& p) : stream (p, std::ios::binary | std::ios::in)
    {
        std::error_code ec;
        length = fs::file_size (p, ec);
        if (ec)
            length = 0;
    }
    std::uint64_t size() const override { return length; }
    bool read (std::uint64_t offset, void* dest, std::size_t n) override
    {
        if (! stream || offset + n > length)
            return false;
        stream.seekg (static_cast<std::streamoff> (offset));
        return static_cast<bool> (stream.read (static_cast<char*> (dest), static_cast<std::streamsize> (n)));
    }
    std::ifstream stream;
    std::uint64_t length = 0;
};

WavInfo parse (Source& src)
{
    WavInfo info;
    info.fileSize = src.size();

    std::uint8_t riff[12];
    if (! src.read (0, riff, sizeof (riff)))
    {
        info.error = "file too small";
        return info;
    }

    if (std::memcmp (riff, "RIFF", 4) != 0 || std::memcmp (riff + 8, "WAVE", 4) != 0)
    {
        info.error = std::memcmp (riff, "RF64", 4) == 0 ? "RF64 files are not supported" : "not a RIFF/WAVE file";
        return info;
    }

    std::uint64_t pos = 12;
    std::uint64_t dataBytes = 0;
    int blockAlign = 0;
    bool haveFmt = false, haveData = false;

    while (pos + 8 <= src.size())
    {
        std::uint8_t header[8];
        if (! src.read (pos, header, sizeof (header)))
            break;

        const auto chunkSize = static_cast<std::uint64_t> (le32 (header + 4));
        const auto body = pos + 8;
        const auto available = std::min<std::uint64_t> (chunkSize, src.size() - body);

        if (std::memcmp (header, "fmt ", 4) == 0 && available >= 16)
        {
            std::uint8_t fmt[16];
            if (src.read (body, fmt, sizeof (fmt)))
            {
                info.formatTag = le16 (fmt);
                info.channels = le16 (fmt + 2);
                info.sampleRate = static_cast<int> (le32 (fmt + 4));
                blockAlign = le16 (fmt + 12);
                info.bitsPerSample = le16 (fmt + 14);
                haveFmt = true;
            }
        }
        else if (std::memcmp (header, "data", 4) == 0)
        {
            dataBytes = available; // clamp broken sizes (e.g. 0xFFFFFFFF) to the file
            haveData = true;
        }
        else if (std::memcmp (header, "atem", 4) == 0 && available > 0 && available <= kMaxAtemBytes)
        {
            std::string text (static_cast<std::size_t> (available), '\0');
            if (src.read (body, text.data(), text.size()))
            {
                text.erase (std::remove (text.begin(), text.end(), '\0'), text.end());
                info.atemJson = text;
                info.slices = parseAtemSlices (text);
            }
        }

        pos = body + chunkSize + (chunkSize & 1);
    }

    if (! haveFmt || ! haveData)
    {
        info.error = haveFmt ? "no data chunk" : "no fmt chunk";
        return info;
    }

    if (blockAlign <= 0)
        blockAlign = std::max (1, info.channels * std::max (1, info.bitsPerSample / 8));

    info.frames = static_cast<std::int64_t> (dataBytes / static_cast<std::uint64_t> (blockAlign));
    info.valid = info.channels > 0 && info.sampleRate > 0;
    if (! info.valid)
        info.error = "invalid format header";

    return info;
}
} // namespace

std::vector<WavSlice> parseAtemSlices (const std::string& text)
{
    std::vector<WavSlice> slices;

    const auto j = nlohmann::json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object())
        return slices;

    const auto it = j.find ("value0");
    const auto& v = (it != j.end() && it->is_object()) ? *it : j;

    const auto num = v.find ("Num slices");
    if (num == v.end() || ! num->is_number())
        return slices;

    const int count = std::clamp (num->get<int>(), 0, 4096);
    for (int i = 0; i < count; ++i)
    {
        const auto s = v.find ("Slice " + std::to_string (i));
        if (s == v.end() || ! s->is_object())
            continue;

        WavSlice slice;
        if (auto st = s->find ("Start"); st != s->end() && st->is_number())
            slice.start = st->get<std::int64_t>();
        if (auto en = s->find ("End"); en != s->end() && en->is_number())
            slice.end = en->get<std::int64_t>();
        slices.push_back (slice);
    }
    return slices;
}

WavInfo readWavInfo (const fs::path& file)
{
    FileSource src (file);
    if (! src.stream)
    {
        WavInfo info;
        info.error = "cannot open " + pathToUtf8 (file);
        return info;
    }
    return parse (src);
}

WavInfo parseWavInfo (const std::uint8_t* data, std::size_t size)
{
    MemorySource src (data, size);
    return parse (src);
}
} // namespace padporter
