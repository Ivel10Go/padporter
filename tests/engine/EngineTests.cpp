// Tests for the plugin's sample loading and real-time sampler engine (no host required).

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "BusCompressor.h"
#include "Fixtures.h"
#include "OfflineRenderer.h"
#include "TimeStretch.h"
#include "SampleKit.h"
#include "SamplerEngine.h"

#include <padporter/WavInfo.h>

#include <atomic>
#include <cstdlib>
#include <new>

// ---------------------------------------------------------------------------
// Allocation tracking: verifies that the audio path never allocates.

namespace
{
std::atomic<bool> trackAllocations { false };
std::atomic<int> allocationCount { 0 };
} // namespace

void* operator new (std::size_t size)
{
    if (trackAllocations.load (std::memory_order_relaxed))
        allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size > 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

// ---------------------------------------------------------------------------

using namespace padporter::plugin;

namespace
{
constexpr int kBlock = 512;

struct Rig
{
    explicit Rig (const nlohmann::json& project = fixtures::sampleProjectJson(), bool withSamples = true)
    {
        const auto xpj = fixtures::createProjectOnDisk (tmp.path, "Rig", project, withSamples);
        auto outcome = loadProject (toJuceFile (xpj), -1, cache);
        REQUIRE (outcome.ok);
        loaded = std::move (outcome.loaded);
        loaded->generation = 1;

        engine.prepare (48000.0);
        engine.setKit (buildKit (*loaded, {}));
    }

    SamplerEngine::Outputs outputs (bool multi)
    {
        SamplerEngine::Outputs outs;
        for (int o = 0; o < SamplerEngine::kNumOutputs; ++o)
        {
            const bool second = multi && o == 1;
            outs.left[static_cast<std::size_t> (o)] = buffer.getWritePointer (second ? 2 : 0);
            outs.right[static_cast<std::size_t> (o)] = buffer.getWritePointer (second ? 3 : 1);
        }
        return outs;
    }

    /** Renders one block and returns the RMS of the given channel. Allocation-free. */
    float render (const juce::MidiBuffer& midi, bool multi = false, int channel = 0)
    {
        buffer.clear();
        const auto outs = outputs (multi);
        engine.render (outs, kBlock, midi, {});
        return buffer.getRMSLevel (channel, 0, kBlock);
    }

    bool padActive (int pad) const { return (engine.getActivity (pad / 64) >> (pad % 64)) & 1u; }

    fixtures::TempDir tmp;
    SampleCache cache;
    std::unique_ptr<LoadedProject> loaded;
    SamplerEngine engine;
    juce::AudioBuffer<float> buffer { 4, kBlock };
    juce::MidiBuffer empty;
};

juce::MidiBuffer noteOn (int note, int velocity = 100)
{
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (velocity)), 0);
    return m;
}

juce::MidiBuffer noteOff (int note)
{
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
    return m;
}
} // namespace

TEST_CASE ("project loads and the kit maps notes to pads")
{
    Rig rig;
    REQUIRE (rig.loaded->trackIndex == 0);
    CHECK (rig.loaded->pads[0].sample != nullptr);
    CHECK (rig.loaded->pads[1].sample != nullptr); // non-ASCII file name
    CHECK (rig.loaded->pads[3].sample == nullptr);

    const auto kit = buildKit (*rig.loaded, {});
    CHECK (kit->noteToPad[37] == 0);
    CHECK (kit->noteToPad[36] == 1);
    CHECK (kit->noteToPad[42] == 2);
    CHECK (kit->noteToPad[39] == -1);
    CHECK (kit->pads[0].pitchRatio == doctest::Approx (std::pow (2.0, 1.9 / 12.0)));
    CHECK_FALSE (kit->pads[1].oneShot);
}

TEST_CASE ("note-on produces sound and rendering does not allocate")
{
    Rig rig;
    const auto on = noteOn (37);

    allocationCount = 0;
    trackAllocations = true;
    const float first = rig.render (on);    // also swaps in the pending kit
    const float second = rig.render (rig.empty);
    trackAllocations = false;

    CHECK (allocationCount.load() == 0);
    CHECK (first > 0.0f);
    CHECK (second > 0.0f);
    CHECK (rig.padActive (0));
}

TEST_CASE ("unmapped notes are silent")
{
    Rig rig;
    CHECK (rig.render (noteOn (100)) == 0.0f);
}

