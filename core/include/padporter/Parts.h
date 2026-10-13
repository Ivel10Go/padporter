#pragma once

#include "Model.h"

#include <array>
#include <functional>
#include <string>

namespace padporter
{
class SampleResolver;

/** What a pad contributes musically. */
enum class PartRole
{
    Drums,
    Melody
};

/** A selectable part of a sequence. */
enum class Part
{
    All,
    Drums,
    Melody
};

using RoleMap = std::array<PartRole, kMaxPads>;

/** German display name: "Alles", "Drums", "Melodie". */
const char* partName (Part part);

/** Stable identifier for files and JSON: "drums", "melody". */
const char* roleId (PartRole role);

/** Everything the automatic classification looks at for one pad. */
struct PadFacts
{
    std::string name;                 // sample name or file name
    double durationSeconds = -1.0;    // < 0 = unknown
    double sampleTempo = 0.0;         // detected loop tempo from the sample metadata (0 = none)
    bool playedChromatically = false; // notes on this pad use the tuning modifier (16 Levels / keys)
};

/**
    Heuristic drum/melody classification: keywords in the name (kick, snare, hat ... vs. bass,
    lead, piano, keys, vox, key names like "Am"), sample length, loop tempo and chromatic playing.
*/
PartRole classifyPad (const PadFacts& facts);

/** Automatic roles of all pads of a track. 'durationSeconds' returns < 0 when unknown. */
RoleMap classifyTrack (const Project& project, const Track& track,
                       const std::function<double (const Pad&)>& durationSeconds);

/** Same, with durations read from the WAV headers of the resolved samples. */
RoleMap classifyTrack (const Project& project, const Track& track, const SampleResolver& resolver);

/**
    True if a note belongs to a part. Notes that do not hit a pad with a sample only belong
    to Part::All. Muted notes are never part of anything.
*/
bool noteBelongsTo (const NoteEvent& note, const Track& track, const RoleMap& roles, Part part);

/** Copy of a clip that only contains the notes of a part (automation is dropped). */
Clip filterClip (const Clip& clip, const Track& track, const RoleMap& roles, Part part);

/** True if a pad is played with recorded pitch offsets ("16 Levels" tune) in this clip. */
bool hasTunedNotes (const Clip& clip, const Track& track, int padIndex);

/**
    The notes of one pad as a chromatic melody: note = rootNote + recorded pitch offset.
    For "16 Levels" parts this is what a keyboard sampler (e.g. Ableton Simpler with the
    pad's sample, root C3) needs to reproduce the recorded melody.
*/
Clip chromaticClipForPad (const Clip& clip, const Track& track, int padIndex, int rootNote = 60);

struct PartCounts
{
    int drums = 0;
    int melody = 0;
    int unassigned = 0;   // notes without a pad that has a sample
};

PartCounts countParts (const Clip& clip, const Track& track, const RoleMap& roles);
} // namespace padporter
