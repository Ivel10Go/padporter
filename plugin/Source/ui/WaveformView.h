#pragma once

#include "../SampleKit.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace padporter::plugin
{
/** Min/max waveform of a decoded sample with play range and slice markers. */
class WaveformView final : public juce::Component
{
public:
    void setSample (SamplePtr sample, int start, int end);
    void setMessage (const juce::String& text);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuildPeaks();

    SamplePtr sample;
    int rangeStart = 0;
    int rangeEnd = 0;
    juce::String message;
    std::vector<float> minima, maxima;
};
} // namespace padporter::plugin
