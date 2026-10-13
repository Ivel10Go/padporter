#include "PluginEditor.h"

#include <padporter/PathUtil.h>

namespace padporter::plugin
{
namespace
{
constexpr int kWidth = 1180;
constexpr int kHeight = 790;
const juce::Rectangle<int> kPanel { 16, 16, kWidth - 32, 474 };
constexpr int kPadPanelTop = 498;

void styleDarkButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, theme::darkButton);
    b.setColour (juce::TextButton::textColourOffId, theme::captionOnDark);
}
} // namespace

PadPorterEditor::PadPorterEditor (PadPorterProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), sequences (p), pattern (p), padPanel (p), projects (p)
{
    setLookAndFeel (&lookAndFeel);

    styleDarkButton (deviceChip);
    styleDarkButton (projectsButton);
    styleDarkButton (exportButton);
    deviceChip.onClick = [this] { showProjects (true); };
    projectsButton.onClick = [this] { showProjects (! projects.isVisible()); };
    exportButton.onClick = [this] { chooseExportFolder(); };
    deviceChip.setTooltip (juce::String::fromUTF8 ("Projekte auf dem Gerät anzeigen"));
    exportButton.setTooltip ("Samples + MIDI (Alles / Drums / Melodie) + Mapping in einen Ordner exportieren");
    addAndMakeVisible (deviceChip);
    addAndMakeVisible (projectsButton);
    addAndMakeVisible (exportButton);

    styleDarkButton (compButton);
    compButton.setColour (juce::TextButton::buttonOnColourId, theme::orange);
    compButton.setClickingTogglesState (true);
    compButton.setTooltip (juce::String::fromUTF8 ("Master-Kompressor des Geräts nachbilden (Annäherung an den Color Compressor, gilt für Wiedergabe und Audio-Drag)"));
    compAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.parameters, "devicecomp", compButton);
    addAndMakeVisible (compButton);

    mainVolume.setPopupDisplayEnabled (true, true, this);
    mainVolume.setTextValueSuffix (" dB");
    mainVolume.setTooltip (juce::String::fromUTF8 ("Lautstärke"));
    mainVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, "master", mainVolume);
    addAndMakeVisible (mainVolume);

    addAndMakeVisible (sequences);
    addAndMakeVisible (pattern);
    addAndMakeVisible (padPanel);
    addChildComponent (projects);
    projects.onClose = [this] { showProjects (false); };

    statusLabel.setFont (theme::font (11.5f));
    statusLabel.setColour (juce::Label::textColourId, theme::screenDim);
    addAndMakeVisible (statusLabel);
    detailsButton.setColour (juce::TextButton::buttonColourId, theme::lcdRaised);
    detailsButton.setColour (juce::TextButton::textColourOffId, theme::screenDim);
    detailsButton.onClick = [this] { showDiagnostics(); };
    addAndMakeVisible (detailsButton);

    seenDeviceGeneration = processor.getDeviceGeneration();
    if (const auto* loaded = processor.getLoadedProject())
        seenProjectGeneration = loaded->generation;
    else
        projects.setVisible (true);

    processor.addChangeListener (this);
    setSize (kWidth, kHeight);
    refreshAll();
}

PadPorterEditor::~PadPorterEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void PadPorterEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshAll();
}

void PadPorterEditor::showProjects (bool show)
{
    if (show)
    {
        processor.rescanDevices();
        projects.refresh();
    }
    projects.setVisible (show);
    projectsButton.setToggleState (show, juce::dontSendNotification);
}

void PadPorterEditor::refreshAll()
{
    const auto* loaded = processor.getLoadedProject();

    // A freshly loaded project closes the project overlay; a newly connected device opens it.
    if (loaded != nullptr && loaded->generation != seenProjectGeneration)
    {
        seenProjectGeneration = loaded->generation;
        projects.setVisible (false);
    }
    if (processor.getDeviceGeneration() != seenDeviceGeneration)
    {
        seenDeviceGeneration = processor.getDeviceGeneration();
        projects.setVisible (true);
    }
    projectsButton.setToggleState (projects.isVisible(), juce::dontSendNotification);
    projectsButton.setColour (juce::TextButton::buttonOnColourId, theme::orange);

    // Device status chip.
    juce::String chip;
    if (! processor.getDevices().empty())
    {
        const auto& v = processor.getDevices().front();
        chip << juce::String::fromUTF8 ("●  ") << (v.recognisedLayout ? fromUtf8 (v.deviceName) : juce::String ("Laufwerk")) << "  "
             << fromUtf8 (pathToUtf8 (v.root)) << juce::String::fromUTF8 ("  ·  ") << static_cast<int> (v.projects.size()) << " Projekte";
        deviceChip.setColour (juce::TextButton::textColourOffId, theme::green.brighter (0.3f));
    }
    else
    {
        chip << juce::String::fromUTF8 ("○  Kein Gerät verbunden");
        deviceChip.setColour (juce::TextButton::textColourOffId, theme::captionOnDark.withAlpha (0.6f));
    }
    deviceChip.setButtonText (chip);

    exportButton.setEnabled (loaded != nullptr && ! processor.isLoading());
    compButton.setEnabled (processor.projectHasMasterCompressor());

    sequences.refresh();
    pattern.refresh();
    padPanel.refresh();
    if (projects.isVisible())
        projects.refresh();

    statusLabel.setText (processor.getStatusText(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, processor.statusIsError() ? theme::error : theme::screenDim);
    detailsButton.setVisible (loaded != nullptr && ! loaded->diagnostics.empty());
    repaint();
}

void PadPorterEditor::chooseExportFolder()
{
    if (processor.getLoadedProject() == nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Zielordner für den Export wählen (es wird ein Unterordner mit dem Projektnamen angelegt)"),
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<PadPorterEditor> (this)] (const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (safe == nullptr || dir == juce::File())
                                  return;

                              safe->statusLabel.setText ("Exportiere ...", juce::dontSendNotification);
                              safe->processor.exportProject (dir, [safe] (bool ok, juce::String message, juce::File folder) {
                                  if (safe == nullptr)
                                      return;
                                  safe->refreshAll();
                                  if (! ok)
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export fehlgeschlagen", message);
                                      return;
                                  }
                                  auto options = juce::MessageBoxOptions()
                                                     .withIconType (juce::MessageBoxIconType::InfoIcon)
                                                     .withTitle ("Export fertig")
                                                     .withMessage (message)
                                                     .withButton ("Ordner zeigen")
                                                     .withButton ("OK");
                                  juce::AlertWindow::showAsync (options, [folder] (int result) {
                                      if (result == 1)
                                          folder.revealToUser();
                                  });
                              });
                          });
}

