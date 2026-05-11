#pragma once

#include "Theme.h"
#include "../dsp/JobQueue.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Queue panel: header + scrollable list of jobs. Each row shows filename,
/// state, progress bar, and a context action (cancel for active jobs;
/// "reveal" / "dismiss" for finished jobs). Rendered with direct paint to
/// keep allocations down — no per-row child components.
class JobList : public juce::Component, public juce::SettableTooltipClient
{
public:
    JobList();
    ~JobList() override = default;

    void setQueue (dsp::JobQueue* q) { queue = q; refresh(); }
    void refresh();

    /// Number of jobs currently Queued or Running (i.e. visible to the
    /// user as "something is happening"). Used by the editor to decide
    /// whether to show the full queue panel, a slim 1-row strip, or
    /// nothing at all.
    int activeJobCount() const noexcept;

    /// Total job count regardless of state (Queued / Running / Done /
    /// Failed / Cancelled). Hides the panel completely when 0.
    int totalJobCount() const noexcept { return (int) snapshot.size(); }

    /// "Compact" mode: render a single slim progress row (no header /
    /// no scroll). Used when totalJobCount() == 1.
    void setCompactMode (bool compact);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct RowState
    {
        int                  id { 0 };
        juce::String         filename;
        juce::String         outputDir;
        juce::String         stage;
        juce::String         elapsed;
        dsp::Job::State      state { dsp::Job::State::Queued };
        float                progress { 0.f };
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> actionBounds;
        juce::Rectangle<int> secondaryActionBounds; // empty when unused
    };

    void rebuildSnapshot();
    void clampScroll();

    dsp::JobQueue*        queue { nullptr };
    std::vector<RowState> snapshot;
    int  hoverRow { -1 };
    int  scrollY  { 0 };
    bool compactMode { false };
};

} // namespace stemmerizer::ui
