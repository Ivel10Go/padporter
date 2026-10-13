// padporter-cli - inspect and export .xpj projects from the command line.

#include "padporter/DeviceLibrary.h"
#include "padporter/Exporter.h"
#include "padporter/Parts.h"
#include "padporter/PathUtil.h"
#include "padporter/SampleResolver.h"
#include "padporter/Version.h"
#include "padporter/WavInfo.h"
#include "padporter/XpjReader.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
 #define NOMINMAX
 #include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace padporter;

namespace
{
void printUsage()
{
    std::cout << kProductName << " CLI " << kVersion << "\n\n"
              << "Usage:\n"
              << "  padporter-cli info <project.xpj | folder>   Show project contents and diagnostics\n"
              << "  padporter-cli header <project.xpj>          Show the file header\n"
              << "  padporter-cli dump-json <project.xpj> [out.json]\n"
              << "                                              Write the decompressed JSON (stdout by default)\n"
              << "  padporter-cli export <project.xpj> <target-folder> [--overwrite] [--no-samples] [--no-midi]\n"
              << "                                              Export samples, MIDI files and mapping.json\n"
              << "  padporter-cli scan <drive-or-folder>         Detect a device (SD card) and list its projects\n"
              << "  padporter-cli import <project.xpj> <library-folder> [--samples <device-sample-folder>]\n"
              << "                                              Copy a project with all samples into a library\n"
              << "  padporter-cli --version\n\n"
              << "Source projects are only ever read, never modified.\n";
}

void printDiagnostics (const Diagnostics& d)
{
    if (d.empty())
    {
        std::cout << "Diagnostics: none\n";
        return;
    }

    std::cout << "Diagnostics (" << d.count (Severity::Error) << " errors, " << d.count (Severity::Warning)
              << " warnings, " << d.count (Severity::Info) << " info):\n";
    for (const auto& item : d.items())
        std::cout << "  [" << toString (item.severity) << "] " << item.where << ": " << item.message << "\n";
}

void printProject (const LoadResult& r)
{
    const auto& p = r.project;
    const auto& h = p.header;
    std::error_code ec;

    std::cout << "Project:   " << p.name << "\n"
              << "File:      " << pathToUtf8 (p.xpjPath) << "\n"
              << "Header:    " << h.magic << " " << h.version << " (" << h.type << " / " << h.encoding << " / "
              << h.platform << (h.compressed ? ", gzip" : ", uncompressed") << ")\n"
              << "Samples:   " << pathToUtf8 (p.dataDir) << (fs::is_directory (p.dataDir, ec) ? "" : "  [MISSING]") << "\n";

    char tempo[64];
    std::snprintf (tempo, sizeof (tempo), "%.2f BPM (master tempo %s)", p.masterTempo, p.masterTempoEnabled ? "on" : "off");
    std::cout << "Tempo:     " << tempo << "\n";

    const auto& comp = p.masterCompressor;
    char master[160];
    std::snprintf (master, sizeof (master), "volume %.3f, Color Compressor %s%s", p.masterVolume,
                   ! comp.present ? "none" : (comp.active() ? "active" : (comp.bypassed ? "bypassed" : "disabled")),
                   comp.present && comp.colorOn ? " (Color on)" : "");
    std::cout << "Master:    " << master << "\n";

    std::cout << "Data keys: ";
    for (std::size_t i = 0; i < p.topLevelKeys.size(); ++i)
        std::cout << (i ? ", " : "") << p.topLevelKeys[i];
    std::cout << "\n\n";

    SampleResolver resolver (p);

    std::cout << "Tracks (" << p.tracks.size() << "):\n";
    for (const auto& t : p.tracks)
    {
        if (t.kind == TrackKind::Routing)
        {
            std::cout << "  [" << t.index + 1 << "] " << t.name << "  - " << t.programInfo << " bus\n";
            continue;
        }

        std::cout << "  [" << t.index + 1 << "] " << t.name << "  - "
                  << (t.kind == TrackKind::Drum ? "drum, " + std::to_string (t.loadedPadCount()) + " pads with samples"
                                                : "not supported (" + t.programInfo + ")")
                  << "\n";

        const auto roles = classifyTrack (p, t, resolver);

        for (const auto& pad : t.pads)
        {
            if (! pad.hasSample())
                continue;

            const auto& layer = pad.layers.front();
            const auto res = resolver.resolve (layer.sampleFile);

            std::string details;
            if (res.found)
            {
                const auto info = readWavInfo (res.path);
                char buf[96];
                if (info.valid)
                    std::snprintf (buf, sizeof (buf), "%d Hz/%d bit/%dch %.2fs%s", info.sampleRate, info.bitsPerSample,
                                   info.channels, info.durationSeconds(),
                                   info.slices.empty() ? "" : (" " + std::to_string (info.slices.size()) + " slices").c_str());
                else
                    std::snprintf (buf, sizeof (buf), "unreadable: %s", info.error.c_str());
                details = buf;
            }

            char line[64];
            std::snprintf (line, sizeof (line), "      %s %-4s(%3d) ", pad.label().c_str(), midiNoteName (pad.midiNote).c_str(), pad.midiNote);
            std::cout << line << (roles[static_cast<std::size_t> (pad.index)] == PartRole::Drums ? "DRUM    " : "MELODIE ")
                      << layer.sampleFile << "  [" << (pad.oneShot ? "one-shot" : "gate")
                      << (pad.muteGroup ? ", mute grp " + std::to_string (pad.muteGroup) : "")
                      << (pad.layers.size() > 1 ? ", " + std::to_string (pad.layers.size()) + " layers" : "") << "]  "
                      << (res.found ? "ok " + details : "MISSING") << "\n";
        }
    }

    std::cout << "\nSequences (" << p.sequences.size() << "):\n";
    for (std::size_t i = 0; i < p.sequences.size(); ++i)
    {
        const auto& s = p.sequences[i];
        char line[160];
        std::snprintf (line, sizeof (line), "  %02d %-20s %3d bars  %d/%d  %.2f BPM", s.number(), s.name.c_str(),
                       s.lengthBars, s.timeSignature.beatsPerBar, s.timeSignature.beatUnit, p.tempoFor (s));
        std::cout << line;
        for (const auto& c : s.clips)
        {
            const auto* t = p.findTrack (c.trackName);
            if (t != nullptr && t->kind == TrackKind::Routing)
                continue;
            std::cout << "  | " << c.trackName << ": " << c.playableNoteCount() << " notes";
            if (t != nullptr && t->kind == TrackKind::Drum)
            {
                const auto parts = countParts (c, *t, classifyTrack (p, *t, resolver));
                std::cout << " (drums " << parts.drums << ", melodie " << parts.melody;
                if (parts.unassigned > 0)
                    std::cout << ", ohne pad " << parts.unassigned;
                std::cout << ")";
            }
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    printDiagnostics (r.diagnostics);
}

int cmdInfo (const fs::path& target)
{
    std::error_code ec;
    if (! fs::is_directory (target, ec))
    {
        printProject (XpjReader::load (target));
        return 0;
    }

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it (target, ec), end; ! ec && it != end; it.increment (ec))
        if (it->is_regular_file() && asciiLower (pathToUtf8 (it->path().extension())) == ".xpj")
            files.push_back (it->path());

    std::sort (files.begin(), files.end());
    int failures = 0;

    for (const auto& f : files)
    {
        std::cout << "==================================================================\n";
        try
        {
            printProject (XpjReader::load (f));
        }
        catch (const std::exception& e)
        {
            ++failures;
            std::cout << pathToUtf8 (f) << "\n  ERROR: " << e.what() << "\n";
        }
        std::cout << "\n";
    }

    std::cout << files.size() << " project(s) found, " << failures << " failed.\n";
    return failures == 0 ? 0 : 1;
}

int cmdHeader (const fs::path& file)
{
    const auto raw = XpjReader::readRaw (file);
    for (std::size_t i = 0; i < raw.header.lines.size(); ++i)
        std::cout << "Line " << i + 1 << ": " << raw.header.lines[i] << "\n";
    std::cout << "Payload: " << (raw.header.compressed ? "gzip" : "plain") << ", " << raw.json.size() << " bytes JSON\n";
    return 0;
}

int cmdDumpJson (const fs::path& file, const fs::path* out)
{
    const auto raw = XpjReader::readRaw (file);

    // Pretty-print through the reader's JSON parser by re-parsing a full load is unnecessary;
    // the raw text is already valid JSON. Indentation is added for readability.
    std::string pretty;
    pretty.reserve (raw.json.size() * 2);
    int depth = 0;
    bool inString = false, escaped = false;
    for (char c : raw.json)
    {
        if (inString)
        {
            pretty.push_back (c);
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') inString = false;
            continue;
        }
        switch (c)
        {
            case '"': inString = true; pretty.push_back (c); break;
            case '{': case '[': pretty.push_back (c); pretty += "\n" + std::string (static_cast<std::size_t> (++depth) * 2, ' '); break;
            case '}': case ']': pretty += "\n" + std::string (static_cast<std::size_t> (std::max (0, --depth)) * 2, ' '); pretty.push_back (c); break;
            case ',': pretty += ",\n" + std::string (static_cast<std::size_t> (depth) * 2, ' '); break;
            case ':': pretty += ": "; break;
            case ' ': case '\n': case '\r': case '\t': break;
            default: pretty.push_back (c);
        }
    }
    pretty.push_back ('\n');

    if (out == nullptr)
    {
        std::cout << pretty;
        return 0;
    }

    if (isSamePath (*out, file) || isSameOrInside (*out, projectDataDirFor (file)))
        throw ExportError ("Refusing to write into the source project");

    writeFileAtomically (*out, pretty.data(), pretty.size());
    std::cout << "Written: " << pathToUtf8 (*out) << "\n";
    return 0;
}

int cmdExport (const fs::path& file, const fs::path& dest, const std::vector<std::string>& flags)
{
    ExportOptions options;
    for (const auto& f : flags)
    {
        if (f == "--overwrite")       options.overwrite = true;
        else if (f == "--no-samples") options.copySamples = false;
        else if (f == "--no-midi")    options.writeMidi = false;
        else
        {
            std::cerr << "Unknown option: " << f << "\n";
            return 2;
        }
    }

    const auto loaded = XpjReader::load (file);
    const auto result = exportProject (loaded.project, dest, options, &loaded.diagnostics);

    std::cout << "Exported to: " << pathToUtf8 (result.outputDir) << "\n"
              << "  samples copied: " << result.samplesCopied << " (missing: " << result.samplesMissing << ")\n"
              << "  MIDI files:     " << result.midiFiles << "\n";

    Diagnostics all = loaded.diagnostics;
    all.append (result.diagnostics);
    printDiagnostics (all);
    return 0;
}

int cmdScan (const fs::path& root)
{
    const auto volume = inspectVolume (root);
    if (! volume)
    {
        std::cout << "No projects found on " << pathToUtf8 (root) << "\n";
        return 1;
    }

    std::cout << "Volume:   " << pathToUtf8 (volume->root) << "\n"
              << "Layout:   " << (volume->recognisedLayout ? "device folder '" + volume->deviceName + "'" : std::string ("generic")) << "\n"
              << "Projects: " << pathToUtf8 (volume->projectsDir) << "\n"
              << "Samples:  " << (volume->samplesDir.empty() ? std::string ("-") : pathToUtf8 (volume->samplesDir)) << "\n\n";

    for (const auto& e : volume->projects)
        std::cout << "  " << e.name << "  (" << e.sampleFiles << " sample files, " << e.sizeBytes / 1024 << " KB"
                  << (e.hasDataDir ? "" : ", no sample folder") << ")\n";
    std::cout << volume->projects.size() << " project(s)\n";
    return 0;
}

int cmdImport (const fs::path& xpj, const fs::path& library, const std::vector<std::string>& flags)
{
    ImportOptions options;
    for (std::size_t i = 0; i < flags.size(); ++i)
    {
        if (flags[i] == "--samples" && i + 1 < flags.size())
            options.sampleLibraryDir = pathFromUtf8 (flags[++i]);
        else
        {
            std::cerr << "Unknown option: " << flags[i] << "\n";
            return 2;
        }
    }

    // Default: the device layout keeps its sample library next to the Projects folder.
    if (options.sampleLibraryDir.empty())
    {
        const auto guess = xpj.parent_path().parent_path() / "Samples";
        std::error_code ec;
        if (fs::is_directory (guess, ec))
            options.sampleLibraryDir = guess;
    }

    const auto r = importProject (xpj, library, options);
    std::cout << "Imported to: " << pathToUtf8 (r.projectFile) << (r.replacedExisting ? "  (replaced earlier import)" : "") << "\n"
              << "  files copied:      " << r.filesCopied << " (" << r.bytesCopied / 1024 << " KB)\n"
              << "  from device lib.:  " << r.samplesRecovered << "\n"
              << "  still missing:     " << r.samplesMissing << "\n";
    printDiagnostics (r.diagnostics);
    return r.samplesMissing == 0 ? 0 : 1;
}

int run (const std::vector<std::string>& args)
{
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help")
    {
        printUsage();
        return args.empty() ? 2 : 0;
    }

    const auto& cmd = args[0];
    if (cmd == "--version")
    {
        std::cout << kProductName << " " << kVersion << "\n";
        return 0;
    }

    auto needArgs = [&] (std::size_t n) {
        if (args.size() < n)
        {
            printUsage();
            return false;
        }
        return true;
    };

    if (cmd == "info" && needArgs (2))
        return cmdInfo (pathFromUtf8 (args[1]));

    if (cmd == "header" && needArgs (2))
        return cmdHeader (pathFromUtf8 (args[1]));

    if (cmd == "dump-json" && needArgs (2))
    {
        if (args.size() > 2)
        {
            const auto out = pathFromUtf8 (args[2]);
            return cmdDumpJson (pathFromUtf8 (args[1]), &out);
        }
        return cmdDumpJson (pathFromUtf8 (args[1]), nullptr);
    }

    if (cmd == "export" && needArgs (3))
        return cmdExport (pathFromUtf8 (args[1]), pathFromUtf8 (args[2]), { args.begin() + 3, args.end() });

    if (cmd == "scan" && needArgs (2))
        return cmdScan (pathFromUtf8 (args[1]));

    if (cmd == "import" && needArgs (3))
        return cmdImport (pathFromUtf8 (args[1]), pathFromUtf8 (args[2]), { args.begin() + 3, args.end() });

    if (cmd != "info" && cmd != "header" && cmd != "dump-json" && cmd != "export" && cmd != "scan" && cmd != "import")
        std::cerr << "Unknown command: " << cmd << "\n\n";
    return 2;
}

int runSafely (const std::vector<std::string>& args)
{
    try
    {
        return run (args);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int wmain (int argc, wchar_t** argv)
{
    SetConsoleOutputCP (CP_UTF8);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back (pathToUtf8 (fs::path (argv[i])));
    return runSafely (args);
}
#else
int main (int argc, char** argv)
{
    return runSafely ({ argv + 1, argv + argc });
}
#endif
