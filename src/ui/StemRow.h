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
/// + mute/solo + volume fader.
///
/// All controls bind to the StemMixState slot at `stemIndex`. Selection
/// + drag-out are handled by the parent panel: the row reports clicks and
/// drag-starts through callbacks so the panel can apply multi-select
/// semantics (plain / ctrl / shift) and drag the right set of stems.
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

    /// Selection — purely visual; the parent panel owns the underlying
    /// selection model and tells each row whether to render selected.
    void setSelected (bool s) noexcept;
    bool isSelected() const noexcept { return selected; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

    /// Parent supplies these to perform selection + OS drag-and-drop. The
    /// row never reaches into the session for those — keeps multi-select
    /// state centralized.
    std::function<void(int /*idx*/, juce::ModifierKeys)> onRowClicked;
    std::function<void(int /*idx*/, juce::ModifierKeys)> onDragRequested;

private:
    void layOutControls();
    void drawFader (juce::Graphics&);
    void drawMuteSolo (juce::Graphics&);

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
    juce::Rectangle<int> dbBounds;

    bool selected      { false };
    bool draggingFader { false };

    // Row-body drag arming (the name/dot area outside of M/S/fader). Lets
    // the user grab a row anywhere — not just on the waveform — and drag
    // it out into the DAW.
    bool                pressArmed { false };
    juce::ModifierKeys  pressMods  {};
    static constexpr int kDragOutPx { 6 };
};

} // namespace stemmerizer::ui
