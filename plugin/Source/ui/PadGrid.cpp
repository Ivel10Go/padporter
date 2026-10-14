#include "PadGrid.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <padporter/PathUtil.h>

namespace padporter::plugin
{
PadGrid::PadGrid (PadPorterProcessor& p) : processor (p)
{
    startTimerHz (30);
}

PadGrid::~PadGrid()
{
    stopTimer();
}

namespace
{
// Shift functions printed below the pads, pad 1 first.
const char* const kPadCaptions[] { "FULL LEVEL", "HALF SEQ", "DOUBLE SEQ", "COUNT-IN", "COMPRESSOR", "HALF SPEED", "DOUBLE SPEED", "MIDI CONFIG",
                                    "FADER", "REC QUANTIZE", "RESAMPLE", "SONG", "TRIM SAMPLE", "TIME CORRECT", "WARP", "PROJECT" };

// Pad positions inside the bed, in photo pixels.
constexpr float kColumnX[] { 11.0f, 130.0f, 249.0f, 368.0f };
constexpr float kRowY[] { 9.0f, 128.0f, 247.0f, 366.0f };
constexpr float kPadWidth = 103.0f;
constexpr float kPadHeight = 101.0f;
} // namespace

juce::Rectangle<float> PadGrid::slotBounds (int slot) const
{
    const int column = slot % 4;
    const int row = 3 - slot / 4; // pad 1 sits bottom-left, like on the hardware
    return { kColumnX[column], kRowY[row], kPadWidth, kPadHeight };
}

int PadGrid::slotAt (juce::Point<float> position) const
{
    for (int slot = 0; slot < kPadsPerBank; ++slot)
        if (slotBounds (slot).contains (position))
            return slot;
    return -1;
}

bool PadGrid::isPadActive (int padIndex) const noexcept
{
    return (lastActivity[padIndex / 64] >> (padIndex % 64)) & 1u;
}

void PadGrid::paint (juce::Graphics& g)
{
    const auto* loaded = processor.getLoadedProject();
    const int bank = processor.getBank();
    const int selected = processor.getSelectedPad();

    g.setColour (theme::padBed);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    for (int slot = 0; slot < kPadsPerBank; ++slot)
    {
        const int padIndex = bank * kPadsPerBank + slot;
        const auto bounds = slotBounds (slot);
        const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
        const bool hasSample = pad != nullptr && pad->hasSample();
        const bool active = isPadActive (padIndex);
        const auto glow = hasSample && processor.getPadRole (padIndex) == PartRole::Melody ? theme::melody : theme::padGlow.interpolatedWith (juce::Colour (0xff8a8cff), 0.6f);

        // Backlight around the pad: blue = drums, orange = melody; brighter with a sample, full while playing.
        const float strength = active ? 1.0f : (hasSample ? 0.75f : 0.4f);
        for (int i = 5; i >= 1; --i)
        {
            g.setColour (glow.withAlpha (strength * 0.09f * static_cast<float> (6 - i)));
            g.drawRoundedRectangle (bounds.expanded (static_cast<float> (i) * 0.9f), 4.0f + static_cast<float> (i), 1.8f);
        }

        auto face = theme::padFaceHw;
        if (active)
            face = face.interpolatedWith (glow, 0.35f).brighter (0.1f);
        g.setGradientFill (juce::ColourGradient (face.brighter (0.05f), bounds.getX(), bounds.getY(),
                                                 face.darker (0.05f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 4.0f);

        // Lit rim of the silicone pad.
        g.setColour (padIndex == selected ? theme::padSelected : glow.withAlpha (0.25f + 0.65f * strength));
        g.drawRoundedRectangle (bounds.reduced (0.8f), 4.0f, padIndex == selected ? 2.4f : 1.6f);

        // Printed number and shift function below the pad.
        const auto caption = juce::Rectangle<float> (bounds.getX(), bounds.getBottom() + 3.0f, bounds.getWidth(), 13.0f);
        g.setColour (theme::hwLabel);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (juce::String (slot + 1), caption, juce::Justification::centredLeft);
        g.drawText (kPadCaptions[slot], caption.withTrimmedLeft (18.0f), juce::Justification::centredRight);
    }
}

void PadGrid::mouseMove (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.position);
    juce::String tip;
    if (slot >= 0)
    {
        const int padIndex = processor.getBank() * kPadsPerBank + slot;
        const auto* loaded = processor.getLoadedProject();
        const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
        tip << juce::String::charToString (static_cast<juce::juce_wchar> ('A' + padIndex / kPadsPerBank))
            << juce::String (slot + 1).paddedLeft ('0', 2) << juce::String::fromUTF8 ("  ·  ")
            << (pad != nullptr && pad->hasSample() ? fromUtf8 (pad->layers.front().sampleName) : juce::String ("leer"));
    }
    setTooltip (tip);
    setMouseCursor (slot >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.position);
    if (slot < 0 || (interceptPad && interceptPad (slot)))
        return;

    const int padIndex = processor.getBank() * kPadsPerBank + slot;
    processor.setSelectedPad (padIndex);
    if (onPadSelected)
        onPadSelected (padIndex);

    // Velocity from the vertical click position: top edge = full velocity.
    const auto bounds = slotBounds (slot);
    const auto velocity = juce::jlimit (0.2f, 1.0f, 1.0f - 0.8f * (e.position.y - bounds.getY()) / bounds.getHeight());
    processor.triggerPad (padIndex, velocity, true);
    pressedPad = padIndex;
    repaint();
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (pressedPad >= 0)
        processor.triggerPad (pressedPad, 0.0f, false);
    pressedPad = -1;
}

void PadGrid::timerCallback()
{
    const std::uint64_t a0 = processor.getPadActivity (0);
    const std::uint64_t a1 = processor.getPadActivity (1);
    if (a0 != lastActivity[0] || a1 != lastActivity[1])
    {
        lastActivity[0] = a0;
        lastActivity[1] = a1;
        repaint();
    }
}
} // namespace padporter::plugin
