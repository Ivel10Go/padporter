#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace padporter
{
struct WavSlice
{
    std::int64_t start = 0;
    std::int64_t end = 0;
};

/** Header information of a RIFF/WAVE file, including the proprietary "atem" chunk. */
struct WavInfo
{
    bool valid = false;
    std::string error;
    std::uint16_t formatTag = 0;    // 1 = PCM, 3 = float, 0xFFFE = extensible
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    std::int64_t frames = 0;
    std::uint64_t fileSize = 0;
    std::vector<WavSlice> slices;   // from "atem" chunk (manual chop points)
    std::string atemJson;           // raw "atem" payload, empty if absent

    double durationSeconds() const noexcept
    {
        return sampleRate > 0 ? static_cast<double> (frames) / sampleRate : 0.0;
    }
};

/** Reads only the chunk headers (and small metadata chunks), never the audio data. */
WavInfo readWavInfo (const std::filesystem::path& file);

/** Same as readWavInfo but on an in-memory file. */
WavInfo parseWavInfo (const std::uint8_t* data, std::size_t size);

/** Parses the JSON of an "atem" chunk into slices. Returns an empty list if unknown. */
std::vector<WavSlice> parseAtemSlices (const std::string& json);
} // namespace padporter
