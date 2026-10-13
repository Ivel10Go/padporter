#include "padporter/Parts.h"
#include "padporter/PathUtil.h"
#include "padporter/SampleResolver.h"
#include "padporter/WavInfo.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace padporter
{
namespace
{
const std::vector<std::string> kDrumWords {
    "kick", "kik", "kck", "bd", "bassdrum", "snare", "snr", "sd", "clap", "clp", "hat", "hats", "hihat", "hh",
    "oh", "ch", "cymbal", "cym", "crash", "ride", "tom", "toms", "perc", "prc", "percussion", "rim", "rimshot",
    "shaker", "shk", "conga", "bongo", "cowbell", "clave", "snap", "drum", "drums", "tamb", "tambourine", "808",
    "909", "707", "break", "fill"
};

const std::vector<std::string> kMelodyWords {
    "bass", "bas", "bassline", "sub", "lead", "pad", "piano", "keys", "key", "chord", "chords", "synth", "vox",
    "vocal", "vocals", "voc", "voice", "string", "strings", "brass", "guitar", "gtr", "organ", "arp", "pluck",
    "melody", "mel", "bell", "bells", "choir", "flute", "sax", "horn", "lp", "phrase", "riff", "stab"
};

/** Lower-case alphanumeric tokens; '#' is kept so that "c#m" stays one token. */
std::vector<std::string> tokenize (const std::string& name)
{
    std::vector<std::string> tokens;
    std::string current;
    for (unsigned char c : asciiLower (name))
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '#')
            current.push_back (static_cast<char> (c));
        else if (! current.empty())
        {
            tokens.push_back (current);
            current.clear();
        }
    }
    if (! current.empty())
        tokens.push_back (current);
    return tokens;
}

std::string stripDigits (const std::string& token)
{
    std::string out;
    for (char c : token)
        if (c < '0' || c > '9')
            out.push_back (c);
    return out;
}

bool matchesWord (const std::string& token, const std::string& word)
{
    if (token == word)
        return true;

    // "909kick" -> "kick", "lp5" -> "lp"; compounds like "pianoph" / "voxph" by prefix.
    const auto letters = stripDigits (token);
    if (! letters.empty() && letters == word)
        return true;
    if (word.size() >= 3 && letters.size() > word.size() && letters.rfind (word, 0) == 0)
        return true;
    if (word.size() >= 4 && letters.size() > word.size() && letters.compare (letters.size() - word.size(), word.size(), word) == 0)
        return true;
    return false;
}

bool containsWord (const std::vector<std::string>& tokens, const std::vector<std::string>& words)
{
    for (const auto& t : tokens)
        for (const auto& w : words)
            if (matchesWord (t, w))
                return true;
    return false;
}

/** Minor-key tokens like "am", "c#m", "gbm", or any key name directly after "in". */
bool containsKeyName (const std::vector<std::string>& tokens)
{
    auto isKey = [] (const std::string& t, bool requireMinor) {
        if (t.empty() || t[0] < 'a' || t[0] > 'g')
            return false;
        std::size_t i = 1;
        if (i < t.size() && (t[i] == '#' || t[i] == 'b'))
            ++i;
        if (i < t.size() && t[i] == 'm')
            return i + 1 == t.size();
        return ! requireMinor && i == t.size();
    };

    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (isKey (tokens[i], true))
            return true;
        if (i > 0 && tokens[i - 1] == "in" && isKey (tokens[i], false))
            return true;
    }
    return false;
}

const SamplePoolEntry* findPoolEntry (const Project& project, const Track& track, const std::string& sampleFile)
{
    const auto wanted = asciiLower (pathToUtf8 (pathFromUtf8 (sampleFile).filename()));
    for (const auto* pool : { &track.samplePool, &project.samplePool })
        for (const auto& e : *pool)
            if (asciiLower (pathToUtf8 (pathFromUtf8 (e.path).filename())) == wanted)
                return &e;
    return nullptr;
}
} // namespace

const char* partName (Part part)
{
    switch (part)
    {
        case Part::All:    return "Alles";
        case Part::Drums:  return "Drums";
        case Part::Melody: return "Melodie";
    }
    return "";
}

const char* roleId (PartRole role)
{
    return role == PartRole::Drums ? "drums" : "melody";
}

