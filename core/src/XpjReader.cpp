#include "padporter/XpjReader.h"
#include "padporter/Gzip.h"
#include "padporter/PathUtil.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace padporter
{
namespace
{
// ---------------------------------------------------------------------------
// Tolerant JSON accessors: the format is undocumented, so every field is optional.

const json* child (const json& j, const char* key)
{
    if (! j.is_object())
        return nullptr;
    const auto it = j.find (key);
    return it == j.end() ? nullptr : &*it;
}

const json* childPath (const json& j, std::initializer_list<const char*> keys)
{
    const json* current = &j;
    for (auto* k : keys)
    {
        current = child (*current, k);
        if (current == nullptr)
            return nullptr;
    }
    return current;
}

std::optional<double> optNumber (const json* j)
{
    if (j == nullptr)
        return std::nullopt;
    if (j->is_number())
        return j->get<double>();
    if (j->is_boolean())
        return j->get<bool>() ? 1.0 : 0.0;
    return std::nullopt;
}

double number (const json& j, const char* key, double fallback)
{
    return optNumber (child (j, key)).value_or (fallback);
}

std::int64_t integer (const json& j, const char* key, std::int64_t fallback)
{
    if (auto v = optNumber (child (j, key)))
        return static_cast<std::int64_t> (std::llround (*v));
    return fallback;
}

bool boolean (const json& j, const char* key, bool fallback)
{
    if (auto v = optNumber (child (j, key)))
        return *v != 0.0;
    return fallback;
}

std::string string (const json& j, const char* key, const std::string& fallback = {})
{
    const auto* c = child (j, key);
    return (c != nullptr && c->is_string()) ? c->get<std::string>() : fallback;
}

/** Cereal-style values are often wrapped: { "value0": x }. */
std::optional<double> wrappedNumber (const json* j)
{
    if (j == nullptr)
        return std::nullopt;
    if (auto v = optNumber (j))
        return v;
    return optNumber (child (*j, "value0"));
}

std::string trimLine (std::string s)
{
    while (! s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\0'))
        s.pop_back();
    return s;
}

XpjHeader parseHeaderText (const std::string& text, bool compressed)
{
    XpjHeader h;
    h.compressed = compressed;

    std::size_t start = 0;
    while (start < text.size())
    {
        auto end = text.find ('\n', start);
        if (end == std::string::npos)
            end = text.size();
        auto line = trimLine (text.substr (start, end - start));
        if (! line.empty())
            h.lines.push_back (std::move (line));
        start = end + 1;
    }

    auto lineAt = [&h] (std::size_t i) { return i < h.lines.size() ? h.lines[i] : std::string(); };
    h.magic = lineAt (0);
    h.version = lineAt (1);
    h.type = lineAt (2);
    h.encoding = lineAt (3);
    h.platform = lineAt (4);
    return h;
}

// ---------------------------------------------------------------------------

class ProjectParser
{
public:
    ProjectParser (Project& p, Diagnostics& d) : project (p), diag (d) {}

    void parse (const json& data)
    {
        for (auto it = data.begin(); it != data.end(); ++it)
            project.topLevelKeys.push_back (it.key());

        if (auto t = optNumber (child (data, "masterTempo")))
            project.masterTempo = *t;
        else
            diag.warning ("data.masterTempo", "missing - assuming 120 BPM");

        project.masterTempoEnabled = boolean (data, "masterTempoEnabled", false);

        if (const auto* samples = child (data, "samples"))
            project.samplePool = parseSamplePool (*samples);

        parseMainOutput (data);

        if (const auto* tracks = child (data, "tracks"); tracks != nullptr && tracks->is_array())
        {
            for (std::size_t i = 0; i < tracks->size(); ++i)
                parseTrack ((*tracks)[i], static_cast<int> (i));
        }
        else
        {
            diag.warning ("data.tracks", "no track list found");
        }

        if (const auto* seqs = child (data, "sequences"); seqs != nullptr && seqs->is_array())
        {
            for (std::size_t i = 0; i < seqs->size(); ++i)
                parseSequence ((*seqs)[i], static_cast<int> (i));

            std::stable_sort (project.sequences.begin(), project.sequences.end(),
                              [] (const Sequence& a, const Sequence& b) { return a.key < b.key; });
        }
        else
        {
            diag.warning ("data.sequences", "no sequence list found");
        }

        for (const auto& s : project.sequences)
            for (const auto& c : s.clips)
                if (project.findTrack (c.trackName) == nullptr)
                    diag.warning ("sequence '" + s.name + "'", "clip refers to unknown track '" + c.trackName + "'");
    }

private:
    Project& project;
    Diagnostics& diag;

    // ---- main output ------------------------------------------------------

    static std::vector<std::uint8_t> decodeBase64 (const std::string& text)
    {
        static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<std::uint8_t> out;
        std::uint32_t buffer = 0;
        int bits = 0;
        for (char c : text)
        {
            const auto pos = alphabet.find (c);
            if (pos == std::string::npos)
                continue;
            buffer = (buffer << 6) | static_cast<std::uint32_t> (pos);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back (static_cast<std::uint8_t> ((buffer >> bits) & 0xff));
            }
        }
        return out;
    }

    void parseMainOutput (const json& data)
    {
        const auto* outputs = childPath (data, { "mixer", "outputs" });
        if (outputs == nullptr || ! outputs->is_array() || outputs->empty())
            return;

        const auto* mix = child ((*outputs)[0], "mixable");
        if (mix == nullptr)
            return;

        project.masterVolume = number (*mix, "volume", 1.0);

        const auto* effects = childPath (*mix, { "inserts", "effects" });
        if (effects == nullptr || ! effects->is_array())
            return;

        for (const auto& e : *effects)
        {
            const auto* name = childPath (e, { "plugin", "description", "name" });
            if (name == nullptr || ! name->is_string() || name->get<std::string>() != "Color Compressor")
                continue;

            auto& comp = project.masterCompressor;
            comp.present = true;
            comp.enabled = boolean (e, "enable", true);

            // State blob "119.<base64>" ('.' stands for 'A'). Byte positions as found by MPC2Live.
            if (const auto* state = childPath (e, { "plugin", "state" }); state != nullptr && state->is_string())
            {
                auto text = state->get<std::string>();
                if (text.rfind ("119.", 0) == 0)
                {
                    text = text.substr (4);
                    std::replace (text.begin(), text.end(), '.', 'A');
                    const auto blob = decodeBase64 (text);
                    if (blob.size() >= 105)
                    {
                        comp.colorOn = blob[98] == 0x1f && blob[99] == 0xf8;
                        comp.bypassed = blob[102] == 0x03 && blob[103] == 0xd0 && blob[104] == 0x80;
                    }
                }
            }
        }
    }

    // ---- tracks -----------------------------------------------------------

    static std::vector<SamplePoolEntry> parseSamplePool (const json& samples)
    {
        std::vector<SamplePoolEntry> pool;
        if (! samples.is_array())
            return pool;

        for (const auto& s : samples)
        {
            SamplePoolEntry e;
            e.path = string (s, "path");
            e.loadImpl = static_cast<int> (integer (s, "loadImpl", 0));
            if (const auto* meta = child (s, "metadata"))
            {
                e.rootNote = static_cast<int> (integer (*meta, "rootNote", 60));
                e.tempo = number (*meta, "tempo", 0.0);
                e.key = string (*meta, "key");
            }
            pool.push_back (std::move (e));
        }
        return pool;
    }

    void parseTrack (const json& t, int index)
    {
        Track track;
        track.index = index;
        track.name = string (t, "name", "Track " + std::to_string (index + 1));
        track.volume = number (t, "volume", 1.0);
        track.pan = number (t, "pan", 0.5);

        const std::string where = "data.tracks[" + std::to_string (index) + "]";

        if (const auto* samples = child (t, "samples"))
            track.samplePool = parseSamplePool (*samples);

        const auto* program = child (t, "program");
        const auto* instruments = program != nullptr ? childPath (*program, { "drum", "instruments" }) : nullptr;
        track.programType = program != nullptr ? static_cast<int> (integer (*program, "type", -1)) : -1;
        if (const auto* programMix = program != nullptr ? child (*program, "mixable") : nullptr)
            track.programVolume = number (*programMix, "volume", 1.0);

        if (instruments != nullptr && instruments->is_array())
        {
            track.kind = TrackKind::Drum;
            parsePads (track, *program, *instruments, where);
        }
        else if (track.programType == 8 || track.programType == 9)
        {
            // Submix / output busses exist in every device project; they carry no notes.
            track.kind = TrackKind::Routing;
            track.programInfo = track.programType == 8 ? "submix" : "output";
        }
        else
        {
            track.kind = TrackKind::Other;
            track.programInfo = program == nullptr ? std::string ("no program")
                                                   : "program type " + std::to_string (track.programType);
            diag.info (where, "track '" + track.name + "' is not a drum program (" + track.programInfo + ") - not supported, ignored");
        }

        project.tracks.push_back (std::move (track));
    }

    static std::optional<int> noteForPad (const json* map, std::size_t padIndex)
    {
        if (map == nullptr)
            return std::nullopt;
        if (map->is_object())
            if (auto v = optNumber (child (*map, ("value" + std::to_string (padIndex)).c_str())))
                return static_cast<int> (*v);
        if (map->is_array() && padIndex < map->size())
            if (auto v = optNumber (&(*map)[padIndex]))
                return static_cast<int> (*v);
        return std::nullopt;
    }

    void parsePads (Track& track, const json& program, const json& instruments, const std::string& where)
    {
        const json* noteMap = childPath (program, { "padNoteMap", "noteForPad" });
        if (noteMap == nullptr)
            diag.info (where, "no padNoteMap - using default notes 36 + pad index");

        if (instruments.size() > static_cast<std::size_t> (kMaxPads))
            diag.warning (where, "more than 128 pads (" + std::to_string (instruments.size()) + ") - extra pads ignored");

        std::set<int> unknownTriggerModes;
        int multiLayerPads = 0;

        const auto count = std::min<std::size_t> (instruments.size(), kMaxPads);
        for (std::size_t i = 0; i < count; ++i)
        {
            const json& inst = instruments[i];
            Pad pad;
            pad.index = static_cast<int> (i);
            pad.midiNote = std::clamp (noteForPad (noteMap, i).value_or (36 + static_cast<int> (i)), 0, 127);

            const json* layers = child (inst, "layersv");
            if (layers == nullptr)
                layers = child (inst, "layers");

            if (layers != nullptr && layers->is_array())
                for (const auto& l : *layers)
                    if (auto layer = parseLayer (l))
                        pad.layers.push_back (std::move (*layer));

            if (pad.layers.size() > 1)
                ++multiLayerPads;

            pad.triggerModeRaw = static_cast<int> (integer (inst, "triggerMode", -1));
            pad.ampEnvelope.attack  = wrappedNumber (childPath (inst, { "synthSection", "ampEnvelope", "Attack" })).value_or (0.0);
            pad.ampEnvelope.decay   = wrappedNumber (childPath (inst, { "synthSection", "ampEnvelope", "Decay" })).value_or (0.0);
            pad.ampEnvelope.release = wrappedNumber (childPath (inst, { "synthSection", "ampEnvelope", "Release" })).value_or (0.0);
            pad.ampEnvelope.oneShot = wrappedNumber (childPath (inst, { "synthSection", "ampEnvelope", "OneShot" })).value_or (0.0) != 0.0;

            switch (pad.triggerModeRaw)
            {
                case -1: pad.oneShot = true; break;
                case 1:  pad.oneShot = true; break;
                case 0:
                case 2:  pad.oneShot = pad.ampEnvelope.oneShot; break;
                default:
                    pad.oneShot = true;
                    unknownTriggerModes.insert (pad.triggerModeRaw);
                    break;
            }

            pad.muteGroup = static_cast<int> (integer (inst, "whichMuteGroup", 0));
            pad.monophonic = boolean (inst, "monophonic", false);
            pad.warpEnable = boolean (inst, "warpEnable", false);
            pad.tempo = number (inst, "tempo", 0.0);
            pad.instCoarseTune = static_cast<int> (integer (inst, "coarseTune", 0));
            pad.instFineTune = static_cast<int> (integer (inst, "fineTune", 0));

            if (const auto* mix = child (inst, "mixable"))
            {
                pad.volume = number (*mix, "volume", pad.volume);
                pad.pan = number (*mix, "pan", pad.pan);
                pad.muted = boolean (*mix, "mute", false);
            }

            if (const auto* chop = child (inst, "chopProperties"))
            {
                pad.chopMode = static_cast<int> (integer (*chop, "chopMode", 0));
                pad.chopRegions = static_cast<int> (integer (*chop, "chopRegions", 0));
            }

            if (const auto* filter = childPath (inst, { "synthSection", "filterData", "value0" }))
            {
                pad.filter.type = static_cast<int> (integer (*filter, "filterType", 0));
                pad.filter.cutoff = number (*filter, "filterCutoff", 1.0);
                pad.filter.resonance = number (*filter, "filterResonance", 0.0);
            }

            track.pads.push_back (std::move (pad));
        }

        for (int mode : unknownTriggerModes)
            diag.warning (where, "unknown triggerMode " + std::to_string (mode) + " - treated as one-shot");

        if (multiLayerPads > 0)
            diag.info (where, std::to_string (multiLayerPads) + " pad(s) use several layers - playback uses the first layer");
    }

    static std::optional<Layer> parseLayer (const json& l)
    {
        Layer layer;
        layer.sampleFile = string (l, "sampleFile");
        if (layer.sampleFile.empty())
            return std::nullopt;

        layer.sampleName = string (l, "sampleName", layer.sampleFile);
        layer.coarseTune = static_cast<int> (integer (l, "coarseTune", 0));
        layer.fineTune = static_cast<int> (integer (l, "fineTune", 0));
        layer.pan = number (l, "pan", 0.5);
        layer.reverse = integer (l, "direction", 0) == 1;
        layer.loop = boolean (l, "loop", false);
        layer.offset = integer (l, "offset", 0);

        if (const auto* vol = child (l, "volume"))
        {
            if (vol->is_number())
                layer.gain = vol->get<double>();
            else
                layer.gain = number (*vol, "gainCoefficient", 1.0);
        }

        if (const auto* slice = child (l, "sliceInfo"))
        {
            layer.slice.start = integer (*slice, "Start", 0);
            layer.slice.end = integer (*slice, "End", 0);
            layer.slice.loopStart = integer (*slice, "LoopStart", 0);
        }

        return layer;
    }

    // ---- sequences --------------------------------------------------------

    void parseSequence (const json& entry, int index)
    {
        const json* value = child (entry, "value");
        const json& v = (value != nullptr && value->is_object()) ? *value : entry;

        Sequence seq;
        seq.key = static_cast<int> (integer (entry, "key", index));
        seq.name = string (v, "name", "Sequence " + std::to_string (index + 1));
        const std::string where = "sequence '" + seq.name + "'";

        if (auto bpm = optNumber (child (v, "bpm")))
        {
            seq.bpm = *bpm;
            seq.hasOwnBpm = true;
        }

        seq.lengthBars = static_cast<int> (std::max<std::int64_t> (1, integer (v, "lengthBars", 1)));
        seq.lengthPulses = std::max<std::int64_t> (0, integer (v, "lengthPulses", 0));

        if (const auto* sigs = childPath (v, { "timeSignatureTrack", "timeSignatures" }); sigs != nullptr && sigs->is_array() && ! sigs->empty())
        {
            const auto& ts = (*sigs)[0];
            seq.timeSignature.beatsPerBar = static_cast<int> (std::max<std::int64_t> (1, integer (ts, "beatsPerBar", 4)));
            seq.timeSignature.ticksPerBeat = static_cast<int> (std::max<std::int64_t> (1, integer (ts, "beatLength", kTicksPerQuarter)));
            seq.timeSignature.beatUnit = std::max (1, (4 * kTicksPerQuarter) / seq.timeSignature.ticksPerBeat);

            if (sigs->size() > 1)
                diag.info (where, "contains time signature changes - only the first one is used");
        }

        if (const auto* clipMaps = child (v, "trackClipMaps"); clipMaps != nullptr && clipMaps->is_array())
        {
            for (std::size_t row = 0; row < clipMaps->size(); ++row)
            {
                const auto& r = (*clipMaps)[row];
                if (r.is_array())
                {
                    for (std::size_t col = 0; col < r.size(); ++col)
                        parseClipEntry (seq, r[col], static_cast<int> (row), static_cast<int> (col), where);
                }
                else if (r.is_object())
                {
                    parseClipEntry (seq, r, static_cast<int> (row), 0, where);
                }
            }
        }

        project.sequences.push_back (std::move (seq));
    }

    void parseClipEntry (Sequence& seq, const json& entry, int row, int col, const std::string& where)
    {
        Clip clip;
        clip.row = row;
        clip.column = col;

        if (const auto* key = child (entry, "key"))
        {
            if (key->is_string())
                clip.trackName = key->get<std::string>();
            else if (key->is_number_integer())
            {
                const auto ti = key->get<std::int64_t>();
                if (ti >= 0 && ti < static_cast<std::int64_t> (project.tracks.size()))
                    clip.trackName = project.tracks[static_cast<std::size_t> (ti)].name;
            }
        }

        const json* value = child (entry, "value");
        const json* events = value != nullptr ? childPath (*value, { "eventList", "events" }) : nullptr;
        if (events == nullptr || ! events->is_array())
            return;

        std::map<int, int> unknownTypes;
        bool velocityScaled = false;

        for (const auto& ev : *events)
        {
            const auto type = integer (ev, "type", -1);
            const auto tick = std::max<std::int64_t> (0, integer (ev, "time", 0));

            // The device inserts initial-value automation itself ("invented": true).
            if (boolean (ev, "invented", false))
                continue;

            if (type == 3)
            {
                const auto* n = child (ev, "note");
                if (n == nullptr)
                    continue;

                NoteEvent note;
                note.tick = tick;
                note.note = std::clamp (static_cast<int> (integer (*n, "note", 0)), 0, 127);
                note.velocity = number (*n, "velocity", 1.0);
                if (note.velocity > 1.0)
                {
                    note.velocity /= 127.0; // tolerate 0..127 scaling
                    velocityScaled = true;
                }
                note.velocity = std::clamp (note.velocity, 0.0, 1.0);
                note.length = std::max<std::int64_t> (1, integer (*n, "length", 1));
                note.muted = boolean (ev, "muted", false);

                for (int slot = 0; slot < 32; ++slot)
                {
                    const auto suffix = std::to_string (slot);
                    if (boolean (*n, ("modifierActiveState" + suffix).c_str(), false))
                        note.modifiers[slot] = number (*n, ("modifierValue" + suffix).c_str(), 0.0);
                }

                clip.notes.push_back (std::move (note));
            }
            else if (type == 1 || type == 2)
            {
                const auto* a = child (ev, "automation");
                if (a == nullptr)
                    continue;
                clip.automation.push_back ({ tick,
                                             static_cast<int> (integer (*a, "note", 0)),
                                             static_cast<int> (integer (*a, "parameter", 0)),
                                             number (*a, "value", 0.0) });
            }
            else
            {
                ++unknownTypes[static_cast<int> (type)];
            }
        }

        std::stable_sort (clip.notes.begin(), clip.notes.end(),
                          [] (const NoteEvent& a, const NoteEvent& b) { return a.tick < b.tick; });

        for (const auto& [type, n] : unknownTypes)
            diag.info (where, std::to_string (n) + " event(s) of unknown type " + std::to_string (type) + " ignored");

        if (velocityScaled)
            diag.info (where, "velocities > 1 found - interpreted as 0..127");

        if (! clip.automation.empty())
            diag.info (where, std::to_string (clip.automation.size()) + " pad automation event(s) are read but not exported");

        seq.clips.push_back (std::move (clip));
    }
};
} // namespace

// ---------------------------------------------------------------------------

fs::path projectDataDirFor (const fs::path& xpjFile)
{
    auto dirName = xpjFile.stem();
    dirName += u8"_[ProjectData]";
    return xpjFile.parent_path() / dirName;
}

RawPayload XpjReader::readRaw (const fs::path& xpjFile)
{
    return decode (readFileBytes (xpjFile));
}

RawPayload XpjReader::decode (const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty())
        throw ParseError ("File is empty");

    RawPayload result;

    // 1) Compressed variant: text header followed by a gzip stream.
    const std::size_t searchLimit = std::min<std::size_t> (bytes.size() > 0 ? bytes.size() - 1 : 0, 4096);
    for (std::size_t i = 0; i < searchLimit; ++i)
    {
        if (bytes[i] == 0x1f && bytes[i + 1] == 0x8b && bytes[i + 2] == 0x08)
        {
            const auto inflated = gunzip (bytes.data() + i, bytes.size() - i);
            std::string text (inflated.begin(), inflated.end());

            const auto brace = text.find ('{');
            if (brace == std::string::npos)
                throw ParseError ("Decompressed payload contains no JSON object");

            // Device files compress the text header together with the JSON; older files
            // were reported to keep it in plain text in front of the gzip stream.
            auto headerText = std::string (bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t> (i));
            headerText += text.substr (0, brace);
            result.header = parseHeaderText (headerText, true);
            result.json = text.substr (brace);
            return result;
        }
    }

    // 2) Uncompressed variant: optional text header followed by JSON.
    const std::string text (bytes.begin(), bytes.end());
    const auto brace = text.find ('{');
    if (brace == std::string::npos)
        throw ParseError ("Neither a gzip stream nor a JSON object was found - not an .xpj project?");

    result.header = parseHeaderText (text.substr (0, brace), false);
    result.json = text.substr (brace);
    return result;
}

