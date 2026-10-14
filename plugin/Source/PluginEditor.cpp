#include "PluginEditor.h"
#include "ui/Layout.h"

#include <padporter/PathUtil.h>

namespace padporter::plugin
{
namespace
{
using layout::centred;
using layout::photo;

juce::Rectangle<float> area (int x1, int y1, int x2, int y2)
{
    return photo (x1, y1, x2, y2).toFloat();
}

/** Printed caption on the grey body, centred on a photo coordinate. */
void caption (juce::Graphics& g, const juce::String& text, int cx, int cy, float size = 10.5f)
{
    g.setFont (juce::Font (juce::FontOptions (size, juce::Font::bold)).withHorizontalScale (0.95f));
    g.drawText (text, photo (cx - 70, cy - 7, cx + 70, cy + 7), juce::Justification::centred);
}

/** Section heading with a rule on each side, like "—— MODE ——". */
void heading (juce::Graphics& g, const juce::String& text, int x1, int x2, int cy)
{
    const auto font = juce::Font (juce::FontOptions (10.5f, juce::Font::bold));
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, text) + 12.0f;
    const auto line = area (x1, cy, x2, cy + 1);
    g.fillRect (line.withRight (line.getCentreX() - textWidth / 2.0f));
    g.fillRect (line.withLeft (line.getCentreX() + textWidth / 2.0f));
    g.setFont (font);
    g.drawText (text, line.withSizeKeepingCentre (textWidth, 14.0f), juce::Justification::centred);
}

double initialScale (double saved)
{
    if (saved > 0.0)
        return juce::jlimit (0.5, 1.6, saved);

    // Fit the whole panel on the screen, at most at photo size.
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return juce::jlimit (0.5, 1.0, (static_cast<double> (display->userBounds.getHeight()) - 70.0) / layout::kHeight);
    return 0.8;
}
} // namespace

