#include "PatternView.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <padporter/PathUtil.h>

#include <map>

namespace padporter::plugin
{
namespace
{
constexpr int kTitleHeight = 16;
constexpr int kChipHeight = 24;
constexpr int kLabelWidth = 30;
constexpr int kRulerHeight = 10;
} // namespace

PatternView::PatternView (PadPorterProcessor& p) : processor (p)
{
    chips[0].part = Part::Drums;
    chips[1].part = Part::Melody;
    chips[2].part = Part::All;
    startTimerHz (30);
}

PatternView::~PatternView()
{
    stopTimer();
}

juce::Colour PatternView::colourFor (Part part) const
{
    switch (part)
    {
        case Part::Drums:  return theme::drums;
        case Part::Melody: return theme::melody;
        case Part::All:    break;
    }
    return theme::screenText;
}

void PatternView::refresh()
{
    lanes.clear();
    sequenceIndex = -1;

    const auto* loaded = processor.getLoadedProject();
    const auto* track = loaded != nullptr ? loaded->track() : nullptr;
    const int index = processor.getSelectedSequence();
    if (track == nullptr || index < 0 || index >= static_cast<int> (loaded->project->sequences.size()))
    {
        for (auto& c : chips)
            c.count = 0;
        repaint();
        return;
    }

    sequenceIndex = index;
    const auto& seq = loaded->project->sequences[static_cast<std::size_t> (index)];
    lengthTicks = juce::jmax<std::int64_t> (1, seq.lengthTicks());
    ticksPerBeat = juce::jmax (1, seq.timeSignature.ticksPerBeat);
    ticksPerBar = juce::jmax (1, seq.timeSignature.ticksPerBar());

    const auto counts = processor.getPartCounts (index);
    chips[0].count = counts.drums;
    chips[1].count = counts.melody;
    chips[2].count = counts.drums + counts.melody + counts.unassigned;

    const auto* clip = seq.clipForTrack (track->name);
    if (clip == nullptr)
    {
        repaint();
        return;
    }

    const auto roles = processor.getRoles();

    // One lane per pad (or per note without a pad), ordered by group and pad index.
    std::map<std::pair<int, int>, Lane> byKey;
    for (const auto& n : clip->notes)
    {
        if (n.muted)
            continue;

        const auto* pad = track->padForNote (n.note);
        Group group = Group::Unassigned;
        int order = 1000 + n.note;
        if (pad != nullptr && pad->index >= 0 && pad->index < kMaxPads)
        {
            group = roles[static_cast<std::size_t> (pad->index)] == PartRole::Melody ? Group::Melody : Group::Drums;
            order = pad->index;
        }

        auto& lane = byKey[{ static_cast<int> (group), order }];
        if (lane.notes.empty())
        {
            lane.group = group;
            lane.padIndex = pad != nullptr ? pad->index : -1;
            if (pad != nullptr)
            {
                lane.label = fromUtf8 (pad->label());
                lane.name = fromUtf8 (pad->layers.front().sampleName);
            }
            else
            {
                lane.label = fromUtf8 (midiNoteName (n.note));
                const Pad* empty = nullptr;
                for (const auto& candidate : track->pads)
                    if (candidate.midiNote == n.note)
                        empty = &candidate;
                lane.name = empty != nullptr ? "Pad " + fromUtf8 (empty->label()) + " leer" : juce::String ("kein Pad");
            }
        }
        lane.notes.push_back ({ n.tick, juce::jmax<std::int64_t> (1, n.length), static_cast<float> (n.velocity), static_cast<float> (n.tuneSemitones()) });
        lane.tuned = lane.tuned || n.isTuned();
    }

    for (auto& [key, lane] : byKey)
        lanes.push_back (std::move (lane));

    repaint();
}

void PatternView::layoutChips()
{
    auto title = getLocalBounds().withHeight (kTitleHeight);
    audioToggle = title.removeFromRight (32).withSizeKeepingCentre (32, 12);
    title.removeFromRight (2);
    midiToggle = title.removeFromRight (28).withSizeKeepingCentre (28, 12);

    auto row = getLocalBounds().withTrimmedTop (kTitleHeight + 2).withHeight (kChipHeight);
    const int gap = 4;
    const int width = (row.getWidth() - 2 * gap) / 3;
    for (auto& chip : chips)
    {
        chip.bounds = row.removeFromLeft (width);
        row.removeFromLeft (gap);
        chip.playBounds = chip.bounds.withWidth (chip.bounds.getHeight()).reduced (4);
    }
}

void PatternView::resized()
{
    layoutChips();
}

void PatternView::paintChip (juce::Graphics& g, const Chip& chip, int index) const
{
    const auto colour = colourFor (chip.part);
    const bool enabled = chip.count > 0;
    const bool playing = sequenceIndex >= 0 && processor.getPreviewSequence() == sequenceIndex && processor.getPreviewPart() == chip.part;
    const auto bounds = chip.bounds.toFloat();

    g.setColour (colour.withAlpha (enabled ? (index == hoverChip ? 0.24f : 0.15f) : 0.05f));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (colour.withAlpha (enabled ? (playing ? 1.0f : 0.55f) : 0.15f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, playing ? 1.6f : 0.8f);

    // Play / stop button.
    const auto play = chip.playBounds.toFloat();
    g.setColour (enabled ? colour : colour.withAlpha (0.25f));
    g.fillEllipse (play);
    g.setColour (theme::lcd);
    if (playing)
        g.fillRect (play.withSizeKeepingCentre (play.getWidth() * 0.34f, play.getHeight() * 0.34f));
    else
    {
        juce::Path triangle;
        const auto c = play.getCentre();
        const auto s = play.getWidth() * 0.22f;
        triangle.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 1.1f, c.y);
        g.fillPath (triangle);
    }

    auto text = chip.bounds.withTrimmedLeft (chip.bounds.getHeight() - 2).reduced (1, 2);
    g.setColour (enabled ? theme::screenText : theme::screenDim.withAlpha (0.5f));
    g.setFont (theme::lcdFont (9.0f, true));
    g.drawText (juce::String (partName (chip.part)).toUpperCase(), text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour (theme::screenDim);
    g.setFont (theme::lcdFont (8.0f));
    g.drawText (juce::String (chip.count) + " Noten", text, juce::Justification::topLeft, true);
}

void PatternView::paintLanes (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (lanes.empty())
    {
        g.setColour (theme::screenDim);
        g.setFont (theme::lcdFont (10.0f));
        g.drawText (sequenceIndex < 0 ? juce::String() : juce::String ("Diese Sequence ist leer."), area, juce::Justification::centred);
        return;
    }

    const auto ruler = area.removeFromTop (kRulerHeight);
    // Groups are told apart by colour only: the display has no room for headings.
    const float laneHeight = juce::jlimit (3.0f, 12.0f, static_cast<float> (area.getHeight()) / static_cast<float> (lanes.size()));

    const auto grid = area.withTrimmedLeft (kLabelWidth).toFloat();
    const auto toX = [&grid, this] (double tick) {
        return grid.getX() + static_cast<float> (tick / static_cast<double> (lengthTicks)) * grid.getWidth();
    };

    // Ruler with bar numbers.
    g.setFont (theme::lcdFont (8.0f, true));
    const bool crowded = grid.getWidth() * static_cast<float> (ticksPerBar) / static_cast<float> (lengthTicks) < 14.0f;
    for (std::int64_t tick = 0, bar = 1; tick < lengthTicks; tick += ticksPerBar, ++bar)
    {
        if (crowded && (bar - 1) % 4 != 0)
            continue;
        const auto x = toX (static_cast<double> (tick));
        g.setColour (theme::screenDim);
        g.drawText (juce::String (bar), juce::Rectangle<float> (x + 2.0f, static_cast<float> (ruler.getY()), 20.0f, static_cast<float> (ruler.getHeight())),
                    juce::Justification::centredLeft);
    }

    // Grid lines.
    for (std::int64_t tick = 0; tick <= lengthTicks; tick += ticksPerBeat)
    {
        const bool barLine = tick % ticksPerBar == 0;
        g.setColour (barLine ? theme::lcdLine.brighter (0.25f) : theme::lcdLine.withAlpha (0.5f));
        g.drawVerticalLine (juce::roundToInt (toX (static_cast<double> (tick))), grid.getY(), grid.getBottom());
    }

    float y = static_cast<float> (area.getY());

    for (const auto& lane : lanes)
    {
        const auto colour = lane.group == Group::Drums ? theme::drums : (lane.group == Group::Melody ? theme::melody : theme::unassigned);
        const auto row = juce::Rectangle<float> (static_cast<float> (area.getX()), y, static_cast<float> (area.getWidth()), laneHeight);
        const int laneIndex = static_cast<int> (&lane - lanes.data());

        if (lane.padIndex >= 0)
        {
            // The pad's label can be dragged out as its own clip.
            const auto hit = row.withWidth (static_cast<float> (kLabelWidth - 2)).toNearestInt();
            laneHitAreas.emplace_back (hit, laneIndex);
            if (laneIndex == hoverLane)
            {
                g.setColour (colour.withAlpha (0.3f));
                g.fillRect (hit.toFloat());
            }
        }

        if (laneHeight >= 7.0f)
        {
            g.setColour (lane.group == Group::Unassigned ? theme::screenDim : colour);
            g.setFont (theme::lcdFont (juce::jmin (9.0f, laneHeight), true));
            g.drawText (lane.label, row.withWidth (static_cast<float> (kLabelWidth)), juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (colour);
            g.fillRect (row.withWidth (4.0f).reduced (0.0f, 0.5f));
        }

        for (const auto& n : lane.notes)
        {
            if (n.tick >= lengthTicks)
                continue;
            const auto x1 = toX (static_cast<double> (n.tick));
            const auto x2 = toX (static_cast<double> (juce::jmin (lengthTicks, n.tick + n.length)));
            const auto block = juce::Rectangle<float> (x1, row.getY() + 0.5f, juce::jmax (2.0f, x2 - x1), juce::jmax (1.5f, laneHeight - 1.0f));
            g.setColour (colour.withAlpha (0.35f + 0.65f * n.velocity));
            g.fillRoundedRectangle (block, juce::jmin (1.5f, laneHeight * 0.25f));

            // "16 Levels" notes show their pitch offset in semitones.
            if (lane.tuned && block.getWidth() >= 11.0f && laneHeight >= 8.0f)
            {
                const int st = juce::roundToInt (n.semitones);
                g.setColour (theme::lcd);
                g.setFont (theme::lcdFont (juce::jmin (8.0f, laneHeight - 1.0f), true));
                g.drawText (st > 0 ? "+" + juce::String (st) : juce::String (st), block, juce::Justification::centred, false);
            }
        }

        y += laneHeight;
        if (y > static_cast<float> (area.getBottom()))
            break;
    }

    // Playhead while this sequence is previewed.
    if (sequenceIndex >= 0 && processor.getPreviewSequence() == sequenceIndex)
    {
        const auto x = grid.getX() + processor.getPreviewPosition() * grid.getWidth();
        g.setColour (theme::waveform.withAlpha (0.25f));
        g.fillRect (x - 1.5f, grid.getY(), 3.0f, grid.getHeight());
        g.setColour (theme::waveform);
        g.drawVerticalLine (juce::roundToInt (x), grid.getY() - static_cast<float> (kRulerHeight), grid.getBottom());
    }
}

void PatternView::paintDragMode (juce::Graphics& g) const
{
    const bool audio = processor.isDragAsAudio();
    for (const auto& [bounds, active, text] : { std::tuple { midiToggle, ! audio, "MIDI" }, std::tuple { audioToggle, audio, "AUDIO" } })
    {
        g.setColour (active ? theme::waveform : theme::lcdRaised);
        g.fillRect (bounds.toFloat());
        g.setColour (active ? theme::lcd : theme::screenDim);
        g.setFont (theme::lcdFont (8.5f, true));
        g.drawText (text, bounds, juce::Justification::centred);
    }
}

void PatternView::paint (juce::Graphics& g)
{
    laneHitAreas.clear();
    const auto* loaded = processor.getLoadedProject();
    if (loaded == nullptr || sequenceIndex < 0)
    {
        g.setColour (theme::screenDim);
        g.setFont (theme::lcdFont (10.0f));
        g.drawFittedText (loaded == nullptr ? juce::String::fromUTF8 ("Hier erscheinen die Drums und Melodien deiner Sequences.")
                                            : juce::String::fromUTF8 ("Erst unter SEQUENZEN eine Sequence wählen."),
                          getLocalBounds().reduced (8), juce::Justification::centred, 4);
        return;
    }

    const auto& seq = loaded->project->sequences[static_cast<std::size_t> (sequenceIndex)];

    paintDragMode (g);

    auto title = getLocalBounds().removeFromTop (kTitleHeight).withRight (midiToggle.getX() - 4);
    g.setColour (theme::waveform);
    g.setFont (theme::lcdFont (11.0f, true));
    g.drawText (juce::String (seq.number()).paddedLeft ('0', 2), title.removeFromLeft (18), juce::Justification::centredLeft);
    auto tempo = title.removeFromRight (34);
    g.drawText (fromUtf8 (seq.name), title, juce::Justification::centredLeft, true);
    g.setColour (theme::screenDim);
    g.setFont (theme::lcdFont (8.5f));
    g.drawText (juce::String (loaded->project->tempoFor (seq), 0) + " BPM", tempo, juce::Justification::centredRight);

    for (int i = 0; i < 3; ++i)
        paintChip (g, chips[static_cast<std::size_t> (i)], i);

    paintLanes (g, getLocalBounds().withTrimmedTop (kTitleHeight + kChipHeight + 5).withTrimmedBottom (1));
}

int PatternView::laneAt (juce::Point<int> position) const
{
    for (const auto& [bounds, index] : laneHitAreas)
        if (bounds.contains (position))
            return index;
    return -1;
}

void PatternView::mouseMove (const juce::MouseEvent& e)
{
    int over = -1;
    for (int i = 0; i < 3; ++i)
        if (chips[static_cast<std::size_t> (i)].bounds.contains (e.getPosition()))
            over = i;

    const int lane = over < 0 ? laneAt (e.getPosition()) : -1;
    const bool clickable = (over >= 0 && chips[static_cast<std::size_t> (over)].count > 0) || lane >= 0
                           || midiToggle.contains (e.getPosition()) || audioToggle.contains (e.getPosition());

    const auto as = processor.isDragAsAudio() ? juce::String ("Audio") : juce::String ("MIDI");
    juce::String tip;
    if (over >= 0)
        tip = juce::String::fromUTF8 ("▶ = in PadPorter anhören  ·  Chip ziehen = ") + as + "-Clip in Ableton";
    else if (lane >= 0)
    {
        const auto& l = lanes[static_cast<std::size_t> (lane)];
        tip = l.label + juce::String::fromUTF8 ("  ·  ") + l.name + (l.tuned ? " (16 Levels)" : "") + juce::String::fromUTF8 ("  ·  ziehen = nur dieses Pad als ") + as;
    }
    else if (midiToggle.contains (e.getPosition()) || audioToggle.contains (e.getPosition()))
        tip = "Ziehen als MIDI oder als Audio";
    setTooltip (tip);

    setMouseCursor (lane >= 0 ? juce::MouseCursor::DraggingHandCursor
                              : (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    if (over != hoverChip || lane != hoverLane)
    {
        hoverChip = over;
        hoverLane = lane;
        repaint();
    }
}

void PatternView::mouseExit (const juce::MouseEvent&)
{
    hoverChip = -1;
    hoverLane = -1;
    repaint();
}

void PatternView::mouseDown (const juce::MouseEvent& e)
{
    pressedChip = -1;
    pressedLane = -1;
    dragStarted = false;

    if (midiToggle.contains (e.getPosition()) || audioToggle.contains (e.getPosition()))
    {
        processor.setDragAsAudio (audioToggle.contains (e.getPosition()));
        repaint();
        return;
    }

    pressedLane = laneAt (e.getPosition());
    if (pressedLane >= 0)
        return;

    for (int i = 0; i < 3; ++i)
    {
        const auto& chip = chips[static_cast<std::size_t> (i)];
        if (! chip.bounds.contains (e.getPosition()) || chip.count == 0 || sequenceIndex < 0)
            continue;

        if (chip.playBounds.expanded (4).contains (e.getPosition()))
        {
            const bool playing = processor.getPreviewSequence() == sequenceIndex && processor.getPreviewPart() == chip.part;
            if (playing)
                processor.stopPreview();
            else
                processor.startPreview (sequenceIndex, chip.part);
            repaint();
            return;
        }

        pressedChip = i;
    }
}

void PatternView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6 || sequenceIndex < 0)
        return;

    if (pressedChip >= 0)
    {
        dragStarted = true;
        startDrag (chips[static_cast<std::size_t> (pressedChip)].part, -1);
    }
    else if (pressedLane >= 0 && pressedLane < static_cast<int> (lanes.size()))
    {
        dragStarted = true;
        startDrag (Part::All, lanes[static_cast<std::size_t> (pressedLane)].padIndex);
    }
}

void PatternView::startDrag (Part part, int padIndex)
{
    juce::String error;
    juce::MouseCursor::showWaitCursor();
    const auto file = processor.createDragFile (sequenceIndex, part, padIndex, error);
    juce::MouseCursor::hideWaitCursor();

    if (file == juce::File())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "PadPorter", error);
        return;
    }
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void PatternView::timerCallback()
{
    const bool playing = sequenceIndex >= 0 && processor.getPreviewSequence() == sequenceIndex;
    if (playing || playing != wasPlaying)
        repaint();
    wasPlaying = playing;
}
} // namespace padporter::plugin
