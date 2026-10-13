#pragma once

#include <padporter/Parts.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace padporter::plugin
{
class PadPorterProcessor;

/**
    The selected sequence split into parts: three chips (Drums, Melodie, Alles) that can be
    previewed and dragged into the host, and a lane view of all notes grouped by part.
    Sized for the device display; names and hints are shown as tooltips.
*/
class PatternView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit PatternView (PadPorterProcessor& processor);
    ~PatternView() override;

    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    enum class Group
    {
        Drums,
        Melody,
        Unassigned
    };

    struct NoteBlock
    {
        std::int64_t tick = 0;
        std::int64_t length = 0;
        float velocity = 1.0f;
        float semitones = 0.0f;     // "16 Levels" pitch offset
    };

    struct Lane
    {
        Group group = Group::Drums;
        int padIndex = -1;          // -1 for notes without a pad
        bool tuned = false;         // played with "16 Levels" pitch
        juce::String label;
        juce::String name;
        std::vector<NoteBlock> notes;
    };

    struct Chip
    {
        Part part = Part::All;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> playBounds;
        int count = 0;
    };

    void timerCallback() override;
    void layoutChips();
    juce::Colour colourFor (Part part) const;
    void paintChip (juce::Graphics&, const Chip&, int index) const;
    void paintLanes (juce::Graphics&, juce::Rectangle<int> area) const;
    void paintDragMode (juce::Graphics&) const;
    void startDrag (Part part, int padIndex);
    int laneAt (juce::Point<int> position) const;

    PadPorterProcessor& processor;
    std::array<Chip, 3> chips;
    std::vector<Lane> lanes;
    std::int64_t lengthTicks = 3840;
    int ticksPerBar = 3840;
    int ticksPerBeat = 960;
    int sequenceIndex = -1;

    juce::Rectangle<int> midiToggle, audioToggle;
    mutable std::vector<std::pair<juce::Rectangle<int>, int>> laneHitAreas;   // lane label -> lane index

    int hoverChip = -1;
    int pressedChip = -1;
    int hoverLane = -1;
    int pressedLane = -1;
    bool dragStarted = false;
    bool wasPlaying = false;
};
} // namespace padporter::plugin
