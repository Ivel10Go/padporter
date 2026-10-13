#include "DisplayScreen.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <padporter/PathUtil.h>

namespace padporter::plugin
{
namespace
{
constexpr int kTabHeight = 22;
constexpr int kToastHeight = 17;

juce::String padLabel (int padIndex)
{
    return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + padIndex / kPadsPerBank))
         + juce::String (padIndex % kPadsPerBank + 1).paddedLeft ('0', 2);
}

juce::String signedValue (double value, int decimals)
{
    return (value > 0.0 ? "+" : "") + juce::String (value, decimals);
}
} // namespace

// ---------------------------------------------------------------------------

PadPage::PadPage (PadPorterProcessor& p) : processor (p)
{
    waveform.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (waveform);
}

juce::Rectangle<int> PadPage::waveArea() const
{
    return { 6, 24, getWidth() - 12, 88 };
}

void PadPage::resized()
{
    waveform.setBounds (waveArea().reduced (2, 3));
}

void PadPage::refresh()
{
    const auto* loaded = processor.getLoadedProject();
    const int padIndex = processor.getSelectedPad();
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
    const bool hasSample = pad != nullptr && pad->hasSample();

    if (hasSample && loaded->pads[static_cast<std::size_t> (padIndex)].sample != nullptr)
    {
        const auto& sample = loaded->pads[static_cast<std::size_t> (padIndex)].sample;
        waveform.setSample (sample, 0, sample->audio.getNumSamples());
    }
    else
    {
        waveform.setMessage (loaded == nullptr ? juce::String ("kein Projekt") : (hasSample ? juce::String ("Sample fehlt") : juce::String ("leeres Pad")));
    }
    repaint();
}