PartRole classifyPad (const PadFacts& facts)
{
    const auto tokens = tokenize (facts.name);
    int drums = 0;
    int melody = 0;

    if (containsWord (tokens, kDrumWords))   drums += 3;
    if (containsWord (tokens, kMelodyWords)) melody += 3;
    if (containsKeyName (tokens))            melody += 2;

    if (facts.durationSeconds >= 0.0)
    {
        if (facts.durationSeconds <= 0.6)      drums += 2;
        else if (facts.durationSeconds <= 1.2) drums += 1;
        else if (facts.durationSeconds >= 2.5) melody += 2;
        else if (facts.durationSeconds >= 1.5) melody += 1;
    }

    if (facts.sampleTempo > 0.0)  melody += 1;
    if (facts.playedChromatically) melody += 2;

    return melody > drums ? PartRole::Melody : PartRole::Drums;
}

RoleMap classifyTrack (const Project& project, const Track& track, const std::function<double (const Pad&)>& durationSeconds)
{
    RoleMap roles;
    roles.fill (PartRole::Drums);

    // Pads that are played with the tuning modifier (16 Levels / keys) in any sequence.
    std::set<int> chromaticNotes;
    for (const auto& seq : project.sequences)
        for (const auto& clip : seq.clips)
            if (clip.trackName == track.name)
                for (const auto& n : clip.notes)
                    if (n.modifiers.count (0) > 0)
                        chromaticNotes.insert (n.note);

    for (const auto& pad : track.pads)
    {
        if (! pad.hasSample() || pad.index < 0 || pad.index >= kMaxPads)
            continue;

        const auto& layer = pad.layers.front();
        PadFacts facts;
        facts.name = layer.sampleName.empty() ? layer.sampleFile : layer.sampleName;
        facts.durationSeconds = durationSeconds ? durationSeconds (pad) : -1.0;
        if (const auto* entry = findPoolEntry (project, track, layer.sampleFile))
            facts.sampleTempo = entry->tempo;
        facts.playedChromatically = chromaticNotes.count (pad.midiNote) > 0;

        roles[static_cast<std::size_t> (pad.index)] = classifyPad (facts);
    }
    return roles;
}

RoleMap classifyTrack (const Project& project, const Track& track, const SampleResolver& resolver)
{
    return classifyTrack (project, track, [&resolver] (const Pad& pad) {
        const auto r = resolver.resolve (pad.layers.front().sampleFile);
        if (! r.found)
            return -1.0;
        const auto info = readWavInfo (r.path);
        return info.valid ? info.durationSeconds() : -1.0;
    });
}

bool noteBelongsTo (const NoteEvent& note, const Track& track, const RoleMap& roles, Part part)
{
    if (note.muted)
        return false;
    if (part == Part::All)
        return true;

    const auto* pad = track.padForNote (note.note);
    if (pad == nullptr || pad->index < 0 || pad->index >= kMaxPads)
        return false;

    const auto role = roles[static_cast<std::size_t> (pad->index)];
    return part == Part::Drums ? role == PartRole::Drums : role == PartRole::Melody;
}

Clip filterClip (const Clip& clip, const Track& track, const RoleMap& roles, Part part)
{
    Clip out;
    out.trackName = clip.trackName;
    out.row = clip.row;
    out.column = clip.column;
    for (const auto& n : clip.notes)
        if (noteBelongsTo (n, track, roles, part))
            out.notes.push_back (n);
    return out;
}

bool hasTunedNotes (const Clip& clip, const Track& track, int padIndex)
{
    for (const auto& n : clip.notes)
        if (! n.muted && n.isTuned())
            if (const auto* pad = track.padForNote (n.note); pad != nullptr && pad->index == padIndex)
                return true;
    return false;
}

Clip chromaticClipForPad (const Clip& clip, const Track& track, int padIndex, int rootNote)
{
    Clip out;
    out.trackName = clip.trackName;
    for (const auto& n : clip.notes)
    {
        if (n.muted)
            continue;
        const auto* pad = track.padForNote (n.note);
        if (pad == nullptr || pad->index != padIndex)
            continue;
        auto copy = n;
        copy.note = std::clamp (rootNote + static_cast<int> (std::lround (n.tuneSemitones())), 0, 127);
        copy.modifiers.clear();
        out.notes.push_back (std::move (copy));
    }
    return out;
}

PartCounts countParts (const Clip& clip, const Track& track, const RoleMap& roles)
{
    PartCounts counts;
    for (const auto& n : clip.notes)
    {
        if (n.muted)
            continue;
        if (noteBelongsTo (n, track, roles, Part::Drums))
            ++counts.drums;
        else if (noteBelongsTo (n, track, roles, Part::Melody))
            ++counts.melody;
        else
            ++counts.unassigned;
    }
    return counts;
}
} // namespace padporter