TEST_CASE ("gate pads stop on note-off, one-shot pads keep playing")
{
    auto project = fixtures::sampleProjectJson();
    project["data"]["tracks"][0]["program"]["drum"]["instruments"][1]["synthSection"]["ampEnvelope"]["Release"]["value0"] = 0.0;
    Rig rig (project);

    rig.render (noteOn (36));       // gate pad
    rig.render (noteOn (37));       // one-shot pad
    CHECK (rig.padActive (1));
    CHECK (rig.padActive (0));

    rig.render (noteOff (36));
    rig.render (noteOff (37));
    rig.render (rig.empty);

    CHECK_FALSE (rig.padActive (1));
    CHECK (rig.padActive (0));
}

TEST_CASE ("pads in the same mute group choke each other")
{
    auto project = fixtures::sampleProjectJson();
    project["data"]["tracks"][0]["program"]["drum"]["instruments"][0]["whichMuteGroup"] = 1;
    Rig rig (project);

    rig.render (noteOn (37));
    CHECK (rig.padActive (0));

    rig.render (noteOn (42));       // hat, same group
    rig.render (rig.empty);
    CHECK_FALSE (rig.padActive (0));
    CHECK (rig.padActive (2));
}

TEST_CASE ("multi-out routes a pad to its own output")
{
    Rig rig;
    PadOverride ov;
    ov.output = 1;
    rig.engine.setKit (buildKit (*rig.loaded, { { 0, ov } }));

    const auto on = noteOn (37);
    rig.buffer.clear();
    const auto outs = rig.outputs (true);
    rig.engine.render (outs, kBlock, on, {});

    CHECK (rig.buffer.getRMSLevel (2, 0, kBlock) > 0.0f);
    CHECK (rig.buffer.getRMSLevel (0, 0, kBlock) == 0.0f);
}

TEST_CASE ("overrides change playback values")
{
    Rig rig;
    PadOverride ov;
    ov.tuneSemitones = 12.0;
    ov.gainDb = -6.0;
    ov.oneShot = false;
    const auto kit = buildKit (*rig.loaded, { { 0, ov } });

    CHECK (kit->pads[0].pitchRatio == doctest::Approx (2.0));
    CHECK (kit->pads[0].gainL == doctest::Approx (juce::Decibels::decibelsToGain (-6.0f)));
    CHECK_FALSE (kit->pads[0].oneShot);
}

TEST_CASE ("kits can be swapped repeatedly while playing")
{
    Rig rig;
    rig.render (noteOn (37));

    for (int i = 0; i < 100; ++i)
    {
        rig.loaded->generation = static_cast<std::uint64_t> (1 + i % 2);
        rig.engine.setKit (buildKit (*rig.loaded, {}));
        rig.render (i % 3 == 0 ? noteOn (42) : rig.empty);
        if (i % 10 == 0)
            rig.engine.collectGarbage();
    }
    rig.engine.collectGarbage();
    CHECK (true);
}

TEST_CASE ("sequence preview plays a looping pattern without allocating")
{
    Rig rig;
    auto pattern = std::make_unique<SamplerEngine::Pattern>();
    pattern->bpm = 120.0;
    pattern->lengthTicks = 960;   // one beat = 0.5 s at 120 BPM
    pattern->id = 7;
    pattern->events = { { 0, 37, 1.0f, true }, { 100, 37, 0.0f, false } };
    rig.engine.setPattern (std::move (pattern));

    allocationCount = 0;
    trackAllocations = true;
    const float first = rig.render (rig.empty);
    trackAllocations = false;

    CHECK (allocationCount.load() == 0);
    CHECK (first > 0.0f);
    CHECK (rig.engine.getPlayingPatternId() == 7);

    // After ~0.57 s the pattern has looped (0.5 s) and the 0.1 s sample is sounding again.
    for (int i = 0; i < 52; ++i)
        rig.render (rig.empty);
    CHECK (rig.engine.getPatternPosition() < 0.5f);
    CHECK (rig.padActive (0));

    rig.engine.setPattern (nullptr);
    rig.render (rig.empty);
    CHECK (rig.engine.getPlayingPatternId() == -1);
    rig.engine.collectGarbage();
}

