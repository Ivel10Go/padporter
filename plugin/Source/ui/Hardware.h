#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace padporter::plugin
{
/** Rubber key of the front panel, with its printed label and an optional coloured stripe. */
class HardwareButton final : public juce::Button
{
public:
    enum class Style
    {
        Dark,     // grey keys (SAMPLE, SEQ, ERASE ...)
        Black,    // TAP TEMPO, the soft keys above the display
        Orange,   // PAD FX, KNOB FX
        Blue      // PAD PLAY keys
    };

    HardwareButton (const juce::String& label, Style style, juce::Colour stripe = juce::Colours::transparentBlack);

    /** Backlit label (mode keys, active functions). */
    void setLit (bool shouldBeLit);
    bool isLit() const noexcept { return lit; }

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    Style style;
    juce::Colour stripe;
    bool lit = false;
};

/** The large endless data wheel. Turning it by drag or mouse wheel reports steps. */
class JogWheel final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int steps)> onStep;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void turn (float radians);

    float rotation = 0.0f;
    float pending = 0.0f;
    float lastAngle = 0.0f;
};

/** Two columns of five LEDs (green, green, green, orange, red) showing the output level. */
class LevelMeter final : public juce::Component
{
public:
    void setLevel (float left, float right);
    void paint (juce::Graphics&) override;

private:
    float levels[2] {};
};
} // namespace padporter::plugin