void PadPage::paint (juce::Graphics& g)
{
    const auto* loaded = processor.getLoadedProject();
    const int padIndex = processor.getSelectedPad();
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
    const bool hasSample = pad != nullptr && pad->hasSample();
    const auto values = processor.getEffectivePad (padIndex);
    const auto ov = processor.getOverride (padIndex);

    // Pad and sample name, then the playback icons on the right.
    auto line = juce::Rectangle<int> (6, 2, getWidth() - 12, 16);
    auto icons = line.removeFromRight (64);
    g.setColour (theme::waveform);
    g.setFont (theme::lcdFont (11.0f, true));
    g.drawText (padLabel (padIndex), line.removeFromLeft (30), juce::Justification::centredLeft);
    g.drawText (hasSample ? fromUtf8 (pad->layers.front().sampleName) : juce::String ("(leer)"), line, juce::Justification::centredLeft, true);

    if (pad != nullptr)
    {
        const juce::String iconText[] { values.oneShot ? "1S" : "GT", "M" + juce::String (values.muteGroup), juce::String (values.output + 1) };
        for (int i = 2; i >= 0; --i)
        {
            const auto box = icons.removeFromRight (20).reduced (1, 1).toFloat();
            g.setColour (theme::screenDim);
            g.drawRoundedRectangle (box.reduced (0.5f), 1.5f, 1.0f);
            g.setColour (theme::lcdTabText);
            g.setFont (theme::lcdFont (8.5f, true));
            g.drawText (iconText[i], box, juce::Justification::centred);
            icons.removeFromRight (1);
        }
    }

    // Waveform frame with tick marks above and below.
    const auto wave = waveArea().toFloat();
    g.setColour (theme::lcdFrame);
    g.drawRect (wave, 1.0f);
    g.setColour (theme::lcdTabText.withAlpha (0.7f));
    for (int i = 0; i <= 16; ++i)
    {
        const auto x = wave.getX() + 3.0f + static_cast<float> (i) * (wave.getWidth() - 6.0f) / 16.0f;
        const float h = i % 4 == 0 ? 4.0f : 2.0f;
        g.fillRect (x - 0.5f, wave.getY() - 3.0f - h, 1.0f, h);
        g.fillRect (x - 0.5f, wave.getBottom() + 3.0f, 1.0f, h);
    }
    g.setColour (juce::Colours::white);
    g.fillRect (wave.getX() - 3.0f, wave.getY() + 6.0f, 2.0f, wave.getHeight() - 12.0f);

    // K1 - K3: Tune, Level, Pan. Values changed in PadPorter are shown in blue.
    struct Field
    {
        const char* name;
        juce::String value;
        float proportion;
        bool bipolar;
        bool changed;
    };
    const double panPercent = (values.pan - 0.5) * 200.0;
    const Field fields[] {
        { "Tune", signedValue (values.tuneSemitones, 1), static_cast<float> ((values.tuneSemitones + 36.0) / 72.0), true, ov.tuneSemitones.has_value() },
        { "Level", juce::String (values.gainDb, 1) + " dB", static_cast<float> ((values.gainDb + 60.0) / 66.0), false, ov.gainDb.has_value() },
        { "Pan", std::abs (panPercent) < 0.5 ? juce::String ("C") : (panPercent < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (panPercent))),
          static_cast<float> (values.pan), true, ov.pan.has_value() },
    };

    const int columnWidth = getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        const auto& f = fields[i];
        auto column = juce::Rectangle<int> (i * columnWidth, getHeight() - 40, columnWidth, 38).reduced (7, 4);
        auto label = column.removeFromTop (16);
        g.setColour (f.changed ? theme::lcdBlue : theme::lcdTabText);
        g.setFont (theme::lcdFont (10.5f, true));
        g.drawText (f.name, label, juce::Justification::centredLeft);
        g.setFont (theme::lcdFont (9.0f));
        g.drawText (pad != nullptr ? f.value : juce::String(), label, juce::Justification::centredRight);

        const auto track = column.removeFromTop (8).toFloat().withSizeKeepingCentre (static_cast<float> (column.getWidth()), 3.0f);
        g.setColour (juce::Colour (0xff3a3a3c));
        g.fillRect (track);
        if (pad != nullptr)
        {
            const float p = juce::jlimit (0.0f, 1.0f, f.proportion);
            const float from = f.bipolar ? 0.5f : 0.0f;
            g.setColour (f.changed ? theme::lcdBlue : juce::Colours::white);
            g.fillRect (track.getX() + track.getWidth() * juce::jmin (from, p), track.getY(), juce::jmax (2.0f, track.getWidth() * std::abs (p - from)), track.getHeight());
        }
    }

    // The field bracket of the device marks the knob row.
    g.setColour (theme::lcdTabText);
    const auto bracket = juce::Rectangle<float> (4.0f, static_cast<float> (getHeight() - 40), static_cast<float> (columnWidth) - 4.0f, 10.0f);
    juce::Path b;
    b.startNewSubPath (bracket.getX(), bracket.getBottom());
    b.lineTo (bracket.getX(), bracket.getY());
    b.lineTo (bracket.getRight(), bracket.getY());
    b.lineTo (bracket.getRight(), bracket.getY() + 4.0f);
    g.strokePath (b, juce::PathStrokeType (1.0f));
}

// ---------------------------------------------------------------------------

DisplayScreen::DisplayScreen (PadPorterProcessor& p)
    : processor (p), sequences (p), pattern (p), projects (p), padPage (p)
{
    addChildComponent (sequences);
    addChildComponent (pattern);
    addChildComponent (projects);
    addChildComponent (padPage);

    sequences.onSequenceOpened = [this] { showPage (patternPage); };
    projects.onExport = [this] { if (onExport) onExport(); };
    projects.onDetails = [this] { if (onDetails) onDetails(); };

    updateVisibility();
    startTimerHz (10);
}

DisplayScreen::~DisplayScreen()
{
    stopTimer();
}

void DisplayScreen::setMode (Mode newMode)
{
    mode = newMode;
    updateVisibility();
}

void DisplayScreen::showPage (int newPage)
{
    mode = Mode::Seq;
    page = juce::jlimit (0, 2, newPage);
    updateVisibility();
}

void DisplayScreen::pressSoftKey (int index)
{
    if (mode == Mode::Seq)
    {
        if (index == projectsPage)
            processor.rescanDevices();
        showPage (index);
        return;
    }

    // SAMPLE mode: the soft keys set the selected pad's part.
    const int padIndex = processor.getSelectedPad();
    const auto* loaded = processor.getLoadedProject();
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
    if (pad == nullptr || ! pad->hasSample())
        return;
    if (index == 0)
        processor.setPadRole (padIndex, std::nullopt);
    else
        processor.setPadRole (padIndex, index == 1 ? PartRole::Drums : PartRole::Melody);
    refresh();
}