TEST_CASE ("offline render produces one seamless loop and pad stems add up to the mix")
{
    Rig rig;
    auto render = [&rig] (std::function<bool (int)> include) {
        RenderRequest request;
        request.loaded = rig.loaded.get();
        request.sequenceIndex = 0;   // "Intro": 2 bars 4/4 at 128 BPM
        request.sampleRate = 48000.0;
        request.includePad = std::move (include);
        return renderSequence (request);
    };

    const auto all = render (nullptr);
    REQUIRE (all.ok);
    CHECK (all.audio.getNumChannels() == 2);
    CHECK (all.audio.getNumSamples() == 180000);   // 7680 ticks * 48000 * 60 / (128 * 960)
    CHECK (all.bpm == doctest::Approx (128.0));
    CHECK (all.beats == 8);

    const auto kick = render ([] (int pad) { return pad == 0; });
    const auto snare = render ([] (int pad) { return pad == 1; });
    REQUIRE (kick.ok);
    REQUIRE (snare.ok);

    float maxDifference = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < all.audio.getNumSamples(); ++i)
            maxDifference = juce::jmax (maxDifference, std::abs (all.audio.getSample (ch, i) - kick.audio.getSample (ch, i) - snare.audio.getSample (ch, i)));
    CHECK (maxDifference < 1.0e-4f);

    // The hat note in this sequence is muted: rendering only that pad is reported as silent.
    const auto hat = render ([] (int pad) { return pad == 2; });
    CHECK_FALSE (hat.ok);
    CHECK (hat.error.isNotEmpty());
}

TEST_CASE ("rendered WAV files are valid and never overwritten with different audio")
{
    Rig rig;
    RenderRequest request;
    request.loaded = rig.loaded.get();
    request.sampleRate = 44100.0;
    const auto result = renderSequence (request);
    REQUIRE (result.ok);

    const auto dir = toJuceFile (rig.tmp.path / "renders");
    juce::String error;
    const auto first = writeRender (result, dir, "01 Intro - Alles", error);
    REQUIRE (first.existsAsFile());

    const auto info = padporter::readWavInfo (toStdPath (first));
    CHECK (info.valid);
    CHECK (info.channels == 2);
    CHECK (info.bitsPerSample == 32);
    CHECK (info.sampleRate == 44100);
    CHECK (info.frames == result.audio.getNumSamples());

    juce::MemoryBlock bytes;
    first.loadFileAsData (bytes);
    const auto* begin = static_cast<const char*> (bytes.getData());
    const char tag[] = { 'a', 'c', 'i', 'd' };
    CHECK (std::search (begin, begin + bytes.getSize(), tag, tag + 4) != begin + bytes.getSize());

    // Same audio -> same file; different audio -> new numbered file, the first stays untouched.
    CHECK (writeRender (result, dir, "01 Intro - Alles", error) == first);

    auto quieter = result;
    quieter.audio.applyGain (0.5f);
    const auto second = writeRender (quieter, dir, "01 Intro - Alles", error);
    CHECK (second != first);
    CHECK (second.getFileName() == "01 Intro - Alles (2).wav");

    juce::MemoryBlock after;
    first.loadFileAsData (after);
    CHECK (after == bytes);
}

