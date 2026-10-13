#include "SampleKit.h"
#include "TimeStretch.h"

#include <padporter/PathUtil.h>
#include <padporter/SampleResolver.h>
#include <padporter/WavInfo.h>
#include <padporter/XpjReader.h>

#include <cmath>

namespace padporter::plugin
{
namespace
{
constexpr double kMaxSampleSeconds = 600.0;
}

std::filesystem::path toStdPath (const juce::File& f)
{
    return pathFromUtf8 (f.getFullPathName().toStdString());
}

juce::File toJuceFile (const std::filesystem::path& p)
{
    auto preferred = p;
    preferred.make_preferred();
    return juce::File (fromUtf8 (pathToUtf8 (preferred)));
}

// ---------------------------------------------------------------------------

SamplePtr SampleCache::get (const juce::File& file, bool reversed, double stretch, juce::String& error)
{
    const bool stretched = std::abs (stretch - 1.0) >= 1.0e-4;
    const auto key = file.getFullPathName() + (reversed ? "|R" : "|F") + (stretched ? "|" + juce::String (stretch, 5) : juce::String());
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (auto it = entries.find (key); it != entries.end())
            return it->second;
    }

    if (stretched)
    {
        // Stretch the forward audio, then reverse if needed (same result as the device order).
        const auto original = get (file, false, 1.0, error);
        if (original == nullptr)
            return {};

        auto data = std::make_shared<SampleData>();
        data->audio = timeStretch (original->audio, original->sampleRate, stretch);
        data->sampleRate = original->sampleRate;
        data->reversed = reversed;
        data->path = original->path;
        data->timeScale = stretch;
        for (auto p : original->slicePoints)
            data->slicePoints.push_back (static_cast<std::int64_t> (std::llround (static_cast<double> (p) * stretch)));
        if (reversed)
            data->audio.reverse (0, data->audio.getNumSamples());

        std::lock_guard<std::mutex> lock (mutex);
        entries[key] = data;
        return data;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file";
        return {};
    }

    const auto sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    const auto maxFrames = static_cast<juce::int64> (sampleRate * kMaxSampleSeconds);
    const auto frames = static_cast<int> (juce::jlimit<juce::int64> (0, juce::jmin<juce::int64> (maxFrames, std::numeric_limits<int>::max()),
                                                                      reader->lengthInSamples));
    if (frames <= 0)
    {
        error = "Empty audio file";
        return {};
    }

    auto data = std::make_shared<SampleData>();
    data->audio.setSize (reader->numChannels > 1 ? 2 : 1, frames);
    reader->read (&data->audio, 0, frames, 0, true, true);
    data->sampleRate = sampleRate;
    data->reversed = reversed;
    data->path = file.getFullPathName();

    if (reversed)
        data->audio.reverse (0, frames);

    for (const auto& s : readWavInfo (toStdPath (file)).slices)
        data->slicePoints.push_back (s.start);

    std::lock_guard<std::mutex> lock (mutex);
    entries[key] = data;
    return data;
}

void SampleCache::clear()
{
    std::lock_guard<std::mutex> lock (mutex);
    entries.clear();
}

// ---------------------------------------------------------------------------

EffectivePad computeEffectivePad (const Pad& pad, const PadOverride* ov)
{
    EffectivePad e;
    if (pad.hasSample())
    {
        const auto& layer = pad.layers.front();
        e.tuneSemitones = padTuneSemitones (pad);
        const auto gain = pad.volume * layer.gain;
        e.gainDb = gain > 0.0 ? 20.0 * std::log10 (gain) : -60.0;
        e.pan = juce::jlimit (0.0, 1.0, pad.pan + layer.pan - 0.5);
    }
    e.oneShot = pad.oneShot;
    e.muteGroup = pad.muteGroup;
    e.output = pad.padInBank();

    if (ov != nullptr)
    {
        e.tuneSemitones = ov->tuneSemitones.value_or (e.tuneSemitones);
        e.gainDb = ov->gainDb.value_or (e.gainDb);
        e.pan = ov->pan.value_or (e.pan);
        e.oneShot = ov->oneShot.value_or (e.oneShot);
        e.muteGroup = ov->muteGroup.value_or (e.muteGroup);
        e.output = ov->output.value_or (e.output);
    }
    return e;
}

const Track* LoadedProject::track() const noexcept
{
    if (project == nullptr || trackIndex < 0 || trackIndex >= static_cast<int> (project->tracks.size()))
        return nullptr;
    return &project->tracks[static_cast<std::size_t> (trackIndex)];
}

