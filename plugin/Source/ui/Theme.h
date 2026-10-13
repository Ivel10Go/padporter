#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace padporter::plugin
{
namespace theme
{
// Hardware body (light grey unit with a black display panel)
inline const juce::Colour body          { 0xffe7e7e3 };
inline const juce::Colour bodyShade     { 0xffd2d2ce };
inline const juce::Colour bodyEdge      { 0xffb4b4af };
inline const juce::Colour palmRest      { 0xffc6c6c2 };
inline const juce::Colour blackPanel    { 0xff161616 };
inline const juce::Colour caption       { 0xff4b4b49 };
inline const juce::Colour captionOnDark { 0xffd8d8d8 };

// Display
inline const juce::Colour screen        { 0xff0b1118 };
inline const juce::Colour screenBar     { 0xff16212c };
inline const juce::Colour screenText    { 0xffe9eef4 };
inline const juce::Colour screenDim     { 0xff7d8b9a };
inline const juce::Colour waveform      { 0xfff2c230 };

// Controls
inline const juce::Colour brandRed      { 0xffe2362a };
inline const juce::Colour orange        { 0xfff07a2b };
inline const juce::Colour blue          { 0xff35a2e6 };
inline const juce::Colour padGlow       { 0xff45bfff };
inline const juce::Colour green         { 0xff2fb955 };
inline const juce::Colour greyButton    { 0xffbcbcb8 };
inline const juce::Colour darkButton    { 0xff2a2a2a };
inline const juce::Colour padFace       { 0xffcacac7 };
inline const juce::Colour padFaceLoaded { 0xffdcdcd9 };
inline const juce::Colour knob          { 0xfff5f5f3 };

// Sequence parts
inline const juce::Colour drums         { 0xff45bfff };
inline const juce::Colour melody        { 0xffffa53d };
inline const juce::Colour unassigned    { 0xff6f7b88 };

// Large display (LCD)
inline const juce::Colour lcd           { 0xff0c131b };
inline const juce::Colour lcdRaised     { 0xff16202b };
inline const juce::Colour lcdLine       { 0xff233142 };

// Companion panel (browser, status bar)
inline const juce::Colour background    { 0xff121316 };
inline const juce::Colour panel         { 0xff1c1e22 };
inline const juce::Colour panelRaised   { 0xff272a30 };
inline const juce::Colour outline       { 0xff353940 };
inline const juce::Colour text          { 0xffe8eaee };
inline const juce::Colour textDim       { 0xff8d95a2 };
inline const juce::Colour accent        { 0xff45bfff };
inline const juce::Colour warning       { 0xffffb454 };
inline const juce::Colour error         { 0xffff5d6c };

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace theme

/**
    Look of hardware controls: rubber buttons (colour from TextButton::buttonColourId),
    white knobs, a fader cap and dark list/scroll elements for the companion panel.
*/
class PadPorterLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PadPorterLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
};
} // namespace padporter::plugin