namespace
{
juce::AudioBuffer<float> sine (double frequency, double sampleRate, int length)
{
    juce::AudioBuffer<float> b (2, length);
    for (int i = 0; i < length; ++i)
    {
        const auto v = static_cast<float> (0.5 * std::sin (juce::MathConstants<double>::twoPi * frequency * i / sampleRate));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

/** Frequency estimate from positive zero crossings in the middle half of a buffer. */
double estimateFrequency (const juce::AudioBuffer<float>& b, double sampleRate)
{
    const int from = b.getNumSamples() / 4, to = 3 * b.getNumSamples() / 4;
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if (b.getSample (0, i - 1) < 0.0f && b.getSample (0, i) >= 0.0f)
            ++crossings;
    return crossings * sampleRate / (to - from);
}
} // namespace

TEST_CASE ("time stretch changes length but keeps pitch")
{
    const double sr = 44100.0;
    const auto input = sine (440.0, sr, 44100);

    for (double factor : { 0.5, 0.75, 1.5 })
    {
        CAPTURE (factor);
        const auto out = timeStretch (input, sr, factor);
        CHECK (std::abs (out.getNumSamples() - static_cast<int> (44100 * factor)) <= 1);
        CHECK (estimateFrequency (out, sr) == doctest::Approx (440.0).epsilon (0.02));
        CHECK (out.getMagnitude (0, out.getNumSamples() / 4, out.getNumSamples() / 2) > 0.3f);
    }
}

TEST_CASE ("master compressor reduces loud signals, keeps quiet ones and does not allocate")
{
    BusCompressor comp;
    comp.prepare (48000.0);

    auto loud = sine (100.0, 48000.0, 48000);
    allocationCount = 0;
    trackAllocations = true;
    comp.process (loud.getWritePointer (0), loud.getWritePointer (1), loud.getNumSamples());
    trackAllocations = false;
    CHECK (allocationCount.load() == 0);
    CHECK (comp.getGainReductionDb() < -3.0);
    CHECK (loud.getMagnitude (0, 24000, 24000) < 0.5f);       // input peak 0.5 (-6 dBFS) got reduced

    comp.reset();
    auto quiet = sine (100.0, 48000.0, 48000);
    quiet.applyGain (0.01f);                                   // -46 dBFS: below threshold
    comp.process (quiet.getWritePointer (0), quiet.getWritePointer (1), quiet.getNumSamples());
    CHECK (quiet.getMagnitude (0, 24000, 24000) == doctest::Approx (0.005f * std::pow (10.0f, 3.0f / 20.0f)).epsilon (0.02));
}

TEST_CASE ("warped pads are stretched to the sequence tempo, gain chain is applied")
{
    auto project = fixtures::sampleProjectJson();
    auto& kick = project["data"]["tracks"][0]["program"]["drum"]["instruments"][0];
    kick["warpEnable"] = true;
    kick["tempo"] = 60.0;
    project["data"]["tracks"][0]["program"]["mixable"] = { { "volume", 0.5 } };
    project["data"]["mixer"] = { { "outputs", nlohmann::json::array ({ { { "mixable", { { "volume", 0.5 } } } } }) } };
    Rig rig (project);

    CHECK (rig.loaded->targetBpm == doctest::Approx (128.0));   // "Intro" is the first sequence with notes
    const auto* pad = rig.loaded->pad (0);
    const auto factor = warpFactor (*pad, 128.0);
    CHECK (factor == doctest::Approx (60.0 / 128.0 * std::pow (2.0, 1.9 / 12.0)));
    CHECK (rig.loaded->pads[0].sample->timeScale == doctest::Approx (factor));
    CHECK (std::abs (rig.loaded->pads[0].sample->audio.getNumSamples() - static_cast<int> (4410 * factor)) <= 1);
    CHECK (rig.loaded->pads[1].sample->timeScale == doctest::Approx (1.0));   // not warped

    // Program fader 0.5 x main output 0.5 -> -12 dB on top of the pad gain.
    const auto kit = buildKit (*rig.loaded, {});
    const auto padGain = std::pow (10.0, std::log10 (0.7079457640647888) );   // pad volume, centre pan
    CHECK (kit->pads[1].gainL == doctest::Approx (padGain * 0.25).epsilon (0.001));

    // Re-stretching for another tempo replaces only the warped sample.
    auto copy = *rig.loaded;
    applyWarp (copy, 64.0, rig.cache);
    CHECK (copy.targetBpm == doctest::Approx (64.0));
    CHECK (copy.pads[0].sample->timeScale == doctest::Approx (warpFactor (*pad, 64.0)));
    CHECK (copy.pads[1].sample == rig.loaded->pads[1].sample);
}

TEST_CASE ("16 Levels notes are rendered at their recorded pitch")
{
    auto renderWith = [] (double modifierValue) {
        auto j = fixtures::sampleProjectJson();
        j["data"]["sequences"][1]["value"]["trackClipMaps"][0][0]["value"]["eventList"]["events"] = nlohmann::json::array ({
            { { "type", 3 }, { "time", 0 },
              { "note", { { "note", 37 }, { "velocity", 1.0 }, { "length", 100 },
                          { "modifierActiveState0", true }, { "modifierValue0", modifierValue } } } } });
        Rig rig (j);
        RenderRequest request;
        request.loaded = rig.loaded.get();
        request.sampleRate = 44100.0;
        return renderSequence (request);
    };

    auto soundingLength = [] (const RenderResult& r) {
        int last = 0;
        for (int i = 0; i < r.audio.getNumSamples(); ++i)
            if (std::abs (r.audio.getSample (0, i)) > 1.0e-4f)
                last = i;
        return last;
    };

    const auto original = renderWith (0.5);
    const auto octaveUp = renderWith (0.5 + 12.0 / 240.0);
    REQUIRE (original.ok);
    REQUIRE (octaveUp.ok);

    const double ratio = static_cast<double> (soundingLength (octaveUp)) / soundingLength (original);
    CHECK (ratio == doctest::Approx (0.5).epsilon (0.05));
}

TEST_CASE ("missing samples are reported, loading still succeeds")
{
    Rig rig (fixtures::sampleProjectJson(), false);
    CHECK (rig.loaded->pads[0].sample == nullptr);
    CHECK_FALSE (rig.loaded->pads[0].found);
    CHECK (rig.loaded->diagnostics.count (padporter::Severity::Warning) >= 3);
    CHECK (rig.render (noteOn (37)) == 0.0f);
}

int main (int argc, char** argv)
{
    doctest::Context context;
    context.applyCommandLine (argc, argv);
    return context.run();
}
