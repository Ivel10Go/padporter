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

juce::Rectangle<float> PadGrid::slotBounds (int slot) const
{
    const auto area = getLocalBounds().toFloat().reduced (4.0f);
    const float gap = 8.0f;
    const float w = (area.getWidth() - 3.0f * gap) / 4.0f;
    const float h = (area.getHeight() - 3.0f * gap) / 4.0f;
    const int column = slot % 4;
    const int row = 3 - slot / 4; // pad 1 sits bottom-left, like on the hardware
    return { area.getX() + column * (w + gap), area.getY() + row * (h + gap), w, h };
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

    for (int slot = 0; slot < kPadsPerBank; ++slot)
    {
        const int padIndex = bank * kPadsPerBank + slot;
        const auto bounds = slotBounds (slot);
        const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
        const bool hasSample = pad != nullptr && pad->hasSample();
        const bool missing = hasSample && loaded->pads[static_cast<std::size_t> (padIndex)].sample == nullptr;
        const bool active = isPadActive (padIndex);
        const auto glow = hasSample && processor.getPadRole (padIndex) == PartRole::Melody ? theme::melody : theme::drums;

        // Light around the pad in its part colour (blue = drums, orange = melody); bright while playing.
        if (hasSample || active)
        {
            const float strength = active ? 1.0f : 0.45f;
            for (int i = 4; i >= 1; --i)
            {
                g.setColour (glow.withAlpha (strength * 0.13f * static_cast<float> (5 - i)));
                g.drawRoundedRectangle (bounds.expanded (static_cast<float> (i)), 7.0f + static_cast<float> (i), 2.0f);
            }
        }

        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (bounds.translated (0.0f, 2.0f), 6.0f);

        auto face = hasSample ? theme::padFaceLoaded : theme::padFace;
        if (active)
            face = face.interpolatedWith (glow, 0.45f);
        g.setGradientFill (juce::ColourGradient (face.brighter (0.06f), bounds.getX(), bounds.getY(),
                                                 face.darker (0.06f), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 6.0f);

        if (padIndex == selected)
        {
            g.setColour (theme::caption);
            g.drawRoundedRectangle (bounds.reduced (1.5f), 6.0f, 2.5f);
        }

        if (hasSample)
        {
            // Part stripe at the bottom edge of the pad.
            g.setColour (glow);
            g.fillRoundedRectangle (bounds.getX() + 8.0f, bounds.getBottom() - 5.0f, bounds.getWidth() - 16.0f, 2.5f, 1.25f);
        }

        auto text = bounds.reduced (6.0f, 4.0f).withTrimmedBottom (4.0f);
        const auto label = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + bank))
                         + juce::String (slot + 1).paddedLeft ('0', 2);

        g.setColour (theme::caption.withAlpha (hasSample ? 1.0f : 0.45f));
        g.setFont (theme::font (10.0f, true));
        g.drawText (label, text.removeFromTop (12.0f), juce::Justification::topLeft);

        if (hasSample)
        {
            g.setFont (theme::font (9.5f));
            g.setColour (missing ? theme::brandRed : theme::caption.withAlpha (0.85f));
            const auto name = fromUtf8 (pad->layers.front().sampleName);
            g.drawFittedText (missing ? "fehlt: " + name : name, text.toNearestInt(), juce::Justification::bottomLeft, 2, 0.8f);
        }
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.position);
    if (slot < 0)
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
