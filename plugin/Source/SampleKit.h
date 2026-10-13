#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <padporter/Diagnostics.h>
#include <padporter/Model.h>
#include <padporter/Parts.h>

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace padporter::plugin
{
std::filesystem::path toStdPath (const juce::File& f);
juce::File toJuceFile (const std::filesystem::path& p);
inline juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

/** Decoded audio of one sample file. Immutable once published. */
struct SampleData
{
    juce::AudioBuffer<float> audio;          // 1 or 2 channels
    double sampleRate = 44100.0;
    bool reversed = false;
    juce::String path;
    std::vector<std::int64_t> slicePoints;   // forward frame positions from the "atem" chunk (scaled)
    double timeScale = 1.0;                  // length factor applied by warping (1 = original)
};
using SamplePtr = std::shared_ptr<const SampleData>;

/** Decoded-sample cache shared by loader jobs. Never touched by the audio thread. */
class SampleCache
{
public:
    /** 'stretch' = time-stretch factor for warped pads (1 = unchanged). */
    SamplePtr get (const juce::File& file, bool reversed, double stretch, juce::String& error);
    void clear();

private:
    std::mutex mutex;
    std::map<juce::String, SamplePtr> entries;
};

/** Edits made in the plugin on top of the values stored in the project. */
struct PadOverride
{
    std::optional<double> tuneSemitones;
    std::optional<double> gainDb;
    std::optional<double> pan;       // 0..1
    std::optional<bool> oneShot;
    std::optional<int> muteGroup;    // 0 = off
    std::optional<int> output;       // 0..15
    std::optional<PartRole> role;    // drums / melody, overrides the automatic classification

    bool isEmpty() const noexcept
    {
        return ! tuneSemitones && ! gainDb && ! pan && ! oneShot && ! muteGroup && ! output && ! role;
    }
};
using OverrideMap = std::map<int, PadOverride>;

/** Pad values after applying overrides, as shown in the UI and used for playback. */
struct EffectivePad
{
    double tuneSemitones = 0.0;
    double gainDb = 0.0;
    double pan = 0.5;
    bool oneShot = true;
    int muteGroup = 0;
    int output = 0;
};

EffectivePad computeEffectivePad (const Pad& pad, const PadOverride* override);

struct PadRuntime
{
    SamplePtr sample;
    juce::String resolvedPath;
    bool found = false;
    juce::String error;
};

/** A loaded project plus the decoded samples of the selected drum track (message thread). */
struct LoadedProject
{
    juce::File file;
    std::shared_ptr<const Project> project;
    Diagnostics diagnostics;
    int trackIndex = -1;
    std::array<PadRuntime, kMaxPads> pads;
    RoleMap autoRoles {};            // automatic drum/melody classification of the track's pads
    double targetBpm = 0.0;          // tempo the warped pads are stretched to
    std::uint64_t generation = 0;

    const Track* track() const noexcept;
    const Pad* pad (int index) const noexcept;
};

/** Automatic roles with the user's per-pad role overrides applied. */
RoleMap effectiveRoles (const LoadedProject& loaded, const OverrideMap& overrides);

struct LoadOutcome
{
    bool ok = false;
    juce::String error;
    std::unique_ptr<LoadedProject> loaded;
};

/**
    Parses a project and decodes the samples of one drum track. Runs on a worker thread.
    Warped pads are stretched to 'targetBpm' (<= 0: tempo of the first sequence with notes).
*/
LoadOutcome loadProject (const juce::File& xpj, int requestedTrack, SampleCache& cache, double targetBpm = 0.0);

/** Tuning of a pad's first layer incl. instrument tuning, in semitones. */
double padTuneSemitones (const Pad& pad);

/**
    Length factor the device applies to a warped pad at a tempo: the sample is stretched to
    the sequence tempo, and pre-compensated for its tuning so that resampling the stretched
    audio by the tuning keeps the tempo (warp: tune changes pitch, not speed).
*/
double warpFactor (const Pad& pad, double bpm);

/** Re-stretches all warped pads of 'loaded' for another tempo (uses the cache). */
void applyWarp (LoadedProject& loaded, double bpm, SampleCache& cache);

// ---------------------------------------------------------------------------
// Audio-thread data

/** Plain per-pad playback data. Points into samples kept alive by the owning Kit. */
struct KitPad
{
    const SampleData* sample = nullptr;
    int start = 0;
    int end = 0;                 // exclusive
    int loopStart = 0;
    bool loop = false;
    double pitchRatio = 1.0;
    float gainL = 1.0f;
    float gainR = 1.0f;
    bool oneShot = true;
    int muteGroup = 0;
    bool monophonic = false;
    int output = 0;
    float releaseSeconds = 0.01f;
};

/** Immutable snapshot handed to the audio thread. */
struct Kit
{
    std::uint64_t sampleGeneration = 0;
    std::array<KitPad, kMaxPads> pads {};
    std::array<std::int16_t, 128> noteToPad {};
    std::vector<SamplePtr> keepAlive;
};

std::unique_ptr<Kit> buildKit (const LoadedProject& loaded, const OverrideMap& overrides);
} // namespace padporter::plugin