void DisplayScreen::updateVisibility()
{
    sequences.setVisible (mode == Mode::Seq && page == sequencesPage);
    pattern.setVisible (mode == Mode::Seq && page == patternPage);
    projects.setVisible (mode == Mode::Seq && page == projectsPage);
    padPage.setVisible (mode == Mode::Sample);
    if (projects.isVisible())
        projects.refresh();
    repaint();
    if (onModeChanged)
        onModeChanged();
}

void DisplayScreen::refresh()
{
    sequences.refresh();
    pattern.refresh();
    padPage.refresh();
    if (projects.isVisible())
        projects.refresh();
    repaint();
}

juce::StringArray DisplayScreen::tabLabels() const
{
    if (mode == Mode::Seq)
        return { "Sequenzen", "Pattern", "Projekte" };
    return { "Auto", "Drums", "Melodie" };
}

int DisplayScreen::activeTab() const
{
    if (mode == Mode::Seq)
        return page;

    const int padIndex = processor.getSelectedPad();
    const auto* loaded = processor.getLoadedProject();
    const auto* pad = loaded != nullptr ? loaded->pad (padIndex) : nullptr;
    if (pad == nullptr || ! pad->hasSample())
        return -1;
    if (processor.isPadRoleAutomatic (padIndex))
        return 0;
    return processor.getPadRole (padIndex) == PartRole::Melody ? 2 : 1;
}

void DisplayScreen::paint (juce::Graphics& g)
{
    g.fillAll (theme::lcd);

    // Tab row of the soft keys.
    const auto labels = tabLabels();
    const int active = activeTab();
    const float tabWidth = static_cast<float> (getWidth()) / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto tab = juce::Rectangle<float> (static_cast<float> (i) * tabWidth, 0.0f, tabWidth, static_cast<float> (kTabHeight));
        const auto box = tab.reduced (7.0f, 0.0f).withTrimmedTop (3.0f).withHeight (13.0f);
        const bool on = i == active;
        if (on)
        {
            g.setColour (juce::Colours::white);
            g.fillRect (box);
        }
        g.setColour (on ? juce::Colours::black : theme::lcdTabText);
        g.setFont (theme::lcdFont (10.5f, true));
        g.drawText (labels[i], box, juce::Justification::centred);

        // Three page dashes under each tab; the first one is lit on the active tab.
        for (int d = 0; d < 3; ++d)
        {
            g.setColour (on && d == 0 ? juce::Colours::white : juce::Colour (0xff4a4a4c));
            g.fillRect (box.getCentreX() - 10.0f + static_cast<float> (d) * 7.5f, box.getBottom() + 2.0f, 5.5f, 1.6f);
        }
    }
}

void DisplayScreen::paintOverChildren (juce::Graphics& g)
{
    if (toast.isEmpty())
        return;

    const auto bar = getLocalBounds().removeFromBottom (kToastHeight).toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.92f));
    g.fillRect (bar);
    g.setColour (toastError ? theme::error : theme::waveform);
    g.drawHorizontalLine (juce::roundToInt (bar.getY()), bar.getX(), bar.getRight());
    g.setFont (theme::lcdFont (9.0f, true));
    g.drawFittedText (toast, bar.reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
}

void DisplayScreen::resized()
{
    const auto content = getLocalBounds().withTrimmedTop (kTabHeight + 2);
    sequences.setBounds (content.reduced (4, 0).withTrimmedBottom (3));
    pattern.setBounds (content.reduced (4, 0).withTrimmedBottom (3));
    projects.setBounds (content.reduced (4, 0).withTrimmedBottom (3));
    padPage.setBounds (content);
}

void DisplayScreen::mouseDown (const juce::MouseEvent& e)
{
    // The tabs can also be clicked directly on the screen.
    if (e.y < kTabHeight)
        pressSoftKey (juce::jlimit (0, 2, e.x * 3 / juce::jmax (1, getWidth())));
}

void DisplayScreen::timerCallback()
{
    // Status messages of the processor appear for a few seconds at the bottom of the screen.
    const auto status = processor.getStatusText();
    if (status != lastStatus)
    {
        lastStatus = status;
        toast = status;
        toastError = processor.statusIsError();
        toastTicks = toastError ? 80 : 40;
        repaint();
    }
    else if (toastTicks > 0 && ! processor.isLoading() && --toastTicks == 0)
    {
        toast.clear();
        repaint();
    }
}
} // namespace padporter::plugin
