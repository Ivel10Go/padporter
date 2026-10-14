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
    // Buttons drawn inside the display: flat boxes like the device's on-screen fields.
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;

    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.45f);
    else if (isButtonDown)
        colour = colour.brighter (0.2f);
    else if (isMouseOverButton)
        colour = colour.brighter (0.1f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, 2.0f);
    g.setColour (theme::screenDim.withAlpha (button.isEnabled() ? 0.6f : 0.25f));
    g.drawRoundedRectangle (bounds, 2.0f, 0.8f);
}

juce::Font PadPorterLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::lcdFont (juce::jmin (10.0f, static_cast<float> (buttonHeight) * 0.68f), true);
}

void PadPorterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                             float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + pos * (endAngle - startAngle);
    const bool onPanel = static_cast<bool> (slider.getProperties()[onPanelProperty]);

    // Arc printed around the knob, with a dot at each end.
    const float arcRadius = radius - 4.0f;
    const auto print = onPanel ? juce::Colour (0xff8e8e8e) : juce::Colour (0xff111111);
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (print);
    g.strokePath (arc, juce::PathStrokeType (2.4f));
    for (const auto a : { startAngle, endAngle })
    {
        const auto p = centre.getPointOnCircumference (arcRadius, a);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (p));
    }

    // Cap: ridged skirt and a smooth top with a raised pointer fin.
    const float capRadius = arcRadius * 0.74f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (cap.expanded (1.5f).translated (1.5f, 3.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e2e0), cap.getX(), cap.getY(),
                                             juce::Colour (0xff8f8f8c), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);

    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (int i = 0; i < 56; ++i)
    {
        const auto a = static_cast<float> (i) * juce::MathConstants<float>::twoPi / 56.0f;
        g.drawLine ({ centre.getPointOnCircumference (capRadius * 0.85f, a), centre.getPointOnCircumference (capRadius * 0.99f, a) }, 1.0f);
    }

    const float topRadius = capRadius * 0.82f;
    const auto top = juce::Rectangle<float> (topRadius * 2.0f, topRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f4f2), top.getX() + topRadius * 0.4f, top.getY() + topRadius * 0.3f,
                                             juce::Colour (0xffbdbdba), top.getRight(), top.getBottom(), true));
    g.fillEllipse (top);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.drawEllipse (top, 0.8f);

    const float finWidth = capRadius * 0.24f;
    juce::Path fin;
    fin.addRoundedRectangle (-finWidth / 2.0f, -topRadius * 0.97f, finWidth, topRadius * 1.07f, finWidth * 0.45f);
    const auto place = juce::AffineTransform::rotation (angle).translated (centre);
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillPath (fin, place.translated (1.2f, 1.8f));
    g.setColour (juce::Colour (0xfffbfbf9));
    g.fillPath (fin, place);
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.strokePath (fin, juce::PathStrokeType (0.7f), place);
    if (! slider.isEnabled())
    {
        g.setColour (theme::hwBody.withAlpha (0.35f));
        g.fillEllipse (cap);
    }
}

int PadPorterLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    // Half the fader cap: the cap's centre travels between the ends of the slot.
    return slider.getSliderStyle() == juce::Slider::LinearVertical ? 47 : juce::LookAndFeel_V4::getSliderThumbRadius (slider);
}

void PadPorterLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                             float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    // The area passed in is the cap centre's travel; the slot overhangs it a little.
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto slot = juce::Rectangle<float> (bounds.getCentreX() - 12.0f, bounds.getY() - 12.0f, 24.0f, bounds.getHeight() + 24.0f);
    g.setColour (juce::Colour (0xff0d0d0d));
    g.fillRoundedRectangle (slot, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (slot.expanded (1.0f), 2.5f, 1.0f);

    const auto cap = juce::Rectangle<float> (44.0f, 94.0f).withCentre ({ bounds.getCentreX(), sliderPos });
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (cap.translated (2.0f, 4.0f), 4.0f);

    // Upper and lower halves of the cap, divided by a dark groove.
    const auto upper = cap.withHeight (cap.getHeight() * 0.49f);
    const auto lower = cap.withTrimmedTop (cap.getHeight() * 0.54f);
    g.setColour (juce::Colour (0xff4a4a4a));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffededeb), upper.getX(), upper.getY(), juce::Colour (0xffcfcfcd), upper.getX(), upper.getBottom(), false));
    g.fillRoundedRectangle (upper, 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffdcdcda), lower.getX(), lower.getY(), juce::Colour (0xffbababa), lower.getX(), lower.getBottom(), false));
    g.fillRoundedRectangle (lower, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawHorizontalLine (juce::roundToInt (upper.getBottom() + 1.0f), cap.getX() + 2.0f, cap.getRight() - 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawRoundedRectangle (cap, 4.0f, 0.8f);
}

void PadPorterLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                                          int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    const auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize)
                                           : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height);
    g.setColour (theme::screenDim.withAlpha (isMouseDown ? 0.9f : (isMouseOver ? 0.7f : 0.45f)));
    g.fillRoundedRectangle (thumb.toFloat().reduced (1.0f), 1.5f);
}
} // namespace padporter::plugin