const Pad* LoadedProject::pad (int index) const noexcept
{
    const auto* t = track();
    if (t == nullptr || index < 0 || index >= static_cast<int> (t->pads.size()))
        return nullptr;
    return &t->pads[static_cast<std::size_t> (index)];
}

// ---------------------------------------------------------------------------

double padTuneSemitones (const Pad& pad)
{
    if (! pad.hasSample())
        return 0.0;
    const auto& layer = pad.layers.front();
    return pad.instCoarseTune + layer.coarseTune + (pad.instFineTune + layer.fineTune) / 100.0;
}

double warpFactor (const Pad& pad, double bpm)
{
    if (! pad.warpEnable || pad.tempo < 1.0 || bpm < 1.0)
        return 1.0;
    // Length must become sampleTempo/bpm; resampling by the tuning ratio afterwards shortens it again.
    return (pad.tempo / bpm) * std::pow (2.0, padTuneSemitones (pad) / 12.0);
}

namespace
{
double defaultTempo (const Project& project, int trackIndex)
{
    const auto* track = trackIndex >= 0 ? &project.tracks[static_cast<std::size_t> (trackIndex)] : nullptr;
    for (const auto& seq : project.sequences)
        if (const auto* clip = track != nullptr ? seq.clipForTrack (track->name) : nullptr; clip != nullptr && clip->playableNoteCount() > 0)
            return project.tempoFor (seq);
    return project.sequences.empty() ? project.masterTempo : project.tempoFor (project.sequences.front());
}
} // namespace

void applyWarp (LoadedProject& loaded, double bpm, SampleCache& cache)
{
    loaded.targetBpm = bpm;
    const auto* track = loaded.track();
    if (track == nullptr)
        return;

    for (const auto& pad : track->pads)
    {
        if (! pad.hasSample() || ! pad.warpEnable || pad.index < 0 || pad.index >= kMaxPads)
            continue;
        auto& rt = loaded.pads[static_cast<std::size_t> (pad.index)];
        if (! rt.found)
            continue;
        juce::String error;
        if (auto sample = cache.get (juce::File (rt.resolvedPath), pad.layers.front().reverse, warpFactor (pad, bpm), error))
            rt.sample = std::move (sample);
    }
}

LoadOutcome loadProject (const juce::File& xpj, int requestedTrack, SampleCache& cache, double targetBpm)
{
    LoadOutcome out;
    try
    {
        auto result = XpjReader::load (toStdPath (xpj));
        auto loaded = std::make_unique<LoadedProject>();
        loaded->file = xpj;
        loaded->diagnostics = std::move (result.diagnostics);
        auto project = std::make_shared<Project> (std::move (result.project));

        int track = requestedTrack;
        if (track < 0 || track >= static_cast<int> (project->tracks.size())
            || project->tracks[static_cast<std::size_t> (track)].kind != TrackKind::Drum)
            track = project->defaultDrumTrack();
        loaded->trackIndex = track;
        loaded->targetBpm = targetBpm > 0.0 ? targetBpm : defaultTempo (*project, track);

        if (track >= 0)
        {
            SampleResolver resolver (*project);
            for (const auto& pad : project->tracks[static_cast<std::size_t> (track)].pads)
            {
                if (! pad.hasSample() || pad.index < 0 || pad.index >= kMaxPads)
                    continue;

                const auto& layer = pad.layers.front();
                auto& rt = loaded->pads[static_cast<std::size_t> (pad.index)];
                const auto res = resolver.resolve (layer.sampleFile);
                if (! res.found)
                {
                    rt.error = "Sample file not found";
                    loaded->diagnostics.warning ("samples", "missing sample: " + layer.sampleFile);
                    continue;
                }

                const auto file = toJuceFile (res.path);
                rt.found = true;
                rt.resolvedPath = file.getFullPathName();
                rt.sample = cache.get (file, layer.reverse, warpFactor (pad, loaded->targetBpm), rt.error);
                if (rt.sample == nullptr)
                    loaded->diagnostics.warning ("samples", layer.sampleFile + ": " + rt.error.toStdString());
            }
        }
        else
        {
            loaded->diagnostics.warning ("tracks", "the project contains no drum track");
        }

        if (track >= 0)
        {
            auto* rawLoaded = loaded.get();
            loaded->autoRoles = classifyTrack (*project, project->tracks[static_cast<std::size_t> (track)], [rawLoaded] (const Pad& pad) {
                const auto& sample = rawLoaded->pads[static_cast<std::size_t> (pad.index)].sample;
                return sample != nullptr ? sample->audio.getNumSamples() / sample->sampleRate : -1.0;
            });
        }

        loaded->project = std::move (project);
        out.loaded = std::move (loaded);
        out.ok = true;
    }
    catch (const std::exception& e)
    {
        out.error = fromUtf8 (e.what());
    }
    return out;
}

