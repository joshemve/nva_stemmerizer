#pragma once

#include "Theme.h"
#include "../dsp/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Horizontal scrub strip showing total length, current position, time
/// ticks, and (when loop is on) draggable start/end markers. Click to
/// seek; drag either edge to resize the loop region.
class LoopRegionView : public juce::Component, private juce::Timer
{
public:
    explicit LoopRegionView (dsp::Transport& t);
    ~LoopRegionView() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    long long sampleAtX (int x) const;
    void      drawTimeTicks (juce::Graphics&, const juce::Rectangle<float>& area) const;

    dsp::Transport& transport;

    enum class DragMode { None, Seek, ResizeStart, ResizeEnd, Move };
    DragMode mode { DragMode::None };
    long long anchorStart { 0 };
    long long anchorEnd   { 0 };
    int       anchorX     { 0 };
};

} // namespace stemmerizer::ui
