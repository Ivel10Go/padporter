#include "padporter/Gzip.h"
#include "padporter/Diagnostics.h"

#include <miniz.h>

#include <algorithm>
#include <array>

namespace padporter
{
namespace
{
constexpr std::size_t kMaxDecompressedBytes = 1024ull * 1024 * 1024;

void skipZeroTerminated (const std::uint8_t* data, std::size_t size, std::size_t& pos)
{
    while (pos < size && data[pos] != 0)
        ++pos;
    ++pos;
}
} // namespace

std::vector<std::uint8_t> gunzip (const std::uint8_t* data, std::size_t size)
{
    if (size < 18 || data[0] != 0x1f || data[1] != 0x8b)
        throw ParseError ("Not a gzip stream");

    if (data[2] != 8)
        throw ParseError ("Unsupported gzip compression method");

    const auto flags = data[3];
    std::size_t pos = 10;

    if (flags & 0x04) // FEXTRA
    {
        if (pos + 2 > size)
            throw ParseError ("Truncated gzip header");
        pos += 2 + static_cast<std::size_t> (data[pos] | (data[pos + 1] << 8));
    }
    if (flags & 0x08) skipZeroTerminated (data, size, pos); // FNAME
    if (flags & 0x10) skipZeroTerminated (data, size, pos); // FCOMMENT
    if (flags & 0x02) pos += 2;                             // FHCRC

    if (pos >= size)
        throw ParseError ("Truncated gzip header");

    const std::uint32_t isize = static_cast<std::uint32_t> (data[size - 4])
                              | static_cast<std::uint32_t> (data[size - 3]) << 8
                              | static_cast<std::uint32_t> (data[size - 2]) << 16
                              | static_cast<std::uint32_t> (data[size - 1]) << 24;

    std::vector<std::uint8_t> out;
    out.reserve (std::min<std::size_t> (isize, 256u * 1024 * 1024));

    mz_stream stream {};
    stream.next_in = data + pos;
    stream.avail_in = static_cast<unsigned int> (size - pos);

    if (mz_inflateInit2 (&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
        throw ParseError ("Cannot initialise decompressor");

    std::array<std::uint8_t, 65536> chunk {};

    for (;;)
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<unsigned int> (chunk.size());

        const int result = mz_inflate (&stream, MZ_NO_FLUSH);
        const auto produced = chunk.size() - stream.avail_out;
        out.insert (out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t> (produced));

        if (result == MZ_STREAM_END)
            break;

        if (result != MZ_OK)
        {
            mz_inflateEnd (&stream);
            throw ParseError (stream.avail_in == 0 ? "Truncated gzip data" : "Corrupt gzip data");
        }

        if (out.size() > kMaxDecompressedBytes)
        {
            mz_inflateEnd (&stream);
            throw ParseError ("Decompressed project is unexpectedly large");
        }
    }

    mz_inflateEnd (&stream);
    return out;
}

std::vector<std::uint8_t> gzip (const std::uint8_t* data, std::size_t size)
{
    std::vector<std::uint8_t> out { 0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff };

    mz_stream stream {};
    if (mz_deflateInit2 (&stream, MZ_DEFAULT_LEVEL, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 9, MZ_DEFAULT_STRATEGY) != MZ_OK)
        throw ParseError ("Cannot initialise compressor");

    stream.next_in = data;
    stream.avail_in = static_cast<unsigned int> (size);

    std::array<std::uint8_t, 65536> chunk {};

    for (;;)
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<unsigned int> (chunk.size());

        const int result = mz_deflate (&stream, MZ_FINISH);
        const auto produced = chunk.size() - stream.avail_out;
        out.insert (out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t> (produced));

        if (result == MZ_STREAM_END)
            break;

        if (result != MZ_OK && result != MZ_BUF_ERROR)
        {
            mz_deflateEnd (&stream);
            throw ParseError ("Compression failed");
        }
    }

    mz_deflateEnd (&stream);

    const auto crc = static_cast<std::uint32_t> (mz_crc32 (MZ_CRC32_INIT, data, size));
    const auto len = static_cast<std::uint32_t> (size);

    for (int i = 0; i < 4; ++i) out.push_back (static_cast<std::uint8_t> (crc >> (8 * i)));
    for (int i = 0; i < 4; ++i) out.push_back (static_cast<std::uint8_t> (len >> (8 * i)));

    return out;
}
} // namespace padporter