PadPorterEditor::PadPorterEditor (PadPorterProcessor& p)
    : juce::AudioProcessorEditor (p), processor (p), screen (p), pads (p)
{
    setLookAndFeel (&lookAndFeel);
    panel.setBufferedToImage (true);
    addAndMakeVisible (panel);

    // Knobs and fader.
    mainVolume.getProperties().set (PadPorterLookAndFeel::onPanelProperty, true);
    mainVolume.setPopupDisplayEnabled (true, true, &panel);
    mainVolume.setTextValueSuffix (" dB");
    mainVolume.setTooltip ("MAIN VOLUME");
    mainVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, "master", mainVolume);
    mainVolume.setBounds (centred (285, 280, 124));
    panel.addAndMakeVisible (mainVolume);

    const int knobX[] { 460, 637, 818 };
    const char* knobTips[] { "K1: Tune des Pads (Halbtöne)", "K2: Level des Pads (dB)", "K3: Pan des Pads" };
    for (int i = 0; i < 3; ++i)
    {
        auto& k = knobs[static_cast<std::size_t> (i)];
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setTooltip (juce::String::fromUTF8 (knobTips[i]));
        k.setBounds (centred (knobX[i], 485, 112));
        k.onValueChange = [this, i] { applyKnob (i); };
        k.onDragStart = [this] { screen.setMode (DisplayScreen::Mode::Sample); };
        panel.addAndMakeVisible (k);
    }
    knobs[0].setRange (-36.0, 36.0, 0.1);
    knobs[1].setRange (-60.0, 6.0, 0.1);
    knobs[2].setRange (0.0, 1.0, 0.01);

    fader.setPopupDisplayEnabled (true, true, &panel);
    fader.setTextValueSuffix (" st");
    fader.setTooltip (juce::String::fromUTF8 ("Fader: Global Tune (Halbtöne)"));
    faderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, "tune", fader);
    fader.setBounds (photo (256, 673, 320, 927));
    panel.addAndMakeVisible (fader);

    // Display, soft keys and meter.
    const juce::Rectangle<int> softKeyBounds[] { photo (525, 135, 585, 167), photo (613, 135, 672, 167), photo (700, 135, 759, 167) };
    for (int i = 0; i < 3; ++i)
    {
        auto& key = softKeys[static_cast<std::size_t> (i)];
        key.onClick = [this, i] { screen.pressSoftKey (i); updateKeys(); };
        addKey (key, softKeyBounds[i], {});
    }
    screen.setBounds (photo (517, 192, 761, 376));
    screen.onModeChanged = [this] { updateKeys(); };
    screen.onExport = [this] { chooseExportFolder(); };
    screen.onDetails = [this] { showDiagnostics(); };
    panel.addAndMakeVisible (screen);

    meter.setBounds (photo (816, 196, 858, 371));
    meter.setInterceptsMouseClicks (false, false);
    panel.addAndMakeVisible (meter);

    // Pads.
    pads.setBounds (photo (397, 558, 880, 1045));
    pads.onPadSelected = [this] (int) {
        updateKnobs();
        screen.refresh();
    };
    pads.interceptPad = [this] (int slot) { return handleShiftPad (slot); };
    panel.addAndMakeVisible (pads);

    // Keys with a function in PadPorter.
    sampleKey.onClick = [this] { screen.setMode (DisplayScreen::Mode::Sample); };
    seqKey.onClick = [this] { screen.setMode (DisplayScreen::Mode::Seq); };
    padFxKey.onClick = [this] {
        if (auto* param = processor.parameters.getParameter ("devicecomp"))
            param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
        updateKeys();
    };
    shiftKey.onClick = [this] {
        shiftLatched = ! shiftLatched;
        updateKeys();
    };
    padBankKey.onClick = [this] { changeBank (shiftLatched ? -1 : 1); };
    eraseKey.onClick = [this] {
        processor.resetOverride (processor.getSelectedPad());
        refreshAll();
    };
    minusKey.onClick = [this] { step (-1); };
    plusKey.onClick = [this] { step (1); };
    stopKey.onClick = [this] { processor.stopPreview(); };
    playKey.onClick = [this] {
        const auto* loaded = processor.getLoadedProject();
        const int seq = processor.getSelectedSequence();
        if (loaded != nullptr && seq >= 0 && seq < static_cast<int> (loaded->project->sequences.size()))
            processor.startPreview (seq, Part::All);
    };
    jog.onStep = [this] (int steps) { step (steps); };

    addKey (sampleKey, photo (204, 437, 276, 479), juce::String::fromUTF8 ("SAMPLE: ausgewähltes Pad mit Wellenform; Softkeys = Auto / Drums / Melodie"));
    addKey (seqKey, photo (299, 437, 372, 479), juce::String::fromUTF8 ("SEQ: Sequenzen, Pattern und Projekte"));
    addKey (padFxKey, photo (204, 499, 276, 541), juce::String::fromUTF8 ("PAD FX: Master-Kompressor des Geräts nachbilden (wenn im Projekt aktiv)"));
    addKey (knobFxKey, photo (299, 499, 372, 541), {});
    addKey (shiftKey, photo (205, 581, 275, 623), juce::String::fromUTF8 ("SHIFT (rastet ein): + PAD BANK = Bank zurück, + Pad 16 = Projekte, + Pad 12 = Sequenzen, + Pad 5 = Kompressor"));
    addKey (padBankKey, photo (299, 583, 372, 625), juce::String::fromUTF8 ("PAD BANK: nächste Bank (A–H)"));
    addKey (eraseKey, photo (204, 982, 276, 1024), juce::String::fromUTF8 ("ERASE: Tune/Level/Pan und Teil des Pads auf Projektwerte zurücksetzen"));
    addKey (noteRepeatKey, photo (299, 982, 372, 1024), {});
    addKey (chopKey, photo (906, 437, 979, 479), {});
    addKey (muteKey, photo (1003, 437, 1075, 479), {});
    addKey (loopKey, photo (906, 499, 979, 541), {});
    addKey (levelsKey, photo (1003, 499, 1075, 541), {});
    addKey (sampleSelectKey, photo (906, 583, 979, 625), {});
    addKey (tapTempoKey, photo (1003, 583, 1075, 625), {});
    addKey (minusKey, photo (905, 818, 977, 860), juce::String::fromUTF8 ("Vorherige Sequence bzw. vorheriges Pad"));
    addKey (plusKey, photo (1003, 818, 1075, 860), juce::String::fromUTF8 ("Nächste Sequence bzw. nächstes Pad"));
    addKey (sampleRecordKey, photo (905, 897, 977, 950), {});
    addKey (seqRecordKey, photo (1000, 897, 1072, 950), {});
    addKey (stopKey, photo (903, 970, 975, 1023), "Vorhören stoppen");
    addKey (playKey, photo (998, 970, 1070, 1023), juce::String::fromUTF8 ("Ausgewählte Sequence in PadPorter anhören (loopt)"));

    jog.setBounds (centred (990, 736, 128));
    jog.setTooltip (juce::String::fromUTF8 ("Drehen: Sequence bzw. Pad wählen"));
    panel.addAndMakeVisible (jog);

    seenDeviceGeneration = processor.getDeviceGeneration();
    if (const auto* loaded = processor.getLoadedProject())
        seenProjectGeneration = loaded->generation;
    else
        screen.showPage (DisplayScreen::projectsPage);

    // The window keeps the panel's proportions.
    const double scale = initialScale (processor.getEditorScale());
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (layout::kWidth * 0.5), juce::roundToInt (layout::kHeight * 0.5),
                     juce::roundToInt (layout::kWidth * 1.6), juce::roundToInt (layout::kHeight * 1.6));
    getConstrainer()->setFixedAspectRatio (static_cast<double> (layout::kWidth) / layout::kHeight);
    setSize (juce::roundToInt (layout::kWidth * scale), juce::roundToInt (layout::kHeight * scale));
    constructed = true;

    processor.addChangeListener (this);
    refreshAll();
    startTimerHz (30);
}

