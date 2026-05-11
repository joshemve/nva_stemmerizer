#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Slim, animated horizontal progress bar (also draws indeterminate state).
/// Currently unused at the top level — JobList paints its own bars inline —
/// but exposed here for reuse by future UIs (e.g. the about screen).
class ProgressBar : public juce::Component, private juce::Timer
{
public:
    ProgressBar();
    ~ProgressBar() override;

    void setProgress (float fraction01);            // -1 = indeterminate
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    float target { 0.f };
    float shown  { 0.f };
    float phase  { 0.f };
    bool  indeterminate { false };
};

} // namespace stemmerizer::ui
