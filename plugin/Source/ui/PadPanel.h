#pragma once

#include "PadGrid.h"
#include "WaveformView.h"

#include <array>

namespace padporter::plugin
{
class PadPorterProcessor;

/** Lower hardware area: bank buttons, the 4x4 pads and the selected pad with its part assignment. */
class PadPanel final : public juce::Component
{
public:
    explicit PadPanel (PadPorterProcessor& processor);

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> cardBounds() const;

    PadPorterProcessor& processor;
    std::array<juce::TextButton, 8> bankButtons;
    PadGrid pads;
    juce::TextButton autoButton { "AUTO" }, drumsButton { "DRUMS" }, melodyButton { "MELODIE" };
    WaveformView waveform;
};
} // namespace padporter::plugin
