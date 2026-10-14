#pragma once

#include "PatternView.h"
#include "ProjectsOverlay.h"
#include "SequenceBrowser.h"
#include "WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace padporter::plugin
{
class PadPorterProcessor;

/** SAMPLE mode page: the selected pad with its waveform and the values of K1-K3. */
class PadPage final : public juce::Component
{
public:
    explicit PadPage (PadPorterProcessor& processor);

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> waveArea() const;

    PadPorterProcessor& processor;
    WaveformView waveform;
};

/**
    The small colour display. Like on the device it shows three tabs along the top that
    belong to the three soft keys above it; their meaning depends on the mode:
      SEQ    - Sequenzen | Pattern | Projekte
      SAMPLE - the selected pad's part: Auto | Drums | Melodie
*/
class DisplayScreen final : public juce::Component, private juce::Timer
{
public:
    enum class Mode
    {
        Seq,
        Sample
    };

    enum Page
    {
        sequencesPage = 0,
        patternPage,
        projectsPage
    };

    explicit DisplayScreen (PadPorterProcessor& processor);
    ~DisplayScreen() override;

    void refresh();

    Mode getMode() const noexcept { return mode; }
    void setMode (Mode newMode);
    int getPage() const noexcept { return page; }
    void showPage (int newPage);
    void pressSoftKey (int index);

    /** Notified when mode or page change, so the panel can light its keys. */
    std::function<void()> onModeChanged;
    std::function<void()> onExport;
    std::function<void()> onDetails;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void updateVisibility();
    juce::StringArray tabLabels() const;
    int activeTab() const;

    PadPorterProcessor& processor;
    SequenceBrowser sequences;
    PatternView pattern;
    ProjectsOverlay projects;
    PadPage padPage;

    Mode mode = Mode::Seq;
    int page = sequencesPage;

    juce::String toast, lastStatus;
    bool toastError = false;
    int toastTicks = 0;
};
} // namespace padporter::plugin