RoleMap effectiveRoles (const LoadedProject& loaded, const OverrideMap& overrides)
{
    auto roles = loaded.autoRoles;
    for (const auto& [index, ov] : overrides)
        if (ov.role && index >= 0 && index < kMaxPads)
            roles[static_cast<std::size_t> (index)] = *ov.role;
    return roles;
}

// ---------------------------------------------------------------------------

std::unique_ptr<Kit> buildKit (const LoadedProject& loaded, const OverrideMap& overrides)
{
    auto kit = std::make_unique<Kit>();
    kit->sampleGeneration = loaded.generation;
    kit->noteToPad.fill (-1);

    const auto* track = loaded.track();
    if (track == nullptr)
        return kit;

    for (const auto& pad : track->pads)
    {
        if (pad.index < 0 || pad.index >= kMaxPads || ! pad.hasSample() || pad.muted)
            continue;

        const auto& rt = loaded.pads[static_cast<std::size_t> (pad.index)];
        if (rt.sample == nullptr)
            continue;

        const auto it = overrides.find (pad.index);
        const auto eff = computeEffectivePad (pad, it != overrides.end() ? &it->second : nullptr);
        const auto& layer = pad.layers.front();
        const auto length = static_cast<std::int64_t> (rt.sample->audio.getNumSamples());

        // Slice points refer to the original sample; warped samples are scaled.
        const double scale = rt.sample->timeScale;
        const auto scaled = [scale] (std::int64_t frame) { return static_cast<std::int64_t> (std::llround (static_cast<double> (frame) * scale)); };
        const auto sliceStart = scaled (layer.slice.start);
        const auto sliceEnd = scaled (layer.slice.end);

        auto start = juce::jlimit<std::int64_t> (0, length, sliceStart);
        auto end = (sliceEnd > start && sliceEnd <= length) ? sliceEnd : length;
        if (end - start < 2)
        {
            start = 0;
            end = length;
        }
        auto loopStart = juce::jlimit<std::int64_t> (start, juce::jmax (start, end - 2), scaled (layer.slice.loopStart));

        if (rt.sample->reversed)
        {
            const auto s = length - end;
            end = length - start;
            start = s;
            loopStart = start;
        }

        KitPad kp;
        kp.sample = rt.sample.get();
        kp.start = static_cast<int> (start);
        kp.end = static_cast<int> (end);
        kp.loopStart = static_cast<int> (loopStart);
        kp.loop = layer.loop;
        kp.pitchRatio = std::pow (2.0, eff.tuneSemitones / 12.0);

        // Device gain chain: pad -> program fader -> track fader -> main output fader.
        const auto chain = static_cast<float> (track->programVolume * track->volume * loaded.project->masterVolume);
        const auto gain = juce::Decibels::decibelsToGain (static_cast<float> (eff.gainDb), -60.0f) * chain;
        const auto pan = static_cast<float> (eff.pan);
        kp.gainL = gain * juce::jmin (1.0f, 2.0f * (1.0f - pan));
        kp.gainR = gain * juce::jmin (1.0f, 2.0f * pan);
        kp.oneShot = eff.oneShot;
        kp.muteGroup = eff.muteGroup;
        kp.monophonic = pad.monophonic;
        kp.output = juce::jlimit (0, 15, eff.output);
        // Assumption (unverified): the stored release value maps linearly to 0..2 s.
        kp.releaseSeconds = static_cast<float> (juce::jlimit (0.005, 4.0, pad.ampEnvelope.release * 2.0));

        kit->pads[static_cast<std::size_t> (pad.index)] = kp;
        kit->keepAlive.push_back (rt.sample);

        auto& slot = kit->noteToPad[static_cast<std::size_t> (pad.midiNote)];
        if (slot < 0)
            slot = static_cast<std::int16_t> (pad.index);
    }

    return kit;
}
} // namespace padporter::plugin
