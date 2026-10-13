#include "PatternView.h"
#include "Theme.h"

#include "../PluginProcessor.h"

#include <padporter/PathUtil.h>

#include <map>

namespace padporter::plugin
{
namespace
{
constexpr int kTitleHeight = 34;
constexpr int kChipHeight = 48;
constexpr int kLabelWidth = 150;
constexpr int kRulerHeight = 16;
constexpr int kGroupHeaderHeight = 16;
constexpr int kFooterHeight = 18;

juce::String dot()
{
    return juce::String::fromUTF8 ("  ·  ");
}
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
    audioToggle = title.removeFromRight (62).withSizeKeepingCentre (62, 22);
    midiToggle = title.removeFromRight (54).withSizeKeepingCentre (54, 22);

    auto row = getLocalBounds().withTrimmedTop (kTitleHeight).withHeight (kChipHeight);
    const int gap = 10;
    const int width = (row.getWidth() - 2 * gap) / 3;
    for (auto& chip : chips)
    {
        chip.bounds = row.removeFromLeft (width);
        row.removeFromLeft (gap);
        chip.playBounds = chip.bounds.withWidth (chip.bounds.getHeight()).reduced (8);
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
    g.fillRoundedRectangle (bounds, 9.0f);
    g.setColour (colour.withAlpha (enabled ? (playing ? 1.0f : 0.55f) : 0.15f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 9.0f, playing ? 2.0f : 1.0f);

    // Play / stop button.
    const auto play = chip.playBounds.toFloat();
    g.setColour (enabled ? colour : colour.withAlpha (0.25f));
    g.fillEllipse (play);
    g.setColour (theme::lcd);
    if (playing)
        g.fillRect (play.withSizeKeepingCentre (play.getWidth() * 0.32f, play.getHeight() * 0.32f));
    else
    {
        juce::Path triangle;
        const auto c = play.getCentre();
        const auto s = play.getWidth() * 0.2f;
        triangle.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 1.1f, c.y);
        g.fillPath (triangle);
    }

    auto text = chip.bounds.withTrimmedLeft (chip.bounds.getHeight()).reduced (2, 7);
    auto grip = text.removeFromRight (44);

    g.setColour (enabled ? theme::screenText : theme::screenDim.withAlpha (0.5f));
    g.setFont (theme::font (14.0f, true));
    g.drawText (juce::String (partName (chip.part)).toUpperCase(), text.removeFromTop (text.getHeight() / 2), juce::Justification::bottomLeft);
    g.setColour (theme::screenDim);
    g.setFont (theme::font (11.0f));
    g.drawText (juce::String (chip.count) + " Noten", text, juce::Justification::topLeft);

    if (enabled)
    {
        // Drag handle: 2 x 3 dots + label.
        g.setColour (theme::screenDim);
        const auto gx = static_cast<float> (grip.getCentreX()) - 4.0f;
        const auto gy = static_cast<float> (grip.getY()) + 3.0f;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c)
                g.fillEllipse (gx + c * 6.0f, gy + r * 6.0f, 3.0f, 3.0f);
        g.setFont (theme::font (9.0f, true));
        g.drawText (processor.isDragAsAudio() ? "AUDIO" : "MIDI", grip.withTrimmedTop (20), juce::Justification::centredTop);
    }
}

void PatternView::paintLanes (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (lanes.empty())
    {
        g.setColour (theme::screenDim);
        g.setFont (theme::font (13.0f));
        g.drawText (sequenceIndex < 0 ? juce::String() : juce::String ("Diese Sequence ist leer."), area, juce::Justification::centred);
        return;
    }

    const auto ruler = area.removeFromTop (kRulerHeight);
    const int groupCount = [this] {
        int n = 0;
        Group last = Group::Unassigned;
        for (std::size_t i = 0; i < lanes.size(); ++i)
            if (i == 0 || lanes[i].group != last)
            {
                ++n;
                last = lanes[i].group;
            }
        return n;
    }();

    const float available = static_cast<float> (area.getHeight() - groupCount * kGroupHeaderHeight);
    const float laneHeight = juce::jlimit (6.0f, 22.0f, available / static_cast<float> (lanes.size()));

    const auto grid = area.withTrimmedLeft (kLabelWidth).toFloat();
    const auto toX = [&grid, this] (double tick) {
        return grid.getX() + static_cast<float> (tick / static_cast<double> (lengthTicks)) * grid.getWidth();
    };

    // Ruler with bar numbers.
    g.setFont (theme::font (9.5f, true));
    for (std::int64_t tick = 0, bar = 1; tick < lengthTicks; tick += ticksPerBar, ++bar)
    {
        const auto x = toX (static_cast<double> (tick));
        g.setColour (theme::screenDim);
        g.drawText (juce::String (bar), juce::Rectangle<float> (x + 3.0f, static_cast<float> (ruler.getY()), 30.0f, static_cast<float> (ruler.getHeight())),
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
    Group current = Group::Unassigned;
    bool first = true;

    for (const auto& lane : lanes)
    {
        const auto colour = lane.group == Group::Drums ? theme::drums : (lane.group == Group::Melody ? theme::melody : theme::unassigned);

        if (first || lane.group != current)
        {
            first = false;
            current = lane.group;
            const auto title = lane.group == Group::Drums ? "DRUMS" : (lane.group == Group::Melody ? "MELODIE" : "OHNE PAD");
            g.setColour (colour);
            g.setFont (theme::font (10.0f, true));
            g.drawText (title, juce::Rectangle<float> (static_cast<float> (area.getX()), y, static_cast<float> (kLabelWidth), static_cast<float> (kGroupHeaderHeight)),
                        juce::Justification::centredLeft);
            g.setColour (colour.withAlpha (0.35f));
            g.drawHorizontalLine (juce::roundToInt (y + kGroupHeaderHeight - 3.0f), grid.getX(), grid.getRight());
            y += static_cast<float> (kGroupHeaderHeight);
        }

        const auto row = juce::Rectangle<float> (static_cast<float> (area.getX()), y, static_cast<float> (area.getWidth()), laneHeight);
        const int laneIndex = static_cast<int> (&lane - lanes.data());

        if (lane.padIndex >= 0)
        {
            const auto hit = row.withWidth (static_cast<float> (kLabelWidth - 4)).toNearestInt();
            laneHitAreas.emplace_back (hit, laneIndex);
            if (laneIndex == hoverLane)
            {
                // Hover: the pad's lane can be dragged out as its own clip.
                g.setColour (colour.withAlpha (0.18f));
                g.fillRoundedRectangle (hit.toFloat(), 3.0f);
                g.setColour (colour);
                g.setFont (theme::font (8.5f, true));
                g.drawText (processor.isDragAsAudio() ? "AUDIO" : "MIDI", hit.toFloat().withTrimmedRight (4.0f), juce::Justification::centredRight);
            }
        }

        if (laneHeight >= 10.0f)
        {
            auto label = row.withWidth (static_cast<float> (kLabelWidth - (laneIndex == hoverLane ? 44 : 8)));
            g.setColour (theme::screenText.withAlpha (lane.group == Group::Unassigned ? 0.5f : 0.9f));
            g.setFont (theme::font (juce::jmin (11.0f, laneHeight - 1.0f), true));
            g.drawText (lane.label, label.removeFromLeft (34.0f), juce::Justification::centredLeft);
            g.setColour (theme::screenDim);
            g.setFont (theme::font (juce::jmin (10.5f, laneHeight - 1.0f)));
            g.drawText (lane.tuned ? lane.name + " (16 Lv)" : lane.name, label, juce::Justification::centredLeft, true);
        }

        for (const auto& n : lane.notes)
        {
            if (n.tick >= lengthTicks)
                continue;
            const auto x1 = toX (static_cast<double> (n.tick));
            const auto x2 = toX (static_cast<double> (juce::jmin (lengthTicks, n.tick + n.length)));
            const auto block = juce::Rectangle<float> (x1, row.getY() + 1.0f, juce::jmax (3.0f, x2 - x1), juce::jmax (2.0f, laneHeight - 2.0f));
            g.setColour (colour.withAlpha (0.35f + 0.65f * n.velocity));
            g.fillRoundedRectangle (block, juce::jmin (2.5f, laneHeight * 0.25f));

            // "16 Levels" notes show their pitch offset in semitones.
            if (lane.tuned && block.getWidth() >= 14.0f && laneHeight >= 10.0f)
            {
                const int st = juce::roundToInt (n.semitones);
                g.setColour (theme::lcd);
                g.setFont (theme::font (juce::jmin (10.0f, laneHeight - 2.0f), true));
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
        g.fillRect (x - 2.0f, grid.getY(), 4.0f, grid.getHeight());
        g.setColour (theme::waveform);
        g.drawVerticalLine (juce::roundToInt (x), grid.getY() - static_cast<float> (kRulerHeight), grid.getBottom());
    }
}

void PatternView::paintDragMode (juce::Graphics& g) const
{
    const bool audio = processor.isDragAsAudio();
    g.setColour (theme::screenDim);
    g.setFont (theme::font (9.5f, true));
    g.drawText ("ZIEHEN ALS", midiToggle.withX (midiToggle.getX() - 74).withWidth (70), juce::Justification::centredRight);

    for (const auto& [bounds, active, text] : { std::tuple { midiToggle, ! audio, "MIDI" }, std::tuple { audioToggle, audio, "AUDIO" } })
    {
        g.setColour (active ? theme::waveform : theme::lcdRaised);
        g.fillRoundedRectangle (bounds.toFloat().reduced (1.0f), 4.0f);
        g.setColour (active ? theme::lcd : theme::screenDim);
        g.setFont (theme::font (11.0f, true));
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
        g.setFont (theme::font (14.0f));
        g.drawFittedText (loaded == nullptr ? juce::String::fromUTF8 ("Hier erscheinen die Drums und Melodien deiner Sequences.")
                                            : juce::String::fromUTF8 ("Links eine Sequence auswählen."),
                          getLocalBounds().reduced (20), juce::Justification::centred, 3);
        return;
    }

    const auto& seq = loaded->project->sequences[static_cast<std::size_t> (sequenceIndex)];

    paintDragMode (g);

    auto title = getLocalBounds().removeFromTop (kTitleHeight).withTrimmedRight (200);
    g.setColour (theme::waveform);
    g.setFont (theme::font (18.0f, true));
    g.drawText (juce::String (seq.number()).paddedLeft ('0', 2), title.removeFromLeft (34), juce::Justification::centredLeft);
    g.setColour (theme::screenText);
    g.drawText (fromUtf8 (seq.name), title.removeFromLeft (260), juce::Justification::centredLeft, true);

    juce::String meta;
    meta << seq.lengthBars << (seq.lengthBars == 1 ? " Takt" : " Takte") << dot() << seq.timeSignature.beatsPerBar << "/"
         << seq.timeSignature.beatUnit << dot() << juce::String (loaded->project->tempoFor (seq), 1) << " BPM";
    g.setColour (theme::screenDim);
    g.setFont (theme::font (12.0f));
    g.drawText (meta, title, juce::Justification::centredRight);

    for (int i = 0; i < 3; ++i)
        paintChip (g, chips[static_cast<std::size_t> (i)], i);

    auto lanesArea = getLocalBounds().withTrimmedTop (kTitleHeight + kChipHeight + 12).withTrimmedBottom (kFooterHeight + 4);
    paintLanes (g, lanesArea);

    g.setColour (theme::screenDim.withAlpha (0.8f));
    g.setFont (theme::font (11.0f));
    g.drawText (processor.isDragAsAudio()
                    ? juce::String::fromUTF8 ("Chip oder Pad-Name in eine Audiospur in Ableton ziehen  ·  ▶ = hier anhören")
                    : juce::String::fromUTF8 ("Chip oder Pad-Name in eine MIDI-Spur mit PadPorter ziehen  ·  ▶ = hier anhören"),
                getLocalBounds().removeFromBottom (kFooterHeight), juce::Justification::centredLeft, true);
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
