#include "Fixtures.h"

#include "padporter/Gzip.h"
#include "padporter/PathUtil.h"

#include <fstream>
#include <random>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace fixtures
{
namespace
{
json noteEvent (int tick, int note, double velocity, int length, bool muted = false)
{
    return { { "type", 3 }, { "time", tick }, { "muted", muted },
             { "note", { { "note", note }, { "velocity", velocity }, { "length", length } } } };
}

json pad (const std::string& file, int triggerMode, int muteGroup)
{
    return {
        { "triggerMode", triggerMode },
        { "whichMuteGroup", muteGroup },
        { "monophonic", false },
        { "mixable", { { "volume", 0.7079457640647888 }, { "pan", 0.5 }, { "mute", false } } },
        { "synthSection", { { "ampEnvelope", { { "Attack", { { "value0", 0.0 } } }, { "Release", { { "value0", 0.1 } } },
                                               { "OneShot", { { "value0", false } } } } } } },
        { "layersv", json::array ({ { { "sampleFile", file }, { "sampleName", file.substr (0, file.find ('.')) },
                                      { "coarseTune", 2 }, { "fineTune", -10 },
                                      { "volume", { { "gainCoefficient", 1.0 } } }, { "pan", 0.5 },
                                      { "sliceInfo", { { "Start", 0 }, { "End", 0 }, { "LoopStart", 0 } } } } }) },
    };
}
} // namespace

json sampleProjectJson()
{
    json instruments = json::array();
    instruments.push_back (pad ("Kick 1.wav", 1, 0));
    instruments.push_back (pad ("Snäre.wav", 2, 0));          // non-ASCII name, gate mode
    instruments.push_back (pad ("Hat Closed.wav", 1, 1));
    instruments.push_back (json::object());                    // empty pad

    json noteMap = { { "value0", 37 }, { "value1", 36 }, { "value2", 42 }, { "value3", 39 } };

    json seq1Events = json::array ({ noteEvent (0, 37, 1.0, 240), noteEvent (960, 36, 0.5, 240),
                                     noteEvent (1920, 37, 0.8, 240), noteEvent (2880, 42, 0.25, 120, true),
                                     { { "type", 7 }, { "time", 0 } } });

    json seq2Events = json::array ({ noteEvent (0, 42, 0.6, 120),
                                     { { "type", 1 }, { "time", 0 }, { "invented", true },
                                       { "automation", { { "note", 36 }, { "parameter", 131 }, { "value", 0.0 } } } },
                                     { { "type", 2 }, { "time", 480 },
                                       { "automation", { { "note", 42 }, { "parameter", 518 }, { "value", 0.3 } } } } });

    auto sequence = [] (const std::string& name, double bpm, int bars, int beatsPerBar, int beatLength, const json& events) {
        return json {
            { "name", name }, { "bpm", bpm }, { "lengthBars", bars },
            { "timeSignatureTrack", { { "timeSignatures", json::array ({ { { "beatsPerBar", beatsPerBar }, { "beatLength", beatLength } } }) } } },
            { "trackClipMaps", json::array ({ json::array ({ { { "key", "Drum 001" }, { "value", { { "eventList", { { "events", events } } } } } } }) }) },
        };
    };

    return {
        { "data", {
            { "masterTempo", 93.5 },
            { "masterTempoEnabled", false },
            { "tracks", json::array ({ {
                { "name", "Drum 001" },
                { "volume", 1.0 },
                { "pan", 0.5 },
                { "samples", json::array ({ { { "path", "Kick 1.wav" }, { "loadImpl", 0 }, { "metadata", { { "rootNote", 60 } } } } }) },
                { "program", { { "padNoteMap", { { "noteForPad", noteMap } } }, { "drum", { { "instruments", instruments } } } } },
            }, {
                { "name", "Keys" },
                { "program", { { "type", 3 }, { "keygroup", json::object() } } },
            }, {
                { "name", "Submix 1" },
                { "program", { { "type", 8 } } },
            } }) },
            { "sequences", json::array ({
                { { "key", 1 }, { "value", sequence ("Verse", 120.0, 1, 3, 960, seq2Events) } },
                { { "key", 0 }, { "value", sequence ("Intro", 128.0, 2, 4, 960, seq1Events) } },
            }) },
        } },
    };
}

std::vector<std::uint8_t> makeXpj (const json& root, bool compressed, bool headerInsideGzip)
{
    const std::string header = "ACVS\n1.2.1.2\nSerialisableProjectData\njson\nLinux\n";
    const auto text = root.dump();

    if (compressed && headerInsideGzip)
    {
        const auto all = header + text;
        return padporter::gzip (reinterpret_cast<const std::uint8_t*> (all.data()), all.size());
    }

    std::vector<std::uint8_t> out (header.begin(), header.end());
    if (compressed)
    {
        const auto gz = padporter::gzip (reinterpret_cast<const std::uint8_t*> (text.data()), text.size());
        out.insert (out.end(), gz.begin(), gz.end());
    }
    else
    {
        out.insert (out.end(), text.begin(), text.end());
    }
    return out;
}

std::vector<std::uint8_t> makeWav (int frames, int channels, int sampleRate, const std::optional<std::string>& atemJson)
{
    std::vector<std::uint8_t> out;
    auto u32 = [&out] (std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back (static_cast<std::uint8_t> (v >> (8 * i))); };
    auto u16 = [&out] (std::uint16_t v) { out.push_back (static_cast<std::uint8_t> (v)); out.push_back (static_cast<std::uint8_t> (v >> 8)); };
    auto tag = [&out] (const char* t) { out.insert (out.end(), t, t + 4); };

    const auto dataBytes = static_cast<std::uint32_t> (frames * channels * 2);
    tag ("RIFF");
    u32 (0); // patched below
    tag ("WAVE");

    tag ("fmt ");
    u32 (16);
    u16 (1);
    u16 (static_cast<std::uint16_t> (channels));
    u32 (static_cast<std::uint32_t> (sampleRate));
    u32 (static_cast<std::uint32_t> (sampleRate * channels * 2));
    u16 (static_cast<std::uint16_t> (channels * 2));
    u16 (16);

    tag ("data");
    u32 (dataBytes);
    for (int i = 0; i < frames * channels; ++i)
        u16 (static_cast<std::uint16_t> ((i * 37) & 0x7fff));

    if (atemJson)
    {
        auto payload = *atemJson;
        payload.push_back ('\0');
        tag ("atem");
        u32 (static_cast<std::uint32_t> (payload.size()));
        out.insert (out.end(), payload.begin(), payload.end());
        if (payload.size() & 1)
            out.push_back (0);
    }

    const auto riffSize = static_cast<std::uint32_t> (out.size() - 8);
    for (int i = 0; i < 4; ++i)
        out[4 + static_cast<std::size_t> (i)] = static_cast<std::uint8_t> (riffSize >> (8 * i));
    return out;
}

void writeFile (const fs::path& file, const std::vector<std::uint8_t>& data)
{
    fs::create_directories (file.parent_path());
    std::ofstream out (file, std::ios::binary);
    out.write (reinterpret_cast<const char*> (data.data()), static_cast<std::streamsize> (data.size()));
}

fs::path createProjectOnDisk (const fs::path& root, const std::string& name, const json& project, bool withSamples)
{
    const auto xpj = root / padporter::pathFromUtf8 (name + ".xpj");
    writeFile (xpj, makeXpj (project));

    if (withSamples)
    {
        const auto dataDir = root / padporter::pathFromUtf8 (name + "_[ProjectData]");
        for (const auto& inst : project["data"]["tracks"][0]["program"]["drum"]["instruments"])
            if (inst.contains ("layersv"))
                for (const auto& layer : inst["layersv"])
                    writeFile (dataDir / padporter::pathFromUtf8 (layer["sampleFile"].get<std::string>()), makeWav (4410, 1, 44100));
    }
    return xpj;
}

std::map<std::string, FileState> snapshot (const std::vector<fs::path>& roots)
{
    std::map<std::string, FileState> result;

    auto add = [&result] (const fs::path& p) {
        FileState s;
        s.size = fs::file_size (p);
        s.time = fs::last_write_time (p);
        std::ifstream in (p, std::ios::binary);
        std::uint64_t h = 1469598103934665603ull;
        char c;
        while (in.get (c))
            h = (h ^ static_cast<unsigned char> (c)) * 1099511628211ull;
        s.hash = h;
        result[padporter::pathToUtf8 (p)] = s;
    };

    for (const auto& r : roots)
    {
        if (fs::is_regular_file (r))
            add (r);
        else if (fs::is_directory (r))
            for (const auto& e : fs::recursive_directory_iterator (r))
                if (e.is_regular_file())
                    add (e.path());
    }
    return result;
}

TempDir::TempDir()
{
    std::random_device rd;
    path = fs::temp_directory_path() / ("padporter-test-" + std::to_string (rd()) + std::to_string (rd()));
    fs::create_directories (path);
}

TempDir::~TempDir()
{
    std::error_code ec;
    fs::remove_all (path, ec);
}
} // namespace fixtures