void PadPorterEditor::showDiagnostics()
{
    const auto* loaded = processor.getLoadedProject();
    if (loaded == nullptr)
        return;

    juce::String text;
    int shown = 0;
    for (const auto& d : loaded->diagnostics.items())
    {
        if (++shown > 30)
        {
            text << "... (" << static_cast<int> (loaded->diagnostics.items().size()) - 30 << " weitere - siehe export-log.txt nach einem Export)";
            break;
        }
        text << "[" << toString (d.severity) << "] " << fromUtf8 (d.where) << ": " << fromUtf8 (d.message) << "\n";
    }
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Projekt-Details", text);
}

bool PadPorterEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".xpj"))
            return true;
    return false;
}

void PadPorterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".xpj"))
        {
            processor.openProject (juce::File (f));
            return;
        }
}

juce::Rectangle<int> PadPorterEditor::screenBounds() const
{
    return { kPanel.getX() + 16, kPanel.getY() + 60, kPanel.getWidth() - 32, kPanel.getHeight() - 76 };
}

void PadPorterEditor::paint (juce::Graphics& g)
{
    // Hardware body.
    g.setGradientFill (juce::ColourGradient (theme::body, 0.0f, 0.0f, theme::bodyShade, 0.0f, static_cast<float> (getHeight()), false));
    g.fillAll();

    // Black top panel with logo.
    const auto panel = kPanel.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (panel.translated (0.0f, 3.0f), 18.0f);
    g.setColour (theme::blackPanel);
    g.fillRoundedRectangle (panel, 18.0f);

    g.setColour (theme::brandRed);
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold | juce::Font::italic)));
    g.drawText ("PADPORTER", juce::Rectangle<float> (panel.getX() + 22.0f, panel.getY() + 12.0f, 200.0f, 36.0f), juce::Justification::centredLeft);

    g.setColour (theme::captionOnDark.withAlpha (0.7f));
    g.setFont (theme::font (9.0f, true));
    g.drawText ("VOL", juce::Rectangle<float> (panel.getRight() - 64.0f, panel.getY() + 40.0f, 48.0f, 12.0f), juce::Justification::centred);

    // Display bezel and LCD.
    const auto screen = screenBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (screen.expanded (3.0f), 9.0f);
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (screen, 7.0f);

    // Divider between sequence list and pattern view.
    g.setColour (theme::lcdLine);
    g.drawVerticalLine (static_cast<int> (screen.getX()) + 322, screen.getY() + 14.0f, screen.getBottom() - 36.0f);
    g.drawHorizontalLine (static_cast<int> (screen.getBottom()) - 30, screen.getX() + 12.0f, screen.getRight() - 12.0f);
}

void PadPorterEditor::resized()
{
    const auto panel = kPanel;
    mainVolume.setBounds (panel.getRight() - 62, panel.getY() + 2, 44, 40);
    exportButton.setBounds (panel.getRight() - 62 - 14 - 104, panel.getY() + 16, 104, 28);
    projectsButton.setBounds (exportButton.getX() - 8 - 116, panel.getY() + 16, 116, 28);
    compButton.setBounds (projectsButton.getX() - 8 - 124, panel.getY() + 16, 124, 28);
    deviceChip.setBounds (panel.getX() + 230, panel.getY() + 16, 330, 28);

    const auto screen = screenBounds();
    auto inner = screen.reduced (12, 10);
    auto status = inner.removeFromBottom (22);
    detailsButton.setBounds (status.removeFromRight (80).reduced (0, 1));
    statusLabel.setBounds (status);
    inner.removeFromBottom (8);

    sequences.setBounds (inner.removeFromLeft (298));
    inner.removeFromLeft (24);
    pattern.setBounds (inner);

    projects.setBounds (screen.reduced (2));

    padPanel.setBounds (16, kPadPanelTop, kWidth - 32, kHeight - kPadPanelTop - 12);
}
} // namespace padporter::plugin
