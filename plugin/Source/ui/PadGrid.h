#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace padporter::plugin
{
class PadPorterProcessor;

/**
    The black pad bed with the 4x4 pads of the current bank and their printed labels,
    laid out like the hardware (pad 1 bottom-left).
*/
class PadGrid final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit PadGrid (PadPorterProcessor& processor);
    ~PadGrid() override;

    std::function<void (int padIndex)> onPadSelected;
    /** Called first on a click; returning true consumes it (e.g. SHIFT + pad functions). */
    std::function<bool (int slot)> interceptPad;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> slotBounds (int slot) const;
    int slotAt (juce::Point<float> position) const;
    bool isPadActive (int padIndex) const noexcept;

    PadPorterProcessor& processor;
    int pressedPad = -1;
    std::uint64_t lastActivity[2] {};
};
} // namespace padporter::plugin
