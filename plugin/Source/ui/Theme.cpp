#include "Theme.h"

namespace padporter::plugin
{
PadPorterLookAndFeel::PadPorterLookAndFeel()
{
    setColourScheme ({ theme::background, theme::panel, theme::panelRaised, theme::outline, theme::text,
                       theme::accent, juce::Colours::black, theme::panelRaised, theme::text });

    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::TextButton::buttonColourId, theme::greyButton);
    setColour (juce::TextButton::buttonOnColourId, theme::orange);
    setColour (juce::TextButton::textColourOffId, theme::caption);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::PopupMenu::backgroundColourId, theme::panelRaised);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.4f));
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::ScrollBar::thumbColourId, theme::outline);
    setColour (juce::AlertWindow::backgroundColourId, theme::panel);
    setColour (juce::AlertWindow::textColourId, theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void PadPorterLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                                 bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    auto colour = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.45f);
    else if (isMouseOverButton && ! isButtonDown)
        colour = colour.brighter (0.06f);

    const float radius = 4.0f;
    const float travel = isButtonDown ? 1.0f : 0.0f;

    // Shadow below the rubber key; pressed keys sink into it.
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), radius);

    auto face = bounds.translated (0.0f, travel).withTrimmedBottom (1.0f);
    g.setGradientFill (juce::ColourGradient (colour.brighter (0.12f), face.getX(), face.getY(),
                                             colour.darker (0.08f), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, radius);

    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (face.reduced (0.5f), radius, 1.0f);
}

juce::Font PadPorterLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::font (juce::jmin (11.5f, static_cast<float> (buttonHeight) * 0.45f), true);
}

void PadPorterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + pos * (endAngle - startAngle);

    // Value ring around the knob.
    const float ringRadius = radius - 1.5f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto origin = bipolar ? startAngle + static_cast<float> (slider.valueToProportionOfLength (0.0)) * (endAngle - startAngle)
                                : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, juce::jmin (origin, angle), juce::jmax (origin, angle), true);
    g.setColour (slider.isEnabled() ? theme::padGlow : theme::caption.withAlpha (0.4f));
    g.strokePath (value, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob cap.
    const float capRadius = radius - 6.0f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (cap.translated (0.0f, 2.5f));
    g.setGradientFill (juce::ColourGradient (theme::knob, cap.getX(), cap.getY(),
                                             theme::knob.darker (0.25f), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::black.withAlpha (0.15f));
    g.drawEllipse (cap, 1.0f);

    // Indicator line.
    const auto inner = juce::Point<float> (centre.x + capRadius * 0.25f * std::sin (angle), centre.y - capRadius * 0.25f * std::cos (angle));
    const auto outer = juce::Point<float> (centre.x + capRadius * 0.85f * std::sin (angle), centre.y - capRadius * 0.85f * std::cos (angle));
    g.setColour (theme::caption);
    g.drawLine ({ inner, outer }, 2.2f);
}

void PadPorterLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto slot = juce::Rectangle<float> (6.0f, bounds.getHeight() - 8.0f).withCentre (bounds.getCentre());
    g.setColour (theme::blackPanel);
    g.fillRoundedRectangle (slot, 3.0f);

    // Fill below the cap shows the value.
    g.setColour (theme::padGlow.withAlpha (0.8f));
    g.fillRoundedRectangle (slot.withTop (sliderPos), 3.0f);

    const auto cap = juce::Rectangle<float> (juce::jmin (40.0f, bounds.getWidth() - 4.0f), 22.0f).withCentre ({ bounds.getCentreX(), sliderPos });
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 3.0f);
    g.setGradientFill (juce::ColourGradient (theme::knob, cap.getX(), cap.getY(), theme::knob.darker (0.2f), cap.getX(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (theme::caption.withAlpha (0.5f));
    for (int i = -1; i <= 1; ++i)
        g.drawHorizontalLine (juce::roundToInt (cap.getCentreY()) + i * 4, cap.getX() + 6.0f, cap.getRight() - 6.0f);
}
} // namespace padporter::plugin
