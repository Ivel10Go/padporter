#pragma once

#include "PluginProcessor.h"
#include "ui/PadPanel.h"
#include "ui/PatternView.h"
#include "ui/ProjectsOverlay.h"
#include "ui/SequenceBrowser.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace padporter::plugin
{
class PadPorterEditor final : public juce::AudioProcessorEditor,
                              public juce::FileDragAndDropTarget,
                              private juce::ChangeListener
{
public:
    explicit PadPorterEditor (PadPorterProcessor& processor);
    ~PadPorterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshAll();
    void showProjects (bool show);
    void chooseExportFolder();
    void showDiagnostics();
    juce::Rectangle<int> screenBounds() const;

    PadPorterProcessor& processor;
    PadPorterLookAndFeel lookAndFeel;     // declared first: destroyed after all children

    juce::TextButton deviceChip, projectsButton { "PROJEKTE" }, exportButton { "EXPORT" };
    juce::TextButton compButton { "MASTER-KOMP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> compAttachment;
    juce::Slider mainVolume { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mainVolumeAttachment;

    SequenceBrowser sequences;
    PatternView pattern;
    PadPanel padPanel;
    ProjectsOverlay projects;

    juce::Label statusLabel;
    juce::TextButton detailsButton { "DETAILS" };
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    int seenDeviceGeneration = 0;
    std::uint64_t seenProjectGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadPorterEditor)
};
} // namespace padporter::plugin
