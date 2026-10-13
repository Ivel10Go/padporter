#include "SequenceBrowser.h"
#include "Theme.h"

#include "../PluginProcessor.h"

namespace padporter::plugin
{
namespace
{
constexpr int kRowHeight = 62;
constexpr int kHeaderHeight = 30;

void drawCount (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, int count)
{
    g.setColour (count > 0 ? colour : theme::screenDim.withAlpha (0.35f));
    g.fillEllipse (area.removeFromLeft (8.0f).withSizeKeepingCentre (7.0f, 7.0f));
    area.removeFromLeft (4.0f);
    g.setFont (theme::font (11.0f, true));
    g.drawText (juce::String (count), area, juce::Justification::centredLeft);
}
} // namespace

class SequenceBrowser::Rows final : public juce::Component
{
public:
    explicit Rows (PadPorterProcessor& p) : processor (p)
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
            g.setFont (theme::font (13.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Noch kein Projekt geöffnet.\n\nOben auf PROJEKTE klicken."),
                              getLocalBounds().reduced (10).withHeight (90), juce::Justification::centred, 4);
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
            auto r = juce::Rectangle<float> (0.0f, static_cast<float> (row * kRowHeight), static_cast<float> (getWidth()), static_cast<float> (kRowHeight)).reduced (0.0f, 3.0f);

            g.setColour (isSelected ? theme::lcdRaised.brighter (0.15f) : (row == hoverRow ? theme::lcdRaised.brighter (0.06f) : theme::lcdRaised));
            g.fillRoundedRectangle (r, 6.0f);
            if (isSelected)
            {
                g.setColour (theme::waveform);
                g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f);
            }

            auto content = r.reduced (12.0f, 8.0f);
            auto number = content.removeFromLeft (40.0f);
            g.setColour (isSelected ? theme::waveform : theme::waveform.withAlpha (0.7f));
            g.setFont (theme::font (21.0f, true));
            g.drawText (juce::String (seq.number()).paddedLeft ('0', 2), number, juce::Justification::centredLeft);

            auto counters = content.removeFromRight (46.0f);
            drawCount (g, counters.removeFromTop (counters.getHeight() / 2), theme::drums, counts.drums);
            drawCount (g, counters, theme::melody, counts.melody);

            auto top = content.removeFromTop (content.getHeight() / 2);
            g.setColour (theme::screenText);
            g.setFont (theme::font (13.5f, true));
            g.drawText (fromUtf8 (seq.name), top, juce::Justification::bottomLeft, true);

            if (row == playing)
            {
                g.setColour (theme::waveform);
                juce::Path triangle;
                const auto x = top.getRight() - 10.0f;
                const auto y = top.getBottom() - 11.0f;
                triangle.addTriangle (x, y, x, y + 9.0f, x + 8.0f, y + 4.5f);
                g.fillPath (triangle);
            }

            juce::String meta;
            meta << seq.lengthBars << (seq.lengthBars == 1 ? " Takt" : " Takte") << "  " << juce::String::fromUTF8 ("·") << "  "
                 << juce::String (project.tempoFor (seq), 1) << " BPM";
            g.setColour (theme::screenDim);
            g.setFont (theme::font (11.0f));
            g.drawText (meta, content, juce::Justification::topLeft, true);
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
    int hoverRow = -1;
    int pressedRow = -1;
    bool dragStarted = false;
};

// ---------------------------------------------------------------------------

SequenceBrowser::SequenceBrowser (PadPorterProcessor& p) : processor (p), rows (std::make_unique<Rows> (p))
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
    viewport.setScrollBarThickness (6);
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
    rows->repaint();
}

void SequenceBrowser::paint (juce::Graphics& g)
{
    g.setColour (theme::screenDim);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("SEQUENCES", getLocalBounds().removeFromTop (kHeaderHeight).withTrimmedLeft (2), juce::Justification::centredLeft);
}

void SequenceBrowser::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight);
    trackBox.setBounds (header.removeFromRight (150).reduced (0, 3));

    viewport.setBounds (area);
    rows->setSize (viewport.getMaximumVisibleWidth(), juce::jmax (area.getHeight(), rows->numRows() * kRowHeight));
}
} // namespace padporter::plugin
