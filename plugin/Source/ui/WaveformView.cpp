#include "WaveformView.h"
#include "Theme.h"

namespace padporter::plugin
{
void WaveformView::setSample (SamplePtr newSample, int start, int end)
{
    sample = std::move (newSample);
    rangeStart = start;
    rangeEnd = end;
    message.clear();
    rebuildPeaks();
    repaint();
}

void WaveformView::setMessage (const juce::String& text)
{
    sample.reset();
    message = text;
    minima.clear();
    maxima.clear();
    repaint();
}

void WaveformView::resized()
{
    rebuildPeaks();
}

void WaveformView::rebuildPeaks()
{
    minima.clear();
    maxima.clear();

    const int width = getWidth();
    if (sample == nullptr || width <= 0)
        return;

    const auto& audio = sample->audio;
    const int total = audio.getNumSamples();
    if (total <= 0)
        return;

    minima.resize (static_cast<std::size_t> (width), 0.0f);
    maxima.resize (static_cast<std::size_t> (width), 0.0f);

    for (int x = 0; x < width; ++x)
    {
        const int from = static_cast<int> (static_cast<juce::int64> (x) * total / width);
        const int to = juce::jmax (from + 1, static_cast<int> (static_cast<juce::int64> (x + 1) * total / width));
        float lo = 0.0f, hi = 0.0f;

        // Large ranges are subsampled: drawing precision does not need every frame.
        const int step = juce::jmax (1, (to - from) / 256);
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            const float* data = audio.getReadPointer (ch);
            for (int i = from; i < juce::jmin (to, total); i += step)
            {
                lo = juce::jmin (lo, data[i]);
                hi = juce::jmax (hi, data[i]);
            }
        }
        minima[static_cast<std::size_t> (x)] = lo;
        maxima[static_cast<std::size_t> (x)] = hi;
    }
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (sample == nullptr || minima.empty())
    {
        g.setColour (theme::screenDim);
        g.setFont (theme::font (11.0f));
        g.drawText (message.isNotEmpty() ? message : juce::String ("kein Sample"), getLocalBounds(), juce::Justification::centred);
        return;
    }

    const int total = sample->audio.getNumSamples();
    const float mid = bounds.getCentreY();
    const float halfHeight = bounds.getHeight() * 0.48f;
    const auto toX = [&] (std::int64_t frame) { return static_cast<float> (frame) * bounds.getWidth() / static_cast<float> (total); };

    const float startX = toX (rangeStart);
    const float endX = toX (rangeEnd);

    for (int x = 0; x < static_cast<int> (minima.size()); ++x)
    {
        const bool inRange = static_cast<float> (x) >= startX && static_cast<float> (x) <= endX;
        g.setColour (inRange ? theme::waveform : theme::screenDim.withAlpha (0.5f));
        const float top = mid - maxima[static_cast<std::size_t> (x)] * halfHeight;
        const float bottom = mid - minima[static_cast<std::size_t> (x)] * halfHeight;
        g.drawVerticalLine (x, top, juce::jmax (top + 1.0f, bottom));
    }

    if (! sample->reversed)
    {
        g.setColour (theme::orange.withAlpha (0.9f));
        for (auto point : sample->slicePoints)
            g.drawVerticalLine (static_cast<int> (toX (point)), bounds.getY(), bounds.getBottom());
    }
}
} // namespace padporter::plugin
