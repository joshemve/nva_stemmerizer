#pragma once

#include "Theme.h"
#include "../dsp/RecentProjects.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace stemmerizer::ui
{

/// Horizontal strip of "recent split" cards. One card per RecentEntry,
/// laid out left-to-right with equal width. Each card is a drag source for
/// the stem files on disk (no re-render, no temp file — straight paths).
///
/// Click body anywhere -> arms drag; passing the 6 px threshold fires
/// performExternalDragDropOfFiles with the entry's stemFiles. Click the
/// little × in the top-right of a card -> onRemoveRequested. Click the
/// "clear" link on the right edge -> onClearAll.
///
/// If a card's stems no longer exist on disk we dim it and skip the drag.
class RecentBar : public juce::Component
{
public:
    RecentBar();

    /// Replace the displayed entries. Triggers a layout + repaint.
    /// Any entry whose `inputPath` matches the cached current-session path
    /// (see setCurrentInputPath) is filtered out — the "now-loaded" project
    /// shouldn't double up as a recent card.
    void setEntries (std::vector<dsp::RecentEntry> e);

    /// Cache the currently-loaded session's source path so subsequent
    /// setEntries() calls can suppress it from the strip. Pass an empty
    /// string to clear the filter (no session loaded).
    void setCurrentInputPath (const juce::String& path);

    /// Snapshot for outside callers (mostly tests).
    const std::vector<dsp::RecentEntry>& getEntries() const noexcept { return entries; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /// Caller wires these. Both fire on the message thread (we're inside
    /// a mouse event).
    std::function<void(const juce::String& id)> onRemoveRequested;
    std::function<void()>                       onClearAll;

    /// Card metrics — exposed so the editor can size the bar.
    static constexpr int kCardH    = 64;
    static constexpr int kCardMinW = 160;

private:
    struct CardLayout
    {
        juce::String         id;
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> removeBounds;  // the × hit area inside the card
        bool                 stemsExist { true };
    };

    void rebuildLayout();
    int  cardIndexAt (juce::Point<int> p) const;
    bool isOverRemove (int cardIdx, juce::Point<int> p) const;

    std::vector<dsp::RecentEntry> entries;
    std::vector<CardLayout>       layout;
    juce::Rectangle<int>          clearBounds;     // the "clear" text link
    int hoverIdx { -1 };
    int armedIdx { -1 };
    juce::Point<int> pressPos;
    juce::String     currentInputPath;             // filtered out of setEntries()
};

} // namespace stemmerizer::ui
