#include "Hardware.h"
#include "Theme.h"

namespace padporter::plugin
{
namespace
{
juce::Colour keyColour (HardwareButton::Style style)
{
    switch (style)
    {
        case HardwareButton::Style::Dark:   return theme::keyDark;
        case HardwareButton::Style::Black:  return theme::keyBlack;
        case HardwareButton::Style::Orange: return theme::keyOrange;
        case HardwareButton::Style::Blue:   return theme::keyBlue;
    }
    return theme::keyDark;
}
} // namespace

HardwareButton::HardwareButton (const juce::String& label, Style s, juce::Colour stripeColour)
    : juce::Button (label), style (s), stripe (stripeColour)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void HardwareButton::setLit (bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void HardwareButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f, 0.0f).withTrimmedBottom (3.0f);
    const float radius = 6.0f;
    const float travel = isButtonDown ? 1.5f : 0.0f;
    auto colour = keyColour (style);
    if (isMouseOver && ! isButtonDown)
        colour = colour.brighter (0.07f);

    // Shadow on the panel; a pressed key sinks into it.
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillRoundedRectangle (bounds.translated (0.8f, isButtonDown ? 2.0f : 3.0f), radius);

    auto key = bounds.translated (0.0f, travel);
    g.setColour (colour.darker (0.45f));
    g.fillRoundedRectangle (key, radius);
    const auto face = key.reduced (1.2f).withTrimmedBottom (1.0f);
    g.setGradientFill (juce::ColourGradient (colour.brighter (0.1f), face.getX(), face.getY(),
                                             colour.darker (0.06f), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, radius - 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (face.reduced (0.5f), radius - 1.0f, 0.8f);

    auto textArea = face.reduced (3.0f, 2.0f);
    if (! stripe.isTransparent())
    {
        const auto bar = juce::Rectangle<float> (face.getX() + 9.0f, face.getBottom() - 11.0f, face.getWidth() - 18.0f, 6.0f);
        g.setColour (stripe);
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (stripe.withAlpha (0.25f));
        g.drawRoundedRectangle (bar.expanded (1.0f), 2.0f, 1.0f);
        textArea = textArea.withTrimmedBottom (13.0f);
    }

    const auto label = getButtonText();
    if (label.isEmpty())
        return;

    const auto lines = juce::StringArray::fromLines (label);
    const bool symbol = label.length() == 1;
    const float size = symbol ? 20.0f : (lines.size() > 1 ? 11.5f : 13.0f);

    // Backlit labels: white when lit, grey otherwise. Coloured keys are always lit.
    auto textColour = lit || style == Style::Orange || style == Style::Blue ? theme::keyText : juce::Colour (0xffb4b4b6);
    if (! isEnabled())
        textColour = textColour.withMultipliedAlpha (0.5f);

    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (size, juce::Font::bold)).withHorizontalScale (symbol ? 1.0f : 0.92f));
    g.drawFittedText (label, textArea.toNearestInt(), juce::Justification::centred, lines.size(), 0.8f);
}

// ---------------------------------------------------------------------------

void JogWheel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (bounds.translated (2.0f, 5.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8d8d6), bounds.getX(), bounds.getY(),
                                             juce::Colour (0xff86868a), bounds.getRight(), bounds.getBottom(), false));
    g.fillEllipse (bounds);

    // Knurled rim that turns with the wheel.
    for (int i = 0; i < 72; ++i)
    {
        const auto a = rotation + static_cast<float> (i) * juce::MathConstants<float>::twoPi / 72.0f;
        g.setColour (juce::Colours::black.withAlpha (i % 2 == 0 ? 0.16f : 0.06f));
        g.drawLine ({ centre.getPointOnCircumference (radius * 0.86f, a), centre.getPointOnCircumference (radius * 0.99f, a) }, 1.4f);
    }

    const auto top = bounds.reduced (radius * 0.2f).translated (-radius * 0.04f, -radius * 0.05f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeeec), top.getX() + top.getWidth() * 0.3f, top.getY() + top.getHeight() * 0.25f,
                                             juce::Colour (0xffb2b2af), top.getRight(), top.getBottom(), true));
    g.fillEllipse (top);
    g.setColour (juce::Colours::black.withAlpha (0.1f));
    g.drawEllipse (top, 1.0f);
}

void JogWheel::mouseDown (const juce::MouseEvent& e)
{
    lastAngle = getLocalBounds().toFloat().getCentre().getAngleToPoint (e.position);
}

void JogWheel::mouseDrag (const juce::MouseEvent& e)
{
    const auto angle = getLocalBounds().toFloat().getCentre().getAngleToPoint (e.position);
    auto delta = angle - lastAngle;
    if (delta > juce::MathConstants<float>::pi)
        delta -= juce::MathConstants<float>::twoPi;
    else if (delta < -juce::MathConstants<float>::pi)
        delta += juce::MathConstants<float>::twoPi;
    lastAngle = angle;
    turn (delta);
}

void JogWheel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float amount = std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? -wheel.deltaY : wheel.deltaX;
    if (std::abs (amount) > 1.0e-4f)
        turn (amount > 0.0f ? 0.4f : -0.4f);
}

void JogWheel::turn (float radians)
{
    // One step per 0.4 rad (about 16 detents per turn).
    constexpr float stepAngle = 0.4f;
    rotation += radians;
    pending += radians;
    const int steps = static_cast<int> (pending / stepAngle);
    if (steps != 0)
    {
        pending -= static_cast<float> (steps) * stepAngle;
        if (onStep)
            onStep (steps);
    }
    repaint();
}

// ---------------------------------------------------------------------------

void LevelMeter::setLevel (float left, float right)
{
    if (std::abs (left - levels[0]) > 0.005f || std::abs (right - levels[1]) > 0.005f)
    {
        levels[0] = left;
        levels[1] = right;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    // Top to bottom: red (clip), orange, three green. Thresholds in dBFS.
    static constexpr float thresholds[] { -1.0f, -6.0f, -12.0f, -24.0f, -42.0f };
    const juce::Colour colours[] { theme::meterRed, theme::meterOrange, theme::meterGreen, theme::meterGreen, theme::meterGreen };

    const float columnWidth = 12.0f;
    const float ledHeight = 23.0f;
    const float pitch = (static_cast<float> (getHeight()) - ledHeight) / 4.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = levels[ch] > 1.0e-5f ? 20.0f * std::log10 (levels[ch]) : -100.0f;
        const float x = ch == 0 ? 2.0f : static_cast<float> (getWidth()) - columnWidth - 2.0f;
        for (int i = 0; i < 5; ++i)
        {
            const auto led = juce::Rectangle<float> (x, static_cast<float> (i) * pitch, columnWidth, ledHeight);
            const bool on = db >= thresholds[i];
            if (on)
            {
                g.setColour (colours[i].withAlpha (0.25f));
                g.fillRoundedRectangle (led.expanded (2.5f), 3.0f);
            }
            g.setColour (on ? colours[i] : colours[i].withMultipliedBrightness (0.22f).withAlpha (0.9f));
            g.fillRoundedRectangle (led, 1.5f);
        }
    }
}
} // namespace padporter::plugin
