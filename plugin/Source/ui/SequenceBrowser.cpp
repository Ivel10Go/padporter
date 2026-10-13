#include "SequenceBrowser.h"
#include "Theme.h"

#include "../PluginProcessor.h"

namespace padporter::plugin
{
namespace
{
constexpr int kRowHeight = 30;
constexpr int kTrackRowHeight = 18;

void drawCount (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, int count)
{
    g.setColour (count > 0 ? colour : theme::screenDim.withAlpha (0.35f));
    g.fillEllipse (area.removeFromLeft (5.0f).withSizeKeepingCentre (4.5f, 4.5f));
    area.removeFromLeft (2.0f);
    g.setFont (theme::lcdFont (8.5f, true));
    g.drawText (juce::String (count), area, juce::Justification::centredLeft);
}
} // namespace

class SequenceBrowser::Rows final : public juce::Component
{
public:
    Rows (PadPorterProcessor& p, SequenceBrowser& b) : processor (p), browser (b)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int numRows() const
    {
        const auto* loaded = processor.getLoadedProject();
        return loaded != nullptr ? static_cast<int> (loaded->project->sequences.size()) : 0;
    }

    void paint (juce::Graphics& g) override
    {
        const auto* loaded = processor.getLoadedProject();
        if (loaded == nullptr)
        {
            g.setColour (theme::screenDim);
            g.setFont (theme::lcdFont (10.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Noch kein Projekt geöffnet.\n\nSoftkey PROJEKTE drücken."),
                              getLocalBounds().reduced (6).withHeight (70), juce::Justification::centred, 4);
            return;
        }

        const auto& project = *loaded->project;
        const int selected = processor.getSelectedSequence();
        const int playing = processor.getPreviewSequence();

        for (int row = 0; row < numRows(); ++row)
        {
            const auto& seq = project.sequences[static_cast<std::size_t> (row)];
            const auto counts = processor.getPartCounts (row);
            const bool isSelected = row == selected;
            auto r = juce::Rectangle<float> (0.0f, static_cast<float> (row * kRowHeight), static_cast<float> (getWidth()), static_cast<float> (kRowHeight)).reduced (0.0f, 1.0f);

            g.setColour (isSelected ? theme::lcdRaised.brighter (0.15f) : (row == hoverRow ? theme::lcdRaised.brighter (0.06f) : theme::lcdRaised));
            g.fillRect (r);
            if (isSelected)
            {
                g.setColour (theme::waveform);
                g.fillRect (r.withWidth (2.0f));
            }

            auto content = r.reduced (6.0f, 2.0f);
            auto number = content.removeFromLeft (22.0f);
            g.setColour (isSelected ? theme::waveform : theme::waveform.withAlpha (0.7f));
            g.setFont (theme::lcdFont (13.0f, true));
            g.drawText (juce::String (seq.number()).paddedLeft ('0', 2), number, juce::Justification::centredLeft);

            auto counters = content.removeFromRight (26.0f);
            drawCount (g, counters.removeFromTop (counters.getHeight() / 2), theme::drums, counts.drums);
            drawCount (g, counters, theme::melody, counts.melody);

            auto top = content.removeFromTop (content.getHeight() / 2);
            g.setColour (theme::screenText);
            g.setFont (theme::lcdFont (10.5f, true));
            g.drawText (fromUtf8 (seq.name), top.withTrimmedRight (row == playing ? 10.0f : 0.0f), juce::Justification::centredLeft, true);

            if (row == playing)
            {
                g.setColour (theme::waveform);
                juce::Path triangle;
                const auto x = top.getRight() - 7.0f;
                const auto y = top.getCentreY() - 3.5f;
                triangle.addTriangle (x, y, x, y + 7.0f, x + 6.0f, y + 3.5f);
                g.fillPath (triangle);
            }

            juce::String meta;
            meta << seq.lengthBars << (seq.lengthBars == 1 ? " Takt" : " Takte") << juce::String::fromUTF8 (" · ")
                 << juce::String (project.tempoFor (seq), 1) << " BPM";
            g.setColour (theme::screenDim);
            g.setFont (theme::lcdFont (8.5f));
            g.drawText (meta, content, juce::Justification::centredLeft, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int row = rowAt (e.y);
        if (row != hoverRow)
        {
            hoverRow = row;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverRow = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressedRow = rowAt (e.y);
        dragStarted = false;
        if (pressedRow >= 0)
            processor.setSelectedSequence (pressedRow);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (rowAt (e.y) >= 0 && browser.onSequenceOpened)
            browser.onSequenceOpened();
    }

    void scrollToRow (int row, juce::Viewport& viewport) const
    {
        const int top = row * kRowHeight;
        const int y = viewport.getViewPositionY();
        if (top < y)
            viewport.setViewPosition (0, top);
        else if (top + kRowHeight > y + viewport.getViewHeight())
            viewport.setViewPosition (0, top + kRowHeight - viewport.getViewHeight());
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || pressedRow < 0 || e.getDistanceFromDragStart() < 6)
            return;

        dragStarted = true;
        juce::String error;
        juce::MouseCursor::showWaitCursor();
        const auto file = processor.createDragFile (pressedRow, Part::All, -1, error);
        juce::MouseCursor::hideWaitCursor();
        if (file == juce::File())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "PadPorter", error);
            return;
        }
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

private:
    int rowAt (int y) const
    {
        const int row = y / kRowHeight;
        return row >= 0 && row < numRows() ? row : -1;
    }

    PadPorterProcessor& processor;
    SequenceBrowser& browser;
    int hoverRow = -1;
    int pressedRow = -1;
    bool dragStarted = false;
};

// ---------------------------------------------------------------------------

SequenceBrowser::SequenceBrowser (PadPorterProcessor& p) : processor (p), rows (std::make_unique<Rows> (p, *this))
{
    trackBox.setTextWhenNothingSelected ("Track");
    trackBox.setColour (juce::ComboBox::backgroundColourId, theme::lcdRaised);
    trackBox.setColour (juce::ComboBox::outlineColourId, theme::lcdLine);
    trackBox.setColour (juce::ComboBox::textColourId, theme::screenText);
    trackBox.onChange = [this] {
        if (const int id = trackBox.getSelectedId(); id > 0)
            processor.selectTrack (id - 1);
    };
    addChildComponent (trackBox);

    viewport.setViewedComponent (rows.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (4);
    addAndMakeVisible (viewport);
}

SequenceBrowser::~SequenceBrowser() = default;

void SequenceBrowser::refresh()
{
    const auto* loaded = processor.getLoadedProject();

    trackBox.clear (juce::dontSendNotification);
    if (loaded != nullptr)
    {
        const auto& project = *loaded->project;
        for (std::size_t i = 0; i < project.tracks.size(); ++i)
            if (project.tracks[i].kind == TrackKind::Drum)
                trackBox.addItem (fromUtf8 (project.tracks[i].name), static_cast<int> (i) + 1);
        trackBox.setSelectedId (loaded->trackIndex + 1, juce::dontSendNotification);
    }
    trackBox.setVisible (trackBox.getNumItems() > 1);

    resized();
    rows->scrollToRow (processor.getSelectedSequence(), viewport);
    rows->repaint();
}

void SequenceBrowser::paint (juce::Graphics& g)
{
    if (trackBox.isVisible())
    {
        g.setColour (theme::screenDim);
        g.setFont (theme::lcdFont (9.0f, true));
        g.drawText ("Track", getLocalBounds().removeFromTop (kTrackRowHeight), juce::Justification::centredLeft);
    }
}

void SequenceBrowser::resized()
{
    auto area = getLocalBounds();
    if (trackBox.isVisible())
    {
        auto header = area.removeFromTop (kTrackRowHeight);
        trackBox.setBounds (header.withTrimmedLeft (34).reduced (0, 2));
        area.removeFromTop (2);
    }

    viewport.setBounds (area);
    rows->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (area.getHeight(), rows->numRows() * kRowHeight));
}
} // namespace padporter::plugin
