#include "PadPanel.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <padporter/PathUtil.h>

namespace padporter::plugin
{
namespace
{
constexpr int kPadArea = 252;
constexpr int kPadX = 128;
} // namespace

PadPanel::PadPanel (PadPorterProcessor& p) : processor (p), pads (p)
{
    for (int i = 0; i < 8; ++i)
    {
        auto& b = bankButtons[static_cast<std::size_t> (i)];
        b.setButtonText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)));
        b.setColour (juce::TextButton::buttonColourId, theme::greyButton);
        b.setColour (juce::TextButton::buttonOnColourId, theme::orange);
        b.onClick = [this, i] {
            processor.setBank (i);
            processor.setSelectedPad (i * kPadsPerBank + processor.getSelectedPad() % kPadsPerBank);
            refresh();
        };
        addAndMakeVisible (b);
    }

    pads.onPadSelected = [this] (int) { refresh(); };
    addAndMakeVisible (pads);

    autoButton.setTooltip (juce::String::fromUTF8 ("Automatisch erkennen (Name, Länge, Tempo des Samples)"));
    drumsButton.setTooltip (juce::String::fromUTF8 ("Pad gehört zu DRUMS"));
    melodyButton.setTooltip (juce::String::fromUTF8 ("Pad gehört zu MELODIE"));
    const std::array<std::pair<juce::TextButton*, juce::Colour>, 3> roleButtons { std::pair { &autoButton, theme::caption },
                                                                                   std::pair { &drumsButton, theme::drums },
                                                                                   std::pair { &melodyButton, theme::melody } };
    for (auto [button, colour] : roleButtons)
    {
        button->setColour (juce::TextButton::buttonColourId, theme::lcdRaised);
        button->setColour (juce::TextButton::buttonOnColourId, colour);
        button->setColour (juce::TextButton::textColourOffId, theme::screenDim);
        button->setColour (juce::TextButton::textColourOnId, theme::lcd);
        addAndMakeVisible (*button);
    }
    autoButton.onClick = [this] { processor.setPadRole (processor.getSelectedPad(), std::nullopt); };
    drumsButton.onClick = [this] { processor.setPadRole (processor.getSelectedPad(), PartRole::Drums); };
    melodyButton.onClick = [this] { processor.setPadRole (processor.getSelectedPad(), PartRole::Melody); };

    waveform.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (waveform);
}

juce::Rectangle<int> PadPanel::cardBounds() const
{
    return getLocalBounds().withTrimmedLeft (kPadX + kPadArea + 28).reduced (0, 10).withTrimmedRight (6);
}

void PadPanel::refresh()
{
    const auto* loaded = processor.getLoadedProject();
    const int bank = processor.getBank();

    for (int i = 0; i < 8; ++i)
    {
        auto& b = bankButtons[static_cast<std::size_t> (i)];
        b.setToggleState (i == bank, juce::dontSendNotification);
        bool used = false;
        for (int s = 0; s < kPadsPerBank && ! used && loaded != nullptr; ++s)
            if (const auto* pad = loaded->pad (i * kPadsPerBank + s))
                used = pad->hasSample();
        b.setColour (juce::TextButton::textColourOffId, used ? theme::caption : theme::caption.withAlpha (0.4f));
    }

    const int padIndex = processor.getSelectedPad();
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
    const bool hasSample = pad != nullptr && pad->hasSample();

    for (auto* b : { &autoButton, &drumsButton, &melodyButton })
        b->setEnabled (hasSample);

    const bool automatic = processor.isPadRoleAutomatic (padIndex);
    const auto role = processor.getPadRole (padIndex);
    autoButton.setToggleState (hasSample && automatic, juce::dontSendNotification);
    drumsButton.setToggleState (hasSample && ! automatic && role == PartRole::Drums, juce::dontSendNotification);
    melodyButton.setToggleState (hasSample && ! automatic && role == PartRole::Melody, juce::dontSendNotification);

    if (hasSample && loaded->pads[static_cast<std::size_t> (padIndex)].sample != nullptr)
    {
        const auto& sample = loaded->pads[static_cast<std::size_t> (padIndex)].sample;
        waveform.setSample (sample, 0, sample->audio.getNumSamples());
    }
    else
    {
        waveform.setMessage (pad == nullptr ? juce::String() : (hasSample ? juce::String ("Sample fehlt") : juce::String ("leeres Pad")));
    }

    pads.repaint();
    repaint();
}

void PadPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::caption);
    g.setFont (theme::font (9.5f, true));
    g.drawText ("PAD BANK", juce::Rectangle<int> (16, 4, 96, 16), juce::Justification::centredLeft);

    // Info card for the selected pad (small display).
    const auto card = cardBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (card, 8.0f);
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (card.reduced (3.0f), 6.0f);

    const auto* loaded = processor.getLoadedProject();
    const int padIndex = processor.getSelectedPad();
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;

    auto inner = card.reduced (18.0f, 14.0f);
    auto title = inner.removeFromTop (24.0f);
    const auto label = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + padIndex / kPadsPerBank))
                     + juce::String (padIndex % kPadsPerBank + 1).paddedLeft ('0', 2);

    g.setColour (theme::waveform);
    g.setFont (theme::font (17.0f, true));
    g.drawText (label, title.removeFromLeft (44.0f), juce::Justification::centredLeft);
    g.setColour (theme::screenText);
    g.drawText (pad != nullptr && pad->hasSample() ? fromUtf8 (pad->layers.front().sampleName) : juce::String ("(leeres Pad)"),
                title, juce::Justification::centredLeft, true);

    if (pad != nullptr)
    {
        juce::String meta = "Note " + fromUtf8 (midiNoteName (pad->midiNote)) + " (" + juce::String (pad->midiNote) + ")";
        if (const auto& s = loaded->pads[static_cast<std::size_t> (padIndex)].sample)
            meta << juce::String::fromUTF8 ("  ·  ") << juce::String (s->audio.getNumSamples() / s->sampleRate, 2) << " s";
        if (pad->hasSample())
        {
            const bool automatic = processor.isPadRoleAutomatic (padIndex);
            meta << juce::String::fromUTF8 ("  ·  ") << (processor.getPadRole (padIndex) == PartRole::Melody ? "Melodie" : "Drums")
                 << (automatic ? " (automatisch erkannt)" : " (manuell)");
        }
        g.setColour (theme::screenDim);
        g.setFont (theme::font (11.5f));
        g.drawText (meta, inner.removeFromTop (18.0f), juce::Justification::centredLeft, true);
    }

    g.setColour (theme::screenDim);
    g.setFont (theme::font (10.0f, true));
    g.drawText ("TEIL", juce::Rectangle<float> (inner.getX(), card.getBottom() - 48.0f, 40.0f, 30.0f), juce::Justification::centredLeft);

    g.setFont (theme::font (10.5f));
    g.drawText (juce::String::fromUTF8 ("Pad antippen = anhören"), juce::Rectangle<float> (inner.getX(), card.getBottom() - 48.0f, inner.getWidth(), 30.0f),
                juce::Justification::centredRight);

    // Engraved name, like on the hardware's palm rest.
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.setColour (theme::bodyEdge.darker (0.15f));
    g.drawText ("PADPORTER", juce::Rectangle<int> (16, getHeight() - 22, 96, 16), juce::Justification::centredLeft);
}

void PadPanel::resized()
{
    for (int i = 0; i < 8; ++i)
        bankButtons[static_cast<std::size_t> (i)].setBounds (16 + (i % 2) * 50, 24 + (i / 2) * 40, 44, 32);

    pads.setBounds (kPadX, (getHeight() - kPadArea) / 2, kPadArea, kPadArea);

    const auto card = cardBounds().reduced (18, 14);
    waveform.setBounds (card.getX(), card.getY() + 48, card.getWidth(), card.getHeight() - 48 - 44);

    auto roleRow = juce::Rectangle<int> (card.getX() + 44, card.getBottom() - 30, 270, 26);
    for (auto* b : { &autoButton, &drumsButton, &melodyButton })
    {
        b->setBounds (roleRow.removeFromLeft (86));
        roleRow.removeFromLeft (6);
    }
}
} // namespace padporter::plugin
