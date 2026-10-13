#include "ProjectsOverlay.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <padporter/PathUtil.h>

namespace padporter::plugin
{
namespace
{
constexpr int kTitleHeight = 44;
constexpr int kColumnHeader = 48;

juce::String describe (const ProjectEntry& e)
{
    juce::String s;
    s << e.sampleFiles << " Samples" << juce::String::fromUTF8 ("  ·  ")
      << juce::File::descriptionOfSizeInBytes (static_cast<juce::int64> (e.sizeBytes));
    if (e.modifiedUnix > 0)
        s << juce::String::fromUTF8 ("  ·  ") << juce::Time (e.modifiedUnix * 1000).formatted ("%d.%m.%Y %H:%M");
    return s;
}

void styleButton (juce::TextButton& b, const juce::String& text)
{
    b.setButtonText (text);
    b.setColour (juce::TextButton::buttonColourId, theme::lcdRaised.brighter (0.1f));
    b.setColour (juce::TextButton::textColourOffId, theme::screenText);
}
} // namespace

// ---------------------------------------------------------------------------

void ProjectList::setRows (std::vector<Row> newRows, const juce::String& emptyMessage)
{
    rows = std::move (newRows);
    emptyText = emptyMessage;
    repaint();
}

int ProjectList::idealHeight() const noexcept
{
    return juce::jmax (3, static_cast<int> (rows.size())) * kRowHeight;
}

int ProjectList::rowAt (int y) const noexcept
{
    const int row = y / kRowHeight;
    return row >= 0 && row < static_cast<int> (rows.size()) ? row : -1;
}

juce::Rectangle<int> ProjectList::actionBounds (int row) const
{
    return { getWidth() - 118, row * kRowHeight + 14, 106, kRowHeight - 28 };
}

void ProjectList::paint (juce::Graphics& g)
{
    if (rows.empty())
    {
        g.setColour (theme::screenDim);
        g.setFont (theme::font (13.0f));
        g.drawFittedText (emptyText, getLocalBounds().reduced (12, 8).withHeight (kRowHeight * 3), juce::Justification::centredTop, 6);
        return;
    }

    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
    {
        const auto& row = rows[static_cast<std::size_t> (i)];
        auto r = juce::Rectangle<int> (0, i * kRowHeight, getWidth(), kRowHeight).reduced (0, 3);

        g.setColour (row.active ? theme::lcdRaised.brighter (0.15f) : (i == hoverRow ? theme::lcdRaised.brighter (0.07f) : theme::lcdRaised));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        if (row.active)
        {
            g.setColour (theme::waveform);
            g.fillRoundedRectangle (r.toFloat().withWidth (3.0f), 1.5f);
        }

        auto text = r.reduced (14, 7).withTrimmedRight (116);
        g.setColour (theme::screenText);
        g.setFont (theme::font (14.0f, true));
        g.drawText (row.title, text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour (theme::screenDim);
        g.setFont (theme::font (11.0f));
        g.drawText (row.subtitle, text, juce::Justification::centredLeft, true);

        const auto button = actionBounds (i).toFloat();
        g.setColour (! row.enabled ? theme::lcdLine : (row.active ? theme::lcdLine.brighter (0.3f) : row.actionColour));
        g.fillRoundedRectangle (button, 5.0f);
        g.setColour (row.enabled && ! row.active ? theme::lcd : theme::screenText);
        g.setFont (theme::font (12.0f, true));
        g.drawText (row.action, button, juce::Justification::centred);
    }
}

void ProjectList::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row != hoverRow)
    {
        hoverRow = row;
        repaint();
    }
    setMouseCursor (row >= 0 && actionBounds (row).contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                                              : juce::MouseCursor::NormalCursor);
}

void ProjectList::mouseExit (const juce::MouseEvent&)
{
    hoverRow = -1;
    repaint();
}

void ProjectList::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row >= 0 && rows[static_cast<std::size_t> (row)].enabled && actionBounds (row).contains (e.getPosition()) && onAction)
        onAction (row);
}

void ProjectList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row >= 0 && rows[static_cast<std::size_t> (row)].enabled && onAction)
        onAction (row);
}

// ---------------------------------------------------------------------------

ProjectsOverlay::ProjectsOverlay (PadPorterProcessor& p) : processor (p)
{
    styleButton (closeButton, juce::String::fromUTF8 ("SCHLIESSEN"));
    styleButton (rescanButton, "NEU SUCHEN");
    styleButton (folderButton, "ORDNER");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    rescanButton.onClick = [this] { processor.rescanDevices(); };
    folderButton.onClick = [this] {
        const auto dir = processor.getLibraryDir();
        dir.createDirectory();
        dir.startAsProcess();
    };
    addAndMakeVisible (closeButton);
    addAndMakeVisible (rescanButton);
    addAndMakeVisible (folderButton);

    deviceList.onAction = [this] (int row) {
        if (row >= 0 && row < static_cast<int> (deviceEntries.size()))
            processor.importFromDevice (deviceEntries[static_cast<std::size_t> (row)]);
    };
    libraryList.onAction = [this] (int row) {
        if (row >= 0 && row < static_cast<int> (libraryEntries.size()))
            processor.openProject (toJuceFile (libraryEntries[static_cast<std::size_t> (row)].xpjPath));
    };

    for (auto* v : { &deviceView, &libraryView })
    {
        v->setScrollBarsShown (true, false);
        v->setScrollBarThickness (6);
        addAndMakeVisible (*v);
    }
    deviceView.setViewedComponent (&deviceList, false);
    libraryView.setViewedComponent (&libraryList, false);
}

