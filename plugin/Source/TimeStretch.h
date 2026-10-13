#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace padporter::plugin
{
/**
    Changes the length of audio without changing its pitch (WSOLA: overlap-add of
    waveform-aligned frames). Used offline when samples are loaded, to reproduce the
    device's "warp" for loops.

    factor = output length / input length (e.g. 0.5 = twice as fast).
*/
juce::AudioBuffer<float> timeStretch (const juce::AudioBuffer<float>& input, double sampleRate, double factor);
} // namespace padporter::plugin
