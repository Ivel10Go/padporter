#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace padporter::plugin
{
class PadPorterProcessor;

/** Sequence list on the display. Click selects, dragging a row exports the whole sequence. */
class SequenceBrowser final : public juce::Component
{
public:
    explicit SequenceBrowser (PadPorterProcessor& processor);
    ~SequenceBrowser() override;

    void refresh();

    /** Double-click on a sequence. */
    std::function<void()> onSequenceOpened;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Rows;

    PadPorterProcessor& processor;
    juce::ComboBox trackBox;
    juce::Viewport viewport;
    std::unique_ptr<Rows> rows;
};
} // namespace padporter::plugin
