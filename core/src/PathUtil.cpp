#include "padporter/PathUtil.h"
#include "padporter/Diagnostics.h"

#include <array>
#include <fstream>
#include <random>

namespace fs = std::filesystem;

namespace padporter
{
fs::path pathFromUtf8 (const std::string& utf8)
{
    return fs::path (std::u8string (utf8.begin(), utf8.end()));
}

std::string pathToUtf8 (const fs::path& p)
{
    const auto u = p.u8string();
    return std::string (u.begin(), u.end());
}

std::string asciiLower (std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char> (c - 'A' + 'a');
    return s;
}

std::string sanitizeFileName (const std::string& name)
{
    std::string out;
    out.reserve (name.size());

    for (unsigned char c : name)
    {
        const bool invalid = c < 32 || c == '<' || c == '>' || c == ':' || c == '"'
                          || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*';
        out.push_back (invalid ? '_' : static_cast<char> (c));
    }

    while (! out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();

    while (! out.empty() && out.front() == ' ')
        out.erase (out.begin());

    if (out.empty())
        return "_";

    static const std::array<const char*, 22> reserved { "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9" };

    const auto stem = asciiLower (out.substr (0, out.find ('.')));
    for (auto* r : reserved)
        if (stem == r)
            return "_" + out;

    return out;
}

static std::vector<std::string> normalisedElements (const fs::path& p)
{
    std::error_code ec;
    auto absolute = fs::absolute (p, ec);
    if (ec)
        absolute = p;

    auto canonical = fs::weakly_canonical (absolute, ec);
    if (ec)
        canonical = absolute.lexically_normal();

    std::vector<std::string> parts;
    for (const auto& element : canonical)
    {
        auto s = asciiLower (pathToUtf8 (element));
        if (! s.empty() && s != "/" && s != "\\")
            parts.push_back (std::move (s));
    }
    return parts;
}

bool isSameOrInside (const fs::path& child, const fs::path& parent)
{
    const auto c = normalisedElements (child);
    const auto p = normalisedElements (parent);

    if (p.empty() || c.size() < p.size())
        return false;

    for (std::size_t i = 0; i < p.size(); ++i)
        if (c[i] != p[i])
            return false;

    return true;
}

bool isSamePath (const fs::path& a, const fs::path& b)
{
    return normalisedElements (a) == normalisedElements (b);
}

std::vector<std::uint8_t> readFileBytes (const fs::path& file, std::uint64_t maxBytes)
{
    std::error_code ec;
    const auto size = fs::file_size (file, ec);
    if (ec)
        throw ParseError ("Cannot read file: " + pathToUtf8 (file) + " (" + ec.message() + ")");

    if (size > maxBytes)
        throw ParseError ("File is unexpectedly large: " + pathToUtf8 (file));

    std::ifstream in (file, std::ios::binary | std::ios::in);
    if (! in)
        throw ParseError ("Cannot open file: " + pathToUtf8 (file));

    std::vector<std::uint8_t> bytes (static_cast<std::size_t> (size));
    if (size > 0 && ! in.read (reinterpret_cast<char*> (bytes.data()), static_cast<std::streamsize> (size)))
        throw ParseError ("Cannot read file: " + pathToUtf8 (file));

    return bytes;
}

void writeFileAtomically (const fs::path& file, const void* data, std::size_t size)
{
    static thread_local std::mt19937 rng { std::random_device {}() };
    auto tmp = file;
    tmp += ".tmp" + std::to_string (rng() % 100000);

    {
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        if (! out)
            throw ExportError ("Cannot create file: " + pathToUtf8 (tmp));

        if (size > 0)
            out.write (static_cast<const char*> (data), static_cast<std::streamsize> (size));

        if (! out)
            throw ExportError ("Cannot write file: " + pathToUtf8 (tmp));
    }

    std::error_code ec;
    fs::rename (tmp, file, ec);
    if (ec)
    {
        fs::remove (tmp, ec);
        throw ExportError ("Cannot move file into place: " + pathToUtf8 (file));
    }
}

std::string midiNoteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127)
        return "?";
    return std::string (names[note % 12]) + std::to_string (note / 12 - 2);
}
} // namespace padporter
