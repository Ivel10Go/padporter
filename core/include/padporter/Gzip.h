#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace padporter
{
/** Decompresses one gzip member. Throws ParseError on malformed or truncated data. */
std::vector<std::uint8_t> gunzip (const std::uint8_t* data, std::size_t size);

/** Compresses data as a single gzip member (used by tests and tooling). */
std::vector<std::uint8_t> gzip (const std::uint8_t* data, std::size_t size);
} // namespace padporter
