#pragma once

#include "Theme.h"
#include "IconButton.h"
#include "../dsp/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Play / pause / stop / loop buttons + a single combined time display
/// (position / length). The loop region itself is drawn by LoopRegionView,
/// so this bar stays compact.
class TransportBar : public juce::Component, private juce::Timer
{
public:
    explicit TransportBar (dsp::Transport& t);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Toggle the loop state and update the loop button's active highlight.
    // Exposed so the editor can wire a keyboard shortcut without depending
    // on the internal button.
    void toggleLoop();

    // Pulse the play/pause toggle from outside (keyboard shortcut).
    void togglePlay();

private:
    void timerCallback() override;
    juce::String formatTime (long long sample, int sampleRate) const;

    dsp::Transport& transport;

    IconButton playButton  { IconButton::Glyph::Play  };
    IconButton stopButton  { IconButton::Glyph::Stop  };
    IconButton loopButton  { IconButton::Glyph::Reload };

    juce::Label timeLabel;
};

} // namespace stemmerizer::ui
