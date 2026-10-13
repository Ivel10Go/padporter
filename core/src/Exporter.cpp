#include "padporter/Exporter.h"
#include "padporter/MidiWriter.h"
#include "padporter/PathUtil.h"
#include "padporter/SampleResolver.h"
#include "padporter/Version.h"
#include "padporter/WavInfo.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace padporter
{
namespace
{
double gainToDb (double g)
{
    return g > 0.0 ? 20.0 * std::log10 (g) : -144.0;
}

class SourceGuard
{
public:
    explicit SourceGuard (const Project& p)
    {
        sources.push_back (p.xpjPath);
        dataDir = p.dataDir;
    }

    void addSource (const fs::path& p) { sources.push_back (p); }

    /** Throws if writing 'target' could modify any original file. */
    void check (const fs::path& target) const
    {
        if (! dataDir.empty() && isSameOrInside (target, dataDir))
            throw ExportError ("Refusing to write inside the project's sample folder: " + pathToUtf8 (target));

        for (const auto& s : sources)
            if (isSamePath (target, s))
                throw ExportError ("Refusing to overwrite a source file: " + pathToUtf8 (target));
    }

private:
    std::vector<fs::path> sources;
    fs::path dataDir;
};
} // namespace

ExportResult exportProject (const Project& project, const fs::path& destRoot,
                            const ExportOptions& options, const Diagnostics* loadDiagnostics)
{
    ExportResult result;
    auto& diag = result.diagnostics;

    const auto outDir = destRoot / pathFromUtf8 (sanitizeFileName (project.name));
    result.outputDir = outDir;

    SourceGuard guard (project);
    guard.check (outDir);

    if (isSameOrInside (project.xpjPath, outDir))
        throw ExportError ("The export folder must not contain the source project: " + pathToUtf8 (outDir));

    std::error_code ec;
    if (fs::exists (outDir, ec) && ! fs::is_empty (outDir, ec) && ! options.overwrite)
        throw ExportError ("Export folder already exists and is not empty: " + pathToUtf8 (outDir)
                           + " (choose another folder or enable overwrite)");

    fs::create_directories (outDir, ec);
    if (ec)
        throw ExportError ("Cannot create export folder: " + pathToUtf8 (outDir) + " (" + ec.message() + ")");

    // ---- samples ----------------------------------------------------------
    SampleResolver resolver (project);
    std::map<std::string, ResolvedSample> resolved;     // sampleFile -> resolution
    std::map<std::string, std::string> exportedName;    // canonical source path -> "Samples/x.wav"
    std::set<std::string> usedNames;

    for (const auto& track : project.tracks)
        for (const auto& pad : track.pads)
            for (const auto& layer : pad.layers)
                if (! resolved.count (layer.sampleFile))
                {
                    auto r = resolver.resolve (layer.sampleFile);
                    if (r.found)
                        guard.addSource (r.path);
                    resolved.emplace (layer.sampleFile, std::move (r));
                }

    const auto samplesDir = outDir / "Samples";
    for (const auto& [file, r] : resolved)
    {
        if (! r.found)
        {
            ++result.samplesMissing;
            diag.warning ("samples", "missing sample: " + file);
            continue;
        }

        const auto key = asciiLower (pathToUtf8 (r.path));
        if (exportedName.count (key))
            continue;

        auto name = sanitizeFileName (pathToUtf8 (r.path.filename()));
        if (usedNames.count (asciiLower (name)))
        {
            const auto stem = pathToUtf8 (pathFromUtf8 (name).stem());
            const auto ext = pathToUtf8 (pathFromUtf8 (name).extension());
            for (int i = 2; usedNames.count (asciiLower (name)); ++i)
                name = stem + " (" + std::to_string (i) + ")" + ext;
        }
        usedNames.insert (asciiLower (name));
        exportedName[key] = "Samples/" + name;

        if (! options.copySamples)
            continue;

        fs::create_directories (samplesDir, ec);
        const auto target = samplesDir / pathFromUtf8 (name);
        guard.check (target);

        fs::copy_file (r.path, target, options.overwrite ? fs::copy_options::overwrite_existing
                                                         : fs::copy_options::skip_existing, ec);
        if (ec)
        {
            diag.error ("samples", "cannot copy " + pathToUtf8 (r.path) + ": " + ec.message());
            continue;
        }
        ++result.samplesCopied;
        result.written.push_back (target);
    }

    // ---- roles --------------------------------------------------------------
    std::map<std::string, RoleMap> rolesByTrack;
    for (const auto& track : project.tracks)
    {
        if (track.kind != TrackKind::Drum)
            continue;
        auto roles = classifyTrack (project, track, resolver);
        if (options.roleFor)
            for (const auto& pad : track.pads)
                if (pad.hasSample() && pad.index >= 0 && pad.index < kMaxPads)
                    if (const auto role = options.roleFor (track, pad))
                        roles[static_cast<std::size_t> (pad.index)] = *role;
        rolesByTrack[track.name] = roles;
    }

    // ---- MIDI -------------------------------------------------------------
    std::vector<std::string> midiNames (project.sequences.size());
    std::vector<std::string> drumNames (project.sequences.size());
    std::vector<std::string> melodyNames (project.sequences.size());
    if (options.writeMidi)
    {
        const auto midiDir = outDir / "MIDI";
        for (std::size_t i = 0; i < project.sequences.size(); ++i)
        {
            const auto& seq = project.sequences[i];
            int notes = 0;
            for (const auto& c : seq.clips)
                notes += c.playableNoteCount();

            if (notes == 0)
            {
                diag.info ("midi", "sequence '" + seq.name + "' has no notes - skipped");
                continue;
            }

            fs::create_directories (midiDir, ec);
            const auto name = midiFileName (seq.number(), seq.name);
            const auto target = midiDir / pathFromUtf8 (name);
            guard.check (target);

            const auto bytes = writeSequenceMidi (project, seq);
            writeFileAtomically (target, bytes.data(), bytes.size());
            midiNames[i] = "MIDI/" + name;
            ++result.midiFiles;
            result.written.push_back (target);

            if (! options.splitParts)
                continue;

            for (const auto part : { Part::Drums, Part::Melody })
            {
                std::vector<Clip> filtered;
                filtered.reserve (seq.clips.size());
                std::size_t partNotes = 0;
                for (const auto& c : seq.clips)
                {
                    const auto* track = project.findTrack (c.trackName);
                    const auto roles = rolesByTrack.find (c.trackName);
                    if (track == nullptr || roles == rolesByTrack.end())
                        continue;
                    filtered.push_back (filterClip (c, *track, roles->second, part));
                    partNotes += filtered.back().notes.size();
                }
                if (partNotes == 0)
                    continue;

                std::vector<const Clip*> clips;
                for (const auto& c : filtered)
                    clips.push_back (&c);

                const auto partFile = midiFileName (seq.number(), seq.name, partName (part));
                const auto partTarget = midiDir / pathFromUtf8 (partFile);
                guard.check (partTarget);
                const auto partBytes = writeSequenceMidi (project, seq, clips);
                writeFileAtomically (partTarget, partBytes.data(), partBytes.size());
                (part == Part::Drums ? drumNames : melodyNames)[i] = "MIDI/" + partFile;
                ++result.midiFiles;
                result.written.push_back (partTarget);
            }

            // "16 Levels" pads: the recorded melody as chromatic notes (C3 = original pitch).
            for (const auto& c : seq.clips)
            {
                const auto* track = project.findTrack (c.trackName);
                if (track == nullptr || track->kind != TrackKind::Drum)
                    continue;
                for (const auto& pad : track->pads)
                {
                    if (! pad.hasSample() || ! hasTunedNotes (c, *track, pad.index))
                        continue;
                    const auto melody = chromaticClipForPad (c, *track, pad.index);
                    const auto file = midiFileName (seq.number(), seq.name, pad.label() + " " + pad.layers.front().sampleName + " (16 Levels)");
                    const auto target = midiDir / pathFromUtf8 (file);
                    guard.check (target);
                    const auto bytes16 = writeSequenceMidi (project, seq, { &melody });
                    writeFileAtomically (target, bytes16.data(), bytes16.size());
                    ++result.midiFiles;
                    result.written.push_back (target);
                }
            }
        }
    }

    // ---- mapping.json -----------------------------------------------------
    if (options.writeMapping)
    {
        json root;
        root["format"] = "padporter-mapping";
        root["formatVersion"] = 1;
        root["generator"] = std::string (kProductName) + " " + kVersion;
        root["project"] = {
            { "name", project.name },
            { "source", pathToUtf8 (project.xpjPath) },
            { "headerVersion", project.header.version },
            { "masterTempo", project.masterTempo },
            { "masterTempoEnabled", project.masterTempoEnabled },
        };

        json tracks = json::array();
        for (const auto& track : project.tracks)
        {
            json t;
            t["name"] = track.name;
            t["kind"] = track.kind == TrackKind::Drum ? "drum" : "other";
            json pads = json::array();
            for (const auto& pad : track.pads)
            {
                if (! pad.hasSample())
                    continue;

                json layers = json::array();
                for (const auto& layer : pad.layers)
                {
                    const auto& r = resolved[layer.sampleFile];
                    json l;
                    l["sampleFile"] = layer.sampleFile;
                    l["sampleName"] = layer.sampleName;
                    l["found"] = r.found;
                    if (r.found)
                    {
                        l["exportedFile"] = exportedName[asciiLower (pathToUtf8 (r.path))];
                        const auto info = readWavInfo (r.path);
                        if (info.valid)
                        {
                            l["sampleRate"] = info.sampleRate;
                            l["channels"] = info.channels;
                            l["frames"] = info.frames;
                            json slices = json::array();
                            for (const auto& s : info.slices)
                                slices.push_back ({ { "start", s.start }, { "end", s.end } });
                            if (! slices.empty())
                                l["slices"] = slices;
                        }
                    }
                    l["coarseTune"] = layer.coarseTune;
                    l["fineTune"] = layer.fineTune;
                    l["gain"] = layer.gain;
                    l["pan"] = layer.pan;
                    l["reverse"] = layer.reverse;
                    l["loop"] = layer.loop;
                    l["sliceStart"] = layer.slice.start;
                    l["sliceEnd"] = layer.slice.end;
                    layers.push_back (std::move (l));
                }

                const auto roles = rolesByTrack.find (track.name);
                const auto role = roles != rolesByTrack.end() && pad.index >= 0 && pad.index < kMaxPads
                                      ? roles->second[static_cast<std::size_t> (pad.index)] : PartRole::Drums;

                pads.push_back ({
                    { "index", pad.index },
                    { "role", roleId (role) },
                    { "label", pad.label() },
                    { "midiNote", pad.midiNote },
                    { "noteName", midiNoteName (pad.midiNote) },
                    { "oneShot", pad.oneShot },
                    { "triggerMode", pad.triggerModeRaw },
                    { "muteGroup", pad.muteGroup },
                    { "monophonic", pad.monophonic },
                    { "volume", pad.volume },
                    { "volumeDb", std::round (gainToDb (pad.volume) * 100.0) / 100.0 },
                    { "pan", pad.pan },
                    { "muted", pad.muted },
                    { "layers", layers },
                });
            }
            t["pads"] = pads;
            tracks.push_back (std::move (t));
        }
        root["tracks"] = tracks;

        json seqs = json::array();
        for (std::size_t i = 0; i < project.sequences.size(); ++i)
        {
            const auto& s = project.sequences[i];
            json j;
            j["number"] = s.number();
            j["name"] = s.name;
            j["bpm"] = project.tempoFor (s);
            j["bars"] = s.lengthBars;
            j["timeSignature"] = std::to_string (s.timeSignature.beatsPerBar) + "/" + std::to_string (s.timeSignature.beatUnit);
            j["midiFile"] = midiNames[i].empty() ? json (nullptr) : json (midiNames[i]);
            j["midiDrums"] = drumNames[i].empty() ? json (nullptr) : json (drumNames[i]);
            j["midiMelody"] = melodyNames[i].empty() ? json (nullptr) : json (melodyNames[i]);
            seqs.push_back (std::move (j));
        }
        root["sequences"] = seqs;

        Diagnostics all;
        if (loadDiagnostics != nullptr)
            all.append (*loadDiagnostics);
        all.append (diag);

        json diagnostics = json::array();
        for (const auto& d : all.items())
            diagnostics.push_back ({ { "severity", toString (d.severity) }, { "where", d.where }, { "message", d.message } });
        root["diagnostics"] = diagnostics;

        const auto text = root.dump (2);
        const auto target = outDir / "mapping.json";
        guard.check (target);
        writeFileAtomically (target, text.data(), text.size());
        result.written.push_back (target);

        std::ostringstream log;
        log << kProductName << " " << kVersion << " export log\n";
        log << "Project: " << pathToUtf8 (project.xpjPath) << "\n";
        log << "Samples copied: " << result.samplesCopied << ", missing: " << result.samplesMissing
            << ", MIDI files: " << result.midiFiles << "\n\n";
        for (const auto& d : all.items())
            log << "[" << toString (d.severity) << "] " << d.where << ": " << d.message << "\n";

        const auto logText = log.str();
        const auto logTarget = outDir / "export-log.txt";
        guard.check (logTarget);
        writeFileAtomically (logTarget, logText.data(), logText.size());
        result.written.push_back (logTarget);
    }

    return result;
}
} // namespace padporter
