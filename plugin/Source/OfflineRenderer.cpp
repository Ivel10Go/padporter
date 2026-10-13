#include "OfflineRenderer.h"
#include "BusCompressor.h"

#include <cmath>

namespace padporter::plugin
{
namespace
{
constexpr int kBlockSize = 512;
constexpr int kMaxPasses = 6;

struct TimedNote
{
    std::int64_t sample = 0;
    bool noteOn = false;
    int note = 0;
    float velocity = 0.0f;
    float semitones = 0.0f;
};
} // namespace

RenderResult renderSequence (const RenderRequest& request)
{
    RenderResult result;
    const auto* loaded = request.loaded;
    const auto* track = loaded != nullptr ? loaded->track() : nullptr;
    if (track == nullptr)
    {
        result.error = "Kein Projekt geladen.";
        return result;
    }

    const auto& project = *loaded->project;
    if (request.sequenceIndex < 0 || request.sequenceIndex >= static_cast<int> (project.sequences.size()))
    {
        result.error = juce::String::fromUTF8 ("Ungültige Sequence.");
        return result;
    }

    const auto& seq = project.sequences[static_cast<std::size_t> (request.sequenceIndex)];
    const auto* clip = seq.clipForTrack (track->name);
    if (clip == nullptr)
    {
        result.error = juce::String::fromUTF8 ("Diese Sequence enthält keine Noten.");
        return result;
    }

    const double sampleRate = request.sampleRate > 0.0 ? request.sampleRate : 48000.0;
    const double bpm = project.tempoFor (seq);
    const double samplesPerTick = sampleRate * 60.0 / (bpm * kTicksPerQuarter);
    const auto lengthTicks = std::max<std::int64_t> (96, seq.lengthTicks());
    const auto loopSamples = static_cast<std::int64_t> (std::llround (static_cast<double> (lengthTicks) * samplesPerTick));

    // Kit: included pads on output 0, all others on a discarded output (they still choke etc.).
    auto kit = buildKit (*loaded, request.overrides);
    double longestTailSeconds = 0.0;
    bool anyIncluded = false;
    for (int i = 0; i < kMaxPads; ++i)
    {
        auto& pad = kit->pads[static_cast<std::size_t> (i)];
        if (pad.sample == nullptr)
            continue;

        const bool include = ! request.includePad || request.includePad (i);
        pad.output = include ? 0 : 1;
        if (include)
        {
            anyIncluded = true;
            const auto seconds = static_cast<double> (pad.end - pad.start) / pad.sample->sampleRate / juce::jmax (0.01, pad.pitchRatio);
            longestTailSeconds = juce::jmax (longestTailSeconds, seconds + pad.releaseSeconds);
        }
    }

    if (! anyIncluded)
    {
        result.error = "Keine Pads mit Sample in dieser Auswahl.";
        return result;
    }

    const int passes = juce::jlimit (2, kMaxPasses, 1 + static_cast<int> (std::ceil (longestTailSeconds * sampleRate / static_cast<double> (loopSamples))));
    const auto totalSamples = loopSamples * passes;

    std::vector<TimedNote> notes;
    for (int pass = 0; pass < passes; ++pass)
    {
        const auto offset = loopSamples * pass;
        for (const auto& n : clip->notes)
        {
            if (n.muted || n.tick >= lengthTicks)
                continue;
            const auto on = offset + static_cast<std::int64_t> (std::llround (static_cast<double> (n.tick) * samplesPerTick));
            const auto off = offset + static_cast<std::int64_t> (std::llround (static_cast<double> (n.tick + std::max<std::int64_t> (1, n.length)) * samplesPerTick));
            notes.push_back ({ on, true, n.note, n.velocity127() / 127.0f, static_cast<float> (n.tuneSemitones()) });
            notes.push_back ({ juce::jmax (on + 1, off), false, n.note, 0.0f, 0.0f });
        }
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const TimedNote& a, const TimedNote& b) {
        return a.sample != b.sample ? a.sample < b.sample : (! a.noteOn && b.noteOn);
    });

    SamplerEngine engine;
    engine.prepare (sampleRate);
    engine.setKit (std::move (kit));

    juce::AudioBuffer<float> full (2, static_cast<int> (totalSamples));
    full.clear();
    juce::AudioBuffer<float> discard (2, kBlockSize);

