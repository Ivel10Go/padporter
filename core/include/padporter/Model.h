#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace padporter
{
/** Tick resolution of all sequence data (pulses per quarter note). */
inline constexpr int kTicksPerQuarter = 960;

/** Number of pads in a drum program (8 banks x 16 pads). */
inline constexpr int kMaxPads = 128;
inline constexpr int kPadsPerBank = 16;

/** The plain-text lines in front of the JSON payload, e.g. "ACVS", "1.2.1.2", ... */
struct XpjHeader
{
    std::vector<std::string> lines;
    std::string magic;      // line 1, expected "ACVS"
    std::string version;    // line 2
    std::string type;       // line 3, expected "SerialisableProjectData"
    std::string encoding;   // line 4, expected "json"
    std::string platform;   // line 5, e.g. "Linux"
    bool compressed = true; // payload was gzip-compressed
};

struct TimeSignature
{
    int beatsPerBar = 4;
    int beatUnit = 4;                       // denominator, derived from ticksPerBeat
    int ticksPerBeat = kTicksPerQuarter;    // "beatLength" in the project file

    int ticksPerBar() const noexcept { return beatsPerBar * ticksPerBeat; }
};

struct SliceRegion
{
    std::int64_t start = 0;     // sample frames; end == 0 means "whole sample"
    std::int64_t end = 0;
    std::int64_t loopStart = 0;
};

struct Layer
{
    std::string sampleFile;     // file name as stored in the project (UTF-8)
    std::string sampleName;
    int coarseTune = 0;         // semitones
    int fineTune = 0;           // cents
    double gain = 1.0;          // linear
    double pan = 0.5;           // 0 = left, 0.5 = centre, 1 = right
    bool reverse = false;
    bool loop = false;
    std::int64_t offset = 0;
    SliceRegion slice;
};

struct AmpEnvelope
{
    double attack = 0.0;        // raw normalised values (0..1) as stored
    double decay = 0.0;
    double release = 0.0;
    bool oneShot = false;
};

struct FilterSettings
{
    int type = 0;
    double cutoff = 1.0;
    double resonance = 0.0;
};

struct Pad
{
    int index = 0;              // 0..127
    int midiNote = 36;
    std::vector<Layer> layers;  // only layers that reference a sample
    int triggerModeRaw = -1;    // -1 = field missing
    bool oneShot = true;        // false = gate (stops on note-off)
    int muteGroup = 0;          // 0 = none
    bool monophonic = false;
    double volume = 0.7079457640647888; // linear pad-strip gain (-3 dB default)
    double pan = 0.5;
    bool muted = false;
    AmpEnvelope ampEnvelope;
    FilterSettings filter;
    bool warpEnable = false;    // time-stretched to the sequence tempo on the device
    double tempo = 0.0;         // sample tempo used for warping (BPM)
    int instCoarseTune = 0;     // instrument-level tuning, added to the layer tuning
    int instFineTune = 0;
    int chopMode = 0;
    int chopRegions = 0;

    bool hasSample() const noexcept { return ! layers.empty(); }
    int bank() const noexcept { return index / kPadsPerBank; }
    int padInBank() const noexcept { return index % kPadsPerBank; }
    /** E.g. "A01" .. "H16". */
    std::string label() const;
};

struct SamplePoolEntry
{
    std::string path;
    int loadImpl = 0;           // 0 = stored with project, 1 = device library
    int rootNote = 60;
    double tempo = 0.0;         // detected loop tempo (0 = one-shot / unknown)
    std::string key;            // detected key, e.g. "C Natural Minor"
};

enum class TrackKind
{
    Drum,
    Routing,    // submix / output tracks (program type 8 / 9)
    Other
};

struct Track
{
    int index = 0;
    std::string name;
    TrackKind kind = TrackKind::Other;
    int programType = -1;       // 0 = drum, 8 = submix, 9 = output (as seen on the device)
    std::string programInfo;    // short description for non-drum tracks
    double volume = 1.0;
    double pan = 0.5;
    double programVolume = 1.0; // program fader (linear)
    std::vector<Pad> pads;      // index == pad index; may contain pads without samples
    std::vector<SamplePoolEntry> samplePool;

    int loadedPadCount() const noexcept;
    const Pad* padForNote (int note) const noexcept;
};

struct NoteEvent
{
    std::int64_t tick = 0;
    int note = 0;
    double velocity = 1.0;      // 0..1
    std::int64_t length = 0;    // ticks
    bool muted = false;
    std::map<int, double> modifiers; // active note modifiers (slot -> value)

    int velocity127() const noexcept;

    /**
        Pitch offset recorded with "16 Levels" (tuning modifier, slot 0, relative):
        semitones = (value - 0.5) * 240. 0 when the note is not tuned.
    */
    double tuneSemitones() const noexcept;
    bool isTuned() const noexcept { return modifiers.count (0) > 0; }
};

struct AutomationEvent
{
    std::int64_t tick = 0;
    int note = 0;
    int parameter = 0;
    double value = 0.0;
};

struct Clip
{
    std::string trackName;
    int row = 0;
    int column = 0;
    std::vector<NoteEvent> notes;           // sorted by tick
    std::vector<AutomationEvent> automation;

    int playableNoteCount() const noexcept;
};

struct Sequence
{
    int key = 0;
    std::string name;
    double bpm = 120.0;
    bool hasOwnBpm = false;
    int lengthBars = 1;
    std::int64_t lengthPulses = 0;  // exact length if stored, 0 = derive from bars
    TimeSignature timeSignature;
    std::vector<Clip> clips;

    /** Sequence number as shown on the device (key + 1). */
    int number() const noexcept { return key + 1; }
    std::int64_t lengthTicks() const noexcept;
    const Clip* clipForTrack (const std::string& trackName) const noexcept;
};

/** The device's "Color Compressor" on the main output (its parameters are an opaque blob). */
struct MasterCompressor
{
    bool present = false;
    bool enabled = false;
    bool bypassed = false;      // decoded from the state blob (unverified heuristic)
    bool colorOn = false;       // "Color" saturation button (unverified heuristic)

    bool active() const noexcept { return present && enabled && ! bypassed; }
};

struct Project
{
    std::filesystem::path xpjPath;
    std::filesystem::path dataDir;          // "<stem>_[ProjectData]"
    std::string name;
    XpjHeader header;
    double masterTempo = 120.0;
    bool masterTempoEnabled = false;
    std::vector<Track> tracks;
    std::vector<Sequence> sequences;
    std::vector<SamplePoolEntry> samplePool; // "data.samples": all samples of the project with metadata
    double masterVolume = 1.0;              // main output fader (linear)
    MasterCompressor masterCompressor;
    std::vector<std::string> topLevelKeys;  // keys of the "data" object, for format research

    double tempoFor (const Sequence& s) const noexcept;
    const Track* findTrack (const std::string& name) const noexcept;
    /** Index of the first drum track with at least one sample, or the first drum track, or -1. */
    int defaultDrumTrack() const noexcept;
};
} // namespace padporter
