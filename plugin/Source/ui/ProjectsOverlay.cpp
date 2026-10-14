#include "ProjectsOverlay.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <padporter/PathUtil.h>

namespace padporter::plugin
{
namespace
{
constexpr int kBarHeight = 16;
constexpr int kHeadingHeight = 14;
constexpr int kActionWidth = 54;

juce::String describe (const ProjectEntry& e)
{
    juce::String s;
    s << e.sampleFiles << " Samples" << juce::String::fromUTF8 (" · ")
      << juce::File::descriptionOfSizeInBytes (static_cast<juce::int64> (e.sizeBytes));
    if (e.modifiedUnix > 0)
        s << juce::String::fromUTF8 (" · ") << juce::Time (e.modifiedUnix * 1000).formatted ("%d.%m.%y");
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
    return rows.empty() ? kRowHeight + 4 : static_cast<int> (rows.size()) * kRowHeight;
}

int ProjectList::rowAt (int y) const noexcept
{
    const int row = y / kRowHeight;
    return row >= 0 && row < static_cast<int> (rows.size()) ? row : -1;
}

juce::Rectangle<int> ProjectList::actionBounds (int row) const
{
    return { getWidth() - kActionWidth - 2, row * kRowHeight + 7, kActionWidth, kRowHeight - 14 };
}

void ProjectList::paint (juce::Graphics& g)
{
    if (rows.empty())
    {
        g.setColour (theme::screenDim);
        g.setFont (theme::lcdFont (9.0f));
        g.drawFittedText (emptyText, getLocalBounds().reduced (4, 2), juce::Justification::centredLeft, 3);
        return;
    }

    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
    {
        const auto& row = rows[static_cast<std::size_t> (i)];
        auto r = juce::Rectangle<int> (0, i * kRowHeight, getWidth(), kRowHeight).reduced (0, 1);

        g.setColour (row.active ? theme::lcdRaised.brighter (0.15f) : (i == hoverRow ? theme::lcdRaised.brighter (0.07f) : theme::lcdRaised));
        g.fillRect (r);
        if (row.active)
        {
            g.setColour (theme::waveform);
            g.fillRect (r.withWidth (2));
        }

        auto text = r.reduced (6, 2).withTrimmedRight (kActionWidth + 2);
        g.setColour (theme::screenText);
        g.setFont (theme::lcdFont (10.0f, true));
        g.drawText (row.title, text.removeFromTop (text.getHeight() / 2), juce::Justification::centredLeft, true);
        g.setColour (theme::screenDim);
        g.setFont (theme::lcdFont (8.0f));
        g.drawText (row.subtitle, text, juce::Justification::centredLeft, true);

        const auto button = actionBounds (i).toFloat();
        g.setColour (! row.enabled ? theme::lcdLine : (row.active ? theme::lcdLine.brighter (0.3f) : row.actionColour));
        g.fillRect (button);
        g.setColour (row.enabled && ! row.active ? theme::lcd : theme::screenText);
        g.setFont (theme::lcdFont (8.5f, true));
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
    styleButton (rescanButton, "SUCHEN");
    styleButton (folderButton, "ORDNER");
    styleButton (exportButton, "EXPORT");
    styleButton (detailsButton, "INFO");
    rescanButton.setTooltip (juce::String::fromUTF8 ("Erneut nach angeschlossenen Geräten suchen"));
    folderButton.setTooltip (juce::String::fromUTF8 ("Bibliotheksordner im Explorer öffnen"));
    exportButton.setTooltip ("Samples + MIDI (Alles / Drums / Melodie) + Mapping in einen Ordner exportieren");
    detailsButton.setTooltip ("Hinweise zum geladenen Projekt");
    rescanButton.onClick = [this] { processor.rescanDevices(); };
    folderButton.onClick = [this] {
        const auto dir = processor.getLibraryDir();
        dir.createDirectory();
        dir.startAsProcess();
    };
    exportButton.onClick = [this] { if (onExport) onExport(); };
    detailsButton.onClick = [this] { if (onDetails) onDetails(); };
    for (auto* b : { &rescanButton, &folderButton, &exportButton, &detailsButton })
        addAndMakeVisible (*b);

    deviceList.onAction = [this] (int row) {
        if (row >= 0 && row < static_cast<int> (deviceEntries.size()))
            processor.importFromDevice (deviceEntries[static_cast<std::size_t> (row)]);
    };
    libraryList.onAction = [this] (int row) {
        if (row >= 0 && row < static_cast<int> (libraryEntries.size()))
            processor.openProject (toJuceFile (libraryEntries[static_cast<std::size_t> (row)].xpjPath));
    };

    for (auto [label, text, colour] : { std::tuple { &deviceHeading, juce::String::fromUTF8 ("AUF DEM GERÄT"), theme::orange },
                                        std::tuple { &libraryHeading, juce::String ("BIBLIOTHEK"), theme::drums } })
    {
        label->setText (text, juce::dontSendNotification);
        label->setFont (theme::lcdFont (8.5f, true));
        label->setColour (juce::Label::textColourId, colour);
        label->setBorderSize ({});
        content.addAndMakeVisible (*label);
    }
    content.addAndMakeVisible (deviceList);
    content.addAndMakeVisible (libraryList);

    view.setScrollBarsShown (true, false);
    view.setScrollBarThickness (4);
    view.setViewedComponent (&content, false);
    addAndMakeVisible (view);
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
    libraryList.setRows (std::move (libraryRows), juce::String::fromUTF8 ("Noch nichts importiert. Importierte\nProjekte bleiben hier, auch ohne Gerät."));

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
                                    busy ? juce::String ("...") : (inLibrary ? juce::String ("NEU LADEN") : juce::String ("IMPORT")),
                                    theme::orange, isOpen, ! busy });
            deviceEntries.push_back (e);
        }