LoadResult XpjReader::load (const fs::path& xpjFile)
{
    return parse (readRaw (xpjFile), xpjFile);
}

LoadResult XpjReader::parse (const RawPayload& payload, const fs::path& xpjFile)
{
    LoadResult result;
    auto& project = result.project;
    auto& diag = result.diagnostics;

    project.xpjPath = xpjFile;
    project.dataDir = projectDataDirFor (xpjFile);
    project.name = pathToUtf8 (xpjFile.stem());
    project.header = payload.header;

    const auto& h = payload.header;
    if (h.magic != "ACVS")
        diag.warning ("header", "unexpected magic '" + h.magic + "' (expected ACVS)");
    if (! h.type.empty() && h.type != "SerialisableProjectData")
        diag.warning ("header", "unexpected payload type '" + h.type + "'");
    if (! h.encoding.empty() && h.encoding != "json")
        diag.warning ("header", "unexpected encoding '" + h.encoding + "'");

    json root;
    try
    {
        root = json::parse (payload.json);
    }
    catch (const json::exception& e)
    {
        throw ParseError (std::string ("Invalid JSON in project: ") + e.what());
    }

    if (! root.is_object())
        throw ParseError ("Project JSON root is not an object");

    const json* data = child (root, "data");
    if (data == nullptr || ! data->is_object())
    {
        if (child (root, "tracks") != nullptr || child (root, "sequences") != nullptr)
        {
            diag.warning ("root", "no 'data' wrapper - reading root object directly");
            data = &root;
        }
        else
        {
            throw ParseError ("Project JSON has no 'data' object");
        }
    }

    ProjectParser (project, diag).parse (*data);

    std::error_code ec;
    if (! fs::is_directory (project.dataDir, ec))
        diag.warning ("files", "sample folder not found: " + pathToUtf8 (project.dataDir));

    return result;
}
} // namespace padporter
