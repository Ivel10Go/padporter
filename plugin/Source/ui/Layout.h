#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace padporter::plugin::layout
{
/**
    The editor reproduces the hardware's front panel. All positions are measured on a
    1280 x 1280 px product photo and written here in photo pixels; the unit's top-left
    corner sits at (kOriginX, kOriginY) of that photo. The whole canvas is then scaled
    to the window size, so the proportions always match the device.
*/
constexpr int kOriginX = 150;
constexpr int kOriginY = 58;
constexpr int kWidth = 975;
constexpr int kHeight = 1162;

/** Rectangle from photo coordinates (x1, y1) - (x2, y2). */
inline juce::Rectangle<int> photo (int x1, int y1, int x2, int y2) noexcept
{
    return { x1 - kOriginX, y1 - kOriginY, x2 - x1, y2 - y1 };
}

/** Square of the given size centred on a photo coordinate. */
inline juce::Rectangle<int> centred (int cx, int cy, int size) noexcept
{
    return { cx - kOriginX - size / 2, cy - kOriginY - size / 2, size, size };
}

inline juce::Point<float> point (float x, float y) noexcept
{
    return { x - static_cast<float> (kOriginX), y - static_cast<float> (kOriginY) };
}
} // namespace padporter::plugin::layout
