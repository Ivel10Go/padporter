#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace padporter::plugin
{
class PadPorterProcessor;

/** 4x4 pad view of the current bank, laid out like the hardware (pad 1 bottom-left). */
class PadGrid final : public juce::Component, private juce::Timer
{
public:
    explicit PadGrid (PadPorterProcessor& processor);
    ~PadGrid() override;

    std::function<void (int padIndex)> onPadSelected;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

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