PadPorterEditor::~PadPorterEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void PadPorterEditor::addKey (HardwareButton& key, juce::Rectangle<int> bounds, const juce::String& tooltip)
{
    key.setBounds (bounds);
    key.setTooltip (tooltip);
    panel.addAndMakeVisible (key);
}

void PadPorterEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (layout::kWidth);
    panel.setBounds (0, 0, layout::kWidth, layout::kHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));
    if (constructed)
        processor.setEditorScale (scale);
}

void PadPorterEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshAll();
}

void PadPorterEditor::refreshAll()
{
    const auto* loaded = processor.getLoadedProject();

    // A freshly loaded project shows its sequences; a newly connected device shows its projects.
    if (loaded != nullptr && loaded->generation != seenProjectGeneration)
    {
        seenProjectGeneration = loaded->generation;
        screen.showPage (DisplayScreen::sequencesPage);
    }
    if (processor.getDeviceGeneration() != seenDeviceGeneration)
    {
        seenDeviceGeneration = processor.getDeviceGeneration();
        screen.showPage (DisplayScreen::projectsPage);
    }

    screen.refresh();
    updateKnobs();
    updateKeys();
    pads.repaint();

    // Bank letters and the connection LED are printed on the panel.
    const bool device = ! processor.getDevices().empty();
    if (processor.getBank() != shownBank || device != shownDevice)
    {
        shownBank = processor.getBank();
        shownDevice = device;
        panel.repaint (photo (370, 240, 460, 370));
    }
}

void PadPorterEditor::updateKeys()
{
    const bool seqMode = screen.getMode() == DisplayScreen::Mode::Seq;
    sampleKey.setLit (! seqMode);
    seqKey.setLit (seqMode);
    shiftKey.setLit (shiftLatched);

    const bool compOn = processor.parameters.getRawParameterValue ("devicecomp")->load() > 0.5f;
    padFxKey.setLit (compOn && processor.projectHasMasterCompressor());
    playKey.setLit (processor.getPreviewSequence() >= 0);

    const juce::StringArray seqTips { "Sequenzen", "Pattern: Drums / Melodie / Alles anhören und ziehen", "Projekte: Gerät, Bibliothek, Export" };
    const juce::StringArray padTips { "Teil des Pads automatisch erkennen", "Pad gehört zu DRUMS", "Pad gehört zu MELODIE" };
    for (int i = 0; i < 3; ++i)
        softKeys[static_cast<std::size_t> (i)].setTooltip (seqMode ? seqTips[i] : padTips[i]);
    screen.repaint();
}

