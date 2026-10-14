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

// Front panel as on the product photo
inline const juce::Colour hwBody        { 0xffb9b8b4 };
inline const juce::Colour hwBodyShade   { 0xffa9a8a4 };
inline const juce::Colour hwLip         { 0xffcac9c5 };
inline const juce::Colour hwPanel       { 0xff070707 };
inline const juce::Colour hwCaption     { 0xff262625 };
inline const juce::Colour hwLabel       { 0xffe6e6e6 };
inline const juce::Colour keyDark       { 0xff3e3e40 };
inline const juce::Colour keyBlack      { 0xff2b2b2d };
inline const juce::Colour keyOrange     { 0xffb06f44 };
inline const juce::Colour keyBlue       { 0xff5672ab };
inline const juce::Colour keyText       { 0xfff2f2f2 };
inline const juce::Colour padBed        { 0xff121117 };
inline const juce::Colour padFaceHw     { 0xff8b8b8e };
inline const juce::Colour padSelected   { 0xfff1e4a4 };
inline const juce::Colour palmRestHw    { 0xff5a5a5c };
inline const juce::Colour logoRed       { 0xffa5432a };
inline const juce::Colour meterRed      { 0xffe3121f };
inline const juce::Colour meterOrange   { 0xfff28c12 };
inline const juce::Colour meterGreen    { 0xff42d22a };
inline const juce::Colour stripeRed     { 0xffd72939 };
inline const juce::Colour stripeWhite   { 0xfffbfbfd };
inline const juce::Colour stripeGreen   { 0xff56e528 };
inline const juce::Colour ledGreen      { 0xffa2f0c6 };
inline const juce::Colour lcdTabText    { 0xffd8d8d8 };
inline const juce::Colour lcdFrame      { 0xff5e5a4a };
inline const juce::Colour lcdBlue       { 0xff4fa8ee };

// Sequence parts
inline const juce::Colour drums         { 0xff45bfff };
inline const juce::Colour melody        { 0xffffa53d };
inline const juce::Colour unassigned    { 0xff6f7b88 };

// Large display (LCD)
inline const juce::Colour lcd           { 0xff050505 };
inline const juce::Colour lcdRaised     { 0xff1a1a1c };
inline const juce::Colour lcdLine       { 0xff2e2e32 };

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
/** Narrow fixed-pitch face of the device display. */
inline juce::Font lcdFont (float size, bool bold = false)
{
    auto options = juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain);
   #if JUCE_WINDOWS
    options = options.withName ("Consolas");
   #else
    options = options.withName (juce::Font::getDefaultMonospacedFontName());
   #endif
    return juce::Font (options);
}
} // namespace theme

/**
    Look of the controls: knobs with a printed arc and a ridged cap, the long fader cap,
    flat buttons on the display and dark list/scroll elements.
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
    int getSliderThumbRadius (juce::Slider&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    /** Slider property: knob printed on the black top panel (light arc) instead of the grey body. */
    static constexpr const char* onPanelProperty = "onPanel";
};
} // namespace padporter::plugin