void ProjectsOverlay::refresh()
{
    const auto* loaded = processor.getLoadedProject();
    const auto loadedFile = loaded != nullptr ? toStdPath (loaded->file) : std::filesystem::path();
    const auto libraryDir = toStdPath (processor.getLibraryDir());
    const bool busy = processor.isImporting();

    libraryEntries = processor.getLibrary();
    std::vector<ProjectList::Row> libraryRows;
    for (const auto& e : libraryEntries)
    {
        const bool isOpen = ! loadedFile.empty() && isSamePath (loadedFile, e.xpjPath);
        libraryRows.push_back ({ fromUtf8 (e.name), describe (e), isOpen ? juce::String::fromUTF8 ("GEÖFFNET") : juce::String::fromUTF8 ("ÖFFNEN"),
                                 theme::drums, isOpen, true });
    }
    libraryList.setRows (std::move (libraryRows), juce::String::fromUTF8 ("Noch nichts importiert.\nImportierte Projekte bleiben hier, auch ohne Gerät."));

    deviceEntries.clear();
    std::vector<ProjectList::Row> deviceRows;
    for (const auto& volume : processor.getDevices())
        for (const auto& e : volume.projects)
        {
            const bool inLibrary = std::any_of (libraryEntries.begin(), libraryEntries.end(),
                                                [&e] (const ProjectEntry& l) { return asciiLower (l.name) == asciiLower (e.name); });
            const bool isOpen = ! loadedFile.empty() && isSameOrInside (loadedFile, libraryDir)
                                && asciiLower (pathToUtf8 (loadedFile.stem())) == asciiLower (e.name);
            auto subtitle = describe (e);
            if (inLibrary)
                subtitle << juce::String::fromUTF8 ("  ·  importiert");
            deviceRows.push_back ({ fromUtf8 (e.name), subtitle,
                                    busy ? juce::String ("...") : (inLibrary ? juce::String ("NEU LADEN") : juce::String ("IMPORTIEREN")),
                                    theme::orange, isOpen, ! busy });
            deviceEntries.push_back (e);
        }

    deviceList.setRows (std::move (deviceRows),
                        processor.isScanning() ? juce::String::fromUTF8 ("Suche nach Geräten ...")
                                               : juce::String::fromUTF8 ("Kein Gerät gefunden.\n\nGerät per USB anschließen und dort SD-Card-Access aktivieren.\nDie Projekte erscheinen hier automatisch."));

    if (processor.getDevices().empty())
        deviceStatus = processor.isScanning() ? juce::String ("Suche ...") : juce::String::fromUTF8 ("Nicht verbunden");
    else
    {
        deviceStatus.clear();
        for (const auto& v : processor.getDevices())
            deviceStatus << (v.recognisedLayout ? fromUtf8 (v.deviceName) : juce::String ("Laufwerk")) << " an "
                         << fromUtf8 (pathToUtf8 (v.root)) << juce::String::fromUTF8 ("  ·  ")
                         << static_cast<int> (v.projects.size()) << " Projekte   ";
    }

    resized();
    repaint();
}

void ProjectsOverlay::paint (juce::Graphics& g)
{
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    auto area = getLocalBounds().reduced (20, 12);
    auto title = area.removeFromTop (kTitleHeight);
    g.setColour (theme::waveform);
    g.setFont (theme::font (20.0f, true));
    g.drawText ("PROJEKTE", title, juce::Justification::centredLeft);

    const int half = (area.getWidth() - 24) / 2;
    auto left = area.removeFromLeft (half);
    area.removeFromLeft (24);
    auto right = area;

    auto drawHeader = [&g] (juce::Rectangle<int> column, const juce::String& heading, const juce::String& sub, juce::Colour accent) {
        auto header = column.removeFromTop (kColumnHeader);
        g.setColour (accent);
        g.setFont (theme::font (12.0f, true));
        g.drawText (heading, header.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (theme::screenDim);
        g.setFont (theme::font (11.0f));
        g.drawText (sub, header.withTrimmedRight (120), juce::Justification::topLeft, true);
    };

    drawHeader (left, juce::String::fromUTF8 ("AUF DEM GERÄT"), deviceStatus, theme::orange);
    drawHeader (right, "BIBLIOTHEK (lokale Kopien)", processor.getLibraryDir().getFullPathName(), theme::drums);
}

void ProjectsOverlay::resized()
{
    auto area = getLocalBounds().reduced (20, 12);
    auto title = area.removeFromTop (kTitleHeight);
    closeButton.setBounds (title.removeFromRight (120).withSizeKeepingCentre (120, 28));

    const int half = (area.getWidth() - 24) / 2;
    auto left = area.removeFromLeft (half);
    area.removeFromLeft (24);
    auto right = area;

    rescanButton.setBounds (left.getRight() - 110, left.getY(), 110, 24);
    folderButton.setBounds (right.getRight() - 110, right.getY(), 110, 24);

    deviceView.setBounds (left.withTrimmedTop (kColumnHeader + 4));
    libraryView.setBounds (right.withTrimmedTop (kColumnHeader + 4));
    deviceList.setSize (deviceView.getMaximumVisibleWidth(), deviceList.idealHeight());
    libraryList.setSize (libraryView.getMaximumVisibleWidth(), libraryList.idealHeight());
}
} // namespace padporter::plugin
