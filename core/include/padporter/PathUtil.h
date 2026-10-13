#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace padporter
{
/** Converts a UTF-8 string to a path (Unicode-safe on Windows). */
std::filesystem::path pathFromUtf8 (const std::string& utf8);

/** Converts a path to a UTF-8 string. */
std::string pathToUtf8 (const std::filesystem::path& p);

/** Lower-cases ASCII letters only (enough for FAT/NTFS-style case-insensitive matching). */
std::string asciiLower (std::string s);

/** Replaces characters that are not allowed in Windows file names. Never returns an empty string. */
std::string sanitizeFileName (const std::string& name);

/** True if 'child' equals 'parent' or lies inside it (case-insensitive, after normalisation). */
bool isSameOrInside (const std::filesystem::path& child, const std::filesystem::path& parent);

/** True if both paths refer to the same location (case-insensitive, after normalisation). */
bool isSamePath (const std::filesystem::path& a, const std::filesystem::path& b);

/** Reads a whole file in binary read-only mode. Throws ParseError on failure or if larger than maxBytes. */
std::vector<std::uint8_t> readFileBytes (const std::filesystem::path& file,
                                         std::uint64_t maxBytes = 512ull * 1024 * 1024);

/** Writes data to 'file' via a temporary file + rename. Throws ExportError on failure. */
void writeFileAtomically (const std::filesystem::path& file, const void* data, std::size_t size);

/** MIDI note name using the Ableton convention (36 = C1). */
std::string midiNoteName (int note);
} // namespace padporter
