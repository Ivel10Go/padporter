#pragma once

#include "PluginProcessor.h"
#include "ui/DisplayScreen.h"
#include "ui/Hardware.h"
#include "ui/PadGrid.h"
#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace padporter::plugin
{
/**
    The editor is a replica of the device's front panel. Every control sits where it sits on
    the hardware; the panel is drawn in photo coordinates (see ui/Layout.h) and scaled as a
    whole to the window size.
*/
class PadPorterEditor final : public juce::AudioProcessorEditor,
                              public juce::FileDragAndDropTarget,
                              private juce::ChangeListener,
                              private juce::Timer
{
public:
    explicit PadPorterEditor (PadPorterProcessor& processor);
    ~PadPorterEditor() override;

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xfff2f2f0)); }
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    /** Unscaled front panel; paints the body and holds all controls. */
    class FrontPanel final : public juce::Component
    {
    public:
        explicit FrontPanel (PadPorterEditor& e) : editor (e) {}
        void paint (juce::Graphics& g) override { editor.paintPanel (g); }

    private:
        PadPorterEditor& editor;
    };

    void paintPanel (juce::Graphics&);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshAll();
    void updateKeys();
    void updateKnobs();
    void applyKnob (int knob);
    void step (int delta);
    void changeBank (int delta);
    bool handleShiftPad (int slot);
    void chooseExportFolder();
    void showDiagnostics();
    void addKey (HardwareButton& key, juce::Rectangle<int> bounds, const juce::String& tooltip);

    PadPorterProcessor& processor;
    PadPorterLookAndFeel lookAndFeel;     // declared first: destroyed after all children

    FrontPanel panel { *this };

    juce::Slider mainVolume { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mainVolumeAttachment;
    std::array<juce::Slider, 3> knobs;
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAttachment;

    std::array<HardwareButton, 3> softKeys { HardwareButton { {}, HardwareButton::Style::Black },
                                             HardwareButton { {}, HardwareButton::Style::Black },
                                             HardwareButton { {}, HardwareButton::Style::Black } };
    DisplayScreen screen;
    LevelMeter meter;
    PadGrid pads;
    JogWheel jog;

    HardwareButton sampleKey { "SAMPLE", HardwareButton::Style::Dark },
                   seqKey { "SEQ", HardwareButton::Style::Dark },
                   padFxKey { "PAD\nFX", HardwareButton::Style::Orange },
                   knobFxKey { "KNOB\nFX", HardwareButton::Style::Orange },
                   shiftKey { {}, HardwareButton::Style::Dark },
                   padBankKey { "PAD\nBANK", HardwareButton::Style::Dark },
                   eraseKey { "ERASE", HardwareButton::Style::Dark },
                   noteRepeatKey { "NOTE\nREPEAT", HardwareButton::Style::Dark },
                   chopKey { "CHOP", HardwareButton::Style::Blue },
                   muteKey { "MUTE", HardwareButton::Style::Blue },
                   loopKey { "LOOP", HardwareButton::Style::Blue },
                   levelsKey { "16\nLEVELS", HardwareButton::Style::Blue },
                   sampleSelectKey { "SAMPLE\nSELECT", HardwareButton::Style::Dark },
                   tapTempoKey { "TAP\nTEMPO", HardwareButton::Style::Black },
                   minusKey { juce::String::fromUTF8 ("\xe2\x80\x93"), HardwareButton::Style::Dark },
                   plusKey { "+", HardwareButton::Style::Dark },
                   sampleRecordKey { "SAMPLE\nRECORD", HardwareButton::Style::Dark, theme::stripeRed },
                   seqRecordKey { "SEQ\nRECORD", HardwareButton::Style::Dark, theme::stripeRed },
                   stopKey { juce::String::fromUTF8 ("\xe2\x96\xa0"), HardwareButton::Style::Dark, theme::stripeWhite },
                   playKey { juce::String::fromUTF8 ("\xe2\x96\xb6"), HardwareButton::Style::Dark, theme::stripeGreen };

    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;

    bool shiftLatched = false;
    bool updatingKnobs = false;
    bool constructed = false;
    int shownBank = -1;
    bool shownDevice = false;
    float meterLevels[2] {};
    int seenDeviceGeneration = 0;
    std::uint64_t seenProjectGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadPorterEditor)
};
} // namespace padporter::plugin
