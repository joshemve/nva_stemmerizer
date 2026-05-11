#pragma once

#include "Theme.h"
#include "WaveformStrip.h"
#include "../dsp/StemSession.h"
#include "../dsp/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace stemmerizer::ui
{

/// One stem's row inside the StemMixerPanel: color dot + name + waveform
/// + mute/solo + volume fader + drag handle.
///
/// All controls bind to the StemMixState slot at `stemIndex`. Drag-out
/// uses the parent's "request drag" callback so we don't reach into the
/// session from here.
class StemRow : public juce::Component
{
public:
    StemRow (dsp::StemSession& session,
             dsp::Transport&   transport,
             int               stemIndex);
    ~StemRow() override = default;

    void setStemIndex (int idx);
    int  stemIndex() const noexcept { return idx; }

    /// Refresh from the session (call when a new file is loaded).
    void refreshFromSession();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    /// Parent supplies this to perform the actual OS drag-and-drop.
    std::function<void(int)> onDragRequested;

private:
    void layOutControls();
    void drawFader (juce::Graphics&);
    void drawMuteSolo (juce::Graphics&);
    void drawDragHandle (juce::Graphics&);

    dsp::StemSession& session;
    dsp::Transport&   transport;
    int               idx { 0 };

    WaveformStrip     waveform;

    juce::Rectangle<int> dotBounds;
    juce::Rectangle<int> nameBounds;
    juce::Rectangle<int> muteBounds;
    juce::Rectangle<int> soloBounds;
    juce::Rectangle<int> faderBounds;
    juce::Rectangle<int> waveBounds;
    juce::Rectangle<int> dragBounds;
    juce::Rectangle<int> dbBounds;

    bool draggingFader { false };
    bool dragArmed     { false };
};

} // namespace stemmerizer::ui