    const juce::MidiBuffer noMidi;
    std::vector<SamplerEngine::ScheduledNote> blockNotes;
    std::size_t next = 0;
    for (std::int64_t pos = 0; pos < totalSamples; pos += kBlockSize)
    {
        const int num = static_cast<int> (std::min<std::int64_t> (kBlockSize, totalSamples - pos));

        blockNotes.clear();
        while (next < notes.size() && notes[next].sample < pos + num)
        {
            const auto& n = notes[next++];
            blockNotes.push_back ({ static_cast<int> (juce::jmax<std::int64_t> (0, n.sample - pos)), n.note, n.velocity, n.noteOn, n.semitones });
        }

        discard.clear();
        SamplerEngine::Outputs outs;
        for (int o = 0; o < SamplerEngine::kNumOutputs; ++o)
        {
            const bool main = o == 0;
            outs.left[static_cast<std::size_t> (o)] = main ? full.getWritePointer (0, static_cast<int> (pos)) : discard.getWritePointer (0);
            outs.right[static_cast<std::size_t> (o)] = main ? full.getWritePointer (1, static_cast<int> (pos)) : discard.getWritePointer (1);
        }
        engine.render (outs, num, noMidi, request.params, blockNotes.data(), static_cast<int> (blockNotes.size()));
    }
    engine.collectGarbage();

    // Master compressor over all passes, so its state is settled when the kept pass starts.
    if (request.applyCompressor)
    {
        BusCompressor compressor;
        compressor.prepare (sampleRate);
        compressor.setColour (request.compressorColour);
        compressor.process (full.getWritePointer (0), full.getWritePointer (1), full.getNumSamples());
    }

    result.audio.setSize (2, static_cast<int> (loopSamples));
    for (int ch = 0; ch < 2; ++ch)
        result.audio.copyFrom (ch, 0, full, ch, static_cast<int> (loopSamples * (passes - 1)), static_cast<int> (loopSamples));

    if (result.audio.getMagnitude (0, result.audio.getNumSamples()) < 1.0e-6f)
    {
        result.error = juce::String::fromUTF8 ("Diese Auswahl ist in der Sequence stumm (keine Noten auf Pads mit Sample).");
        return result;
    }

    result.ok = true;
    result.sampleRate = sampleRate;
    result.bpm = bpm;
    result.numerator = seq.timeSignature.beatsPerBar;
    result.denominator = seq.timeSignature.beatUnit;
    result.beats = static_cast<int> (std::llround (static_cast<double> (lengthTicks) / juce::jmax (1, seq.timeSignature.ticksPerBeat)));
    return result;
}

juce::File writeRender (const RenderResult& result, const juce::File& directory, const juce::String& baseName, juce::String& error)
{
    if (! directory.createDirectory())
    {
        error = "Ordner kann nicht angelegt werden: " + directory.getFullPathName();
        return {};
    }

    juce::StringPairArray metadata;
    metadata.set (juce::WavAudioFormat::acidOneShot, "0");
    metadata.set (juce::WavAudioFormat::acidRootSet, "0");
    metadata.set (juce::WavAudioFormat::acidStretch, "1");
    metadata.set (juce::WavAudioFormat::acidDiskBased, "0");
    metadata.set (juce::WavAudioFormat::acidizerFlag, "0");
    metadata.set (juce::WavAudioFormat::acidRootNote, "60");
    metadata.set (juce::WavAudioFormat::acidBeats, juce::String (result.beats));
    metadata.set (juce::WavAudioFormat::acidNumerator, juce::String (result.numerator));
    metadata.set (juce::WavAudioFormat::acidDenominator, juce::String (result.denominator));
    metadata.set (juce::WavAudioFormat::acidTempo, juce::String (result.bpm, 3));

    juce::MemoryBlock bytes;
    {
        auto* stream = new juce::MemoryOutputStream (bytes, false);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream, result.sampleRate, 2, 32, metadata, 0));
        if (writer == nullptr)
        {
            delete stream;
            error = "WAV-Datei konnte nicht erzeugt werden.";
            return {};
        }
        writer->writeFromAudioSampleBuffer (result.audio, 0, result.audio.getNumSamples());
    }

    for (int i = 1; i < 100; ++i)
    {
        const auto name = baseName + (i == 1 ? juce::String() : " (" + juce::String (i) + ")") + ".wav";
        const auto file = directory.getChildFile (juce::File::createLegalFileName (name));

        if (! file.existsAsFile())
        {
            if (! file.replaceWithData (bytes.getData(), bytes.getSize()))
            {
                error = "Datei konnte nicht geschrieben werden: " + file.getFullPathName();
                return {};
            }
            return file;
        }

        juce::MemoryBlock existing;
        if (file.loadFileAsData (existing) && existing == bytes)
            return file; // identical render already exists
    }

    error = "Zu viele Varianten dieser Datei vorhanden.";
    return {};
}
} // namespace padporter::plugin
