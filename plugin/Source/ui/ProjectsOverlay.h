#pragma once

#include <padporter/DeviceLibrary.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace padporter::plugin
{
class PadPorterProcessor;

/** Rows with a title, a subtitle and an action button on the right. */
class ProjectList final : public juce::Component
{
public:
    struct Row
    {
        juce::String title;
        juce::String subtitle;
        juce::String action;
        juce::Colour actionColour;
        bool active = false;
        bool enabled = true;
    };

    static constexpr int kRowHeight = 56;

    void setRows (std::vector<Row> newRows, const juce::String& emptyMessage);
    int idealHeight() const noexcept;

    std::function<void (int row)> onAction;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    int rowAt (int y) const noexcept;
    juce::Rectangle<int> actionBounds (int row) const;

    std::vector<Row> rows;
    juce::String emptyText;
    int hoverRow = -1;
};

/** Overlay on the display: projects on the connected device (import) and the local library (open). */
class ProjectsOverlay final : public juce::Component
{
public:
    explicit ProjectsOverlay (PadPorterProcessor& processor);

    void refresh();
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PadPorterProcessor& processor;
    juce::TextButton closeButton, rescanButton, folderButton;
    juce::Viewport deviceView, libraryView;
    ProjectList deviceList, libraryList;
    std::vector<ProjectEntry> deviceEntries, libraryEntries;
    juce::String deviceStatus;
};
} // namespace padporter::plugin