void PadPorterEditor::updateKnobs()
{
    const int padIndex = processor.getSelectedPad();
    const auto* loaded = processor.getLoadedProject();
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
    const auto values = processor.getEffectivePad (padIndex);

    const juce::ScopedValueSetter<bool> guard (updatingKnobs, true);
    knobs[0].setValue (values.tuneSemitones, juce::dontSendNotification);
    knobs[1].setValue (values.gainDb, juce::dontSendNotification);
    knobs[2].setValue (values.pan, juce::dontSendNotification);
    for (auto& k : knobs)
        k.setEnabled (pad != nullptr && pad->hasSample());
}

void PadPorterEditor::applyKnob (int knob)
{
    if (updatingKnobs)
        return;

    const int padIndex = processor.getSelectedPad();
    auto ov = processor.getOverride (padIndex);
    const double value = knobs[static_cast<std::size_t> (knob)].getValue();
    if (knob == 0)
        ov.tuneSemitones = value;
    else if (knob == 1)
        ov.gainDb = value;
    else
        ov.pan = value;
    processor.setOverride (padIndex, ov);
    screen.refresh();
}

void PadPorterEditor::step (int delta)
{
    const auto* loaded = processor.getLoadedProject();
    if (loaded == nullptr || delta == 0)
        return;

    if (screen.getMode() == DisplayScreen::Mode::Sample)
    {
        const int padIndex = juce::jlimit (0, kMaxPads - 1, processor.getSelectedPad() + delta);
        processor.setBank (padIndex / kPadsPerBank);
        processor.setSelectedPad (padIndex);
        refreshAll();
        return;
    }

    const int count = static_cast<int> (loaded->project->sequences.size());
    if (count > 0)
        processor.setSelectedSequence (juce::jlimit (0, count - 1, processor.getSelectedSequence() + delta));
}

void PadPorterEditor::changeBank (int delta)
{
    const int bank = (processor.getBank() + delta + 8) % 8;
    processor.setBank (bank);
    processor.setSelectedPad (bank * kPadsPerBank + processor.getSelectedPad() % kPadsPerBank);
    shiftLatched = false;
    refreshAll();
}

bool PadPorterEditor::handleShiftPad (int slot)
{
    if (! shiftLatched)
        return false;

    // The printed shift functions that have a counterpart in PadPorter.
    shiftLatched = false;
    if (slot == 15)
        screen.showPage (DisplayScreen::projectsPage);
    else if (slot == 11)
        screen.showPage (DisplayScreen::sequencesPage);
    else if (slot == 4)
        padFxKey.onClick();
    updateKeys();
    return true;
}

void PadPorterEditor::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch)
        meterLevels[ch] = juce::jmax (processor.takeOutputPeak (ch), meterLevels[ch] * 0.82f);
    meter.setLevel (meterLevels[0], meterLevels[1]);

    if (playKey.isLit() != (processor.getPreviewSequence() >= 0))
        updateKeys();
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

// ---------------------------------------------------------------------------
// Front panel

