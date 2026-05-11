#pragma once

#include "Theme.h"
#include "IconButton.h"
#include "../dsp/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Play / pause / stop / loop buttons + time display + (if not provided
/// by an external LoopRegionView) a thin scrub bar.
class TransportBar : public juce::Component, private juce::Timer
{
public:
    explicit TransportBar (dsp::Transport& t);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::String formatTime (long long sample, int sampleRate) const;

    dsp::Transport& transport;

    IconButton playButton  { IconButton::Glyph::Play  };
    IconButton stopButton  { IconButton::Glyph::Stop  };
    IconButton loopButton  { IconButton::Glyph::Reload };

    juce::Label currentTime;
    juce::Label totalTime;
    juce::Label loopBadge;
};

} // namespace stemmerizer::ui
