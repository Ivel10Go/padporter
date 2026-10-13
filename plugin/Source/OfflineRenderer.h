#pragma once

#include "SampleKit.h"
#include "SamplerEngine.h"

#include <functional>

namespace padporter::plugin
{
/** What to render: one sequence of the loaded drum track, optionally only some pads. */
struct RenderRequest
{
    const LoadedProject* loaded = nullptr;
    OverrideMap overrides;
    int sequenceIndex = 0;
    std::function<bool (int padIndex)> includePad;   // nullptr = all pads
    double sampleRate = 48000.0;
    SamplerEngine::Params params;
    bool applyCompressor = false;     // emulate the device's master compressor
    bool compressorColour = false;
};

struct RenderResult
{
    bool ok = false;
    juce::String error;
    juce::AudioBuffer<float> audio;   // stereo, exactly one loop of the sequence
    double sampleRate = 48000.0;
    double bpm = 120.0;
    int beats = 4;
    int numerator = 4;
    int denominator = 4;
};

/**
    Renders a sequence offline with the same sampler engine that plays in the plugin.

    The loop is rendered several times and only the last pass is kept, so tails of notes at
    the loop end continue at its start: the result loops seamlessly. Pads that are not
    included still play (silently routed away), so mute groups behave exactly as in the full
    mix and the stems of all pads add up to the full render.
*/
RenderResult renderSequence (const RenderRequest& request);

/**
    Writes a 32-bit float WAV with ACID tempo/beat info into 'directory'.
    An identical existing file is reused; a different file with the same name is never
    overwritten (a numbered variant is created instead), so clips already used in a host
    never change.
*/
juce::File writeRender (const RenderResult& result, const juce::File& directory, const juce::String& baseName, juce::String& error);
} // namespace padporter::plugin
