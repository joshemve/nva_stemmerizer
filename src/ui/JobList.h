#pragma once

#include "Theme.h"
#include "../dsp/JobQueue.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Queue panel: header + scrollable list of jobs. Each row shows filename,
/// state, progress bar, and a context action (cancel for active jobs;
/// "reveal" for done jobs). Rendered with direct paint to keep allocations
/// down — no per-row child components.
class JobList : public juce::Component
{
public:
    JobList();
    ~JobList() override = default;

    void setQueue (dsp::JobQueue* q) { queue = q; refresh(); }
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

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
    };

    void rebuildSnapshot();

    dsp::JobQueue*        queue { nullptr };
    std::vector<RowState> snapshot;
    int hoverRow { -1 };
};

} // namespace stemmerizer::ui