void PadPorterEditor::paintPanel (juce::Graphics& g)
{
    // Lower body.
    const auto body = area (166, 380, 1110, 1215);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (body.translated (0.0f, 3.0f), 28.0f);
    g.setGradientFill (juce::ColourGradient (theme::hwBody, 0.0f, body.getY(), theme::hwBodyShade, 0.0f, body.getBottom(), false));
    g.fillRoundedRectangle (body, 28.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRoundedRectangle (body.reduced (1.0f), 27.0f, 1.2f);

    // Top housing: light lip with the encoder and port on its edge, then the black panel.
    const auto lip = area (152, 62, 1122, 150);
    g.setGradientFill (juce::ColourGradient (theme::hwLip.brighter (0.08f), 0.0f, lip.getY(), theme::hwBody, 0.0f, lip.getBottom(), false));
    g.fillRoundedRectangle (lip, 24.0f);
    g.setColour (juce::Colours::black.withAlpha (0.1f));
    g.drawRoundedRectangle (lip.reduced (0.5f), 24.0f, 1.0f);
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (area (402, 60, 428, 84), 5.0f);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillRoundedRectangle (area (248, 74, 282, 86), 3.0f);

    const auto blackPanel = area (152, 108, 1122, 398);
    juce::Path panelShape;
    panelShape.addRoundedRectangle (blackPanel.getX(), blackPanel.getY(), blackPanel.getWidth(), blackPanel.getHeight(), 22.0f, 22.0f, true, true, false, false);
    g.setColour (theme::hwPanel);
    g.fillPath (panelShape);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), 0.0f, blackPanel.getY(), juce::Colours::transparentWhite, 0.0f, blackPanel.getY() + 90.0f, false));
    g.fillPath (panelShape);

    // Maker logo (PadPorter's own name in place of the manufacturer's).
    g.setColour (theme::logoRed);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 34.0f, juce::Font::bold)));
    g.drawFittedText ("PADPORTER", photo (190, 128, 345, 170), juce::Justification::centredLeft, 1, 0.6f);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 19.0f, juce::Font::italic)));
    g.drawText ("sampler", photo (220, 168, 342, 194), juce::Justification::centredRight);

    // MAIN VOLUME label.
    g.setColour (theme::hwLabel);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("MAIN", photo (235, 328, 335, 342), juce::Justification::centred);
    g.drawText ("VOLUME", photo (235, 344, 335, 358), juce::Justification::centred);

    // Pad bank indicator: A-D below, E-H above; the current bank is lit.
    g.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
    for (int b = 0; b < 8; ++b)
    {
        const int cx = 385 + (b % 4) * 19;
        const int cy = b < 4 ? 284 : 257;
        g.setColour (b == processor.getBank() ? juce::Colours::white : juce::Colour (0xff131313));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + b)), photo (cx - 9, cy - 11, cx + 9, cy + 11), juce::Justification::centred);
    }

    // Connection LED: lit while a device is connected.
    const auto led = area (388, 351, 414, 364);
    const bool connected = ! processor.getDevices().empty();
    if (connected)
    {
        g.setColour (theme::ledGreen.withAlpha (0.3f));
        g.fillRoundedRectangle (led.expanded (3.0f), 6.0f);
    }
    g.setColour (connected ? theme::ledGreen : juce::Colour (0xff17261d));
    g.fillRoundedRectangle (led, 4.0f);

    // Model name in outlined letters.
    {
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (juce::Font (juce::FontOptions (31.0f, juce::Font::bold)).withHorizontalScale (1.35f), "XPJ SAMPLE",
                              area (818, 130, 1094, 166).getX(), area (818, 130, 1094, 166).getY(), area (818, 130, 1094, 166).getWidth(),
                              area (818, 130, 1094, 166).getHeight(), juce::Justification::centredRight, 1, 0.7f);
        juce::Path outline;
        glyphs.createPath (outline);
        g.setColour (juce::Colour (0xff2b0f09));
        g.fillPath (outline);
        g.setColour (theme::logoRed);
        g.strokePath (outline, juce::PathStrokeType (1.5f));
    }

    // Speaker grille.
    const auto grille = area (892, 183, 1082, 357);
    g.setColour (juce::Colour (0xff1e1e1e));
    g.fillRoundedRectangle (grille, 16.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path shape;
        shape.addRoundedRectangle (grille.reduced (5.0f), 12.0f);
        g.reduceClipRegion (shape);
        g.setColour (juce::Colour (0xff050505));
        const float pitch = 5.0f;
        int row = 0;
        for (float y = grille.getY() + 4.0f; y < grille.getBottom(); y += pitch * 0.87f, ++row)
            for (float x = grille.getX() + 4.0f + (row % 2 == 0 ? 0.0f : pitch / 2.0f); x < grille.getRight(); x += pitch)
                g.fillEllipse (x, y, 3.0f, 3.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (grille, 16.0f, 1.2f);

    // Display window.
    g.setColour (juce::Colours::black);
    g.fillRect (area (513, 188, 765, 380));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRect (area (513, 188, 765, 380), 1.0f);
    g.setColour (theme::green.brighter (0.4f));
    g.fillRect (area (513, 188, 517, 192));

    // Printed captions on the grey body.
    g.setColour (theme::hwCaption);
    heading (g, "MODE", 205, 372, 426);
    heading (g, "PAD PLAY", 905, 1074, 426);
    g.fillRect (area (205, 572, 372, 573));
    g.fillRect (area (905, 572, 1074, 573));

    caption (g, "INPUT CONFIG", 240, 488);
    caption (g, "STEP EDIT", 335, 488);
    caption (g, "FLEX BEAT", 240, 550);
    caption (g, "FX SELECT", 335, 550);
    caption (g, "COPY", 240, 1032);
    caption (g, "TRIPLET", 335, 1032);
    caption (g, "NOTE ON", 942, 488);
    caption (g, "UNMUTE ALL", 1039, 488);
    caption (g, "REVERSE", 942, 550);
    caption (g, "TYPE", 1039, 550);
    caption (g, "SAVE SAMPLE", 942, 633);
    caption (g, "METRO", 1039, 633);
    caption (g, "UNDO", 941, 868);
    caption (g, "REDO", 1039, 868);
    caption (g, "RECALL", 941, 959);
    caption (g, "RECALL", 1036, 959);
    caption (g, "CONTINUE", 1034, 1032);
    caption (g, "K1", 460, 542, 11.5f);
    caption (g, "K2", 637, 542, 11.5f);
    caption (g, "K3", 818, 542, 11.5f);

    // SHIFT sits in a black frame with its name printed below the key.
    const auto shiftFrame = area (199, 577, 281, 648);
    g.setColour (juce::Colour (0xff1d1c21));
    g.fillRoundedRectangle (shiftFrame, 8.0f);
    g.setColour (theme::hwLabel);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("SHIFT", shiftFrame.withTop (shiftFrame.getBottom() - 21.0f), juce::Justification::centred);

    // Fader housing and the white LED beside it.
    const auto housing = area (256, 673, 320, 927);
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillRoundedRectangle (housing.translated (0.0f, 2.0f), 7.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd2d1cd), housing.getX(), 0.0f, juce::Colour (0xffa9a8a4), housing.getRight(), 0.0f, false));
    g.fillRoundedRectangle (housing, 7.0f);
    g.setColour (juce::Colour (0xffa3a29e));
    g.fillRoundedRectangle (housing.reduced (6.0f, 10.0f), 4.0f);
    const auto faderLed = area (334, 794, 358, 806);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRoundedRectangle (faderLed.expanded (3.0f), 5.0f);
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (faderLed, 3.0f);

    // Microphone symbol above the data wheel.
    {
        const auto mic = area (1060, 664, 1076, 690);
        g.setColour (theme::hwCaption);
        g.drawRoundedRectangle (mic, 7.0f, 2.0f);
        for (int i = 0; i < 4; ++i)
            g.fillEllipse (mic.getCentreX() - 1.5f + (i % 2 == 0 ? -2.5f : 2.5f), mic.getY() + 6.0f + static_cast<float> (i / 2) * 7.0f, 3.0f, 3.0f);
        juce::Path stand;
        stand.addCentredArc (mic.getCentreX(), mic.getCentreY() + 2.0f, 11.0f, 13.0f, 0.0f, juce::MathConstants<float>::pi * 0.62f, juce::MathConstants<float>::pi * 1.38f, true);
        stand.startNewSubPath (mic.getCentreX(), mic.getBottom() + 3.0f);
        stand.lineTo (mic.getCentreX(), mic.getBottom() + 9.0f);
        g.strokePath (stand, juce::PathStrokeType (2.0f));
    }

    // Palm rest.
    const auto palm = area (177, 1062, 1097, 1206);
    g.setGradientFill (juce::ColourGradient (theme::palmRestHw.darker (0.15f), 0.0f, palm.getY(), theme::palmRestHw, 0.0f, palm.getY() + 30.0f, false));
    g.fillRoundedRectangle (palm, 22.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawRoundedRectangle (palm.reduced (0.5f), 22.0f, 1.0f);
}
} // namespace padporter::plugin