    deviceList.setRows (std::move (deviceRows),
                        processor.isScanning() ? juce::String::fromUTF8 ("Suche nach Geräten ...")
                                               : juce::String::fromUTF8 ("Gerät per USB anschließen und dort\nSD-Card-Access aktivieren."));

    if (processor.getDevices().empty())
        deviceStatus = processor.isScanning() ? juce::String ("Suche ...") : juce::String::fromUTF8 ("Kein Gerät verbunden");
    else
    {
        deviceStatus.clear();
        for (const auto& v : processor.getDevices())
            deviceStatus << (v.recognisedLayout ? fromUtf8 (v.deviceName) : juce::String ("Laufwerk")) << " "
                         << fromUtf8 (pathToUtf8 (v.root)) << "  ";
    }

    exportButton.setEnabled (loaded != nullptr && ! processor.isLoading());
    detailsButton.setEnabled (loaded != nullptr && ! loaded->diagnostics.empty());

    resized();
    repaint();
}

void ProjectsOverlay::paint (juce::Graphics& g)
{
    auto bar = getLocalBounds().removeFromTop (kBarHeight).withTrimmedRight (rescanButton.getWidth() + 4);
    const bool connected = ! processor.getDevices().empty();
    g.setColour (connected ? theme::green.brighter (0.3f) : theme::screenDim);
    g.fillEllipse (bar.removeFromLeft (7).toFloat().withSizeKeepingCentre (5.0f, 5.0f));
    bar.removeFromLeft (3);
    g.setFont (theme::lcdFont (9.0f, true));
    g.drawText (deviceStatus, bar, juce::Justification::centredLeft, true);
}

void ProjectsOverlay::resized()
{
    auto area = getLocalBounds();
    auto top = area.removeFromTop (kBarHeight);
    rescanButton.setBounds (top.removeFromRight (48).reduced (0, 1));

    auto bottom = area.removeFromBottom (kBarHeight + 2).withTrimmedTop (2);
    const int w = (bottom.getWidth() - 6) / 3;
    folderButton.setBounds (bottom.removeFromLeft (w));
    bottom.removeFromLeft (3);
    exportButton.setBounds (bottom.removeFromLeft (w));
    bottom.removeFromLeft (3);
    detailsButton.setBounds (bottom);

    view.setBounds (area.withTrimmedTop (1));
    const int width = view.getMaximumVisibleWidth();
    int y = 0;
    deviceHeading.setBounds (0, y, width, kHeadingHeight);
    y += kHeadingHeight;
    deviceList.setBounds (0, y, width, deviceList.idealHeight());
    y += deviceList.getHeight() + 2;
    libraryHeading.setBounds (0, y, width, kHeadingHeight);
    y += kHeadingHeight;
    libraryList.setBounds (0, y, width, libraryList.idealHeight());
    y += libraryList.getHeight();
    content.setSize (width, y);
}
} // namespace padporter::plugin
