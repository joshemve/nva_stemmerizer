#pragma once

#include "Theme.h"
#include "../dsp/StemSession.h"
#include "../dsp/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace stemmerizer::ui
{

/// Single-track waveform display with click-to-seek and animated playhead.
///
/// Lifetime safety (audit C1):
///   The audio data we draw lives on `StemSession::Snapshot::Stem`. The
///   session can publish a fresh snapshot at any moment, and the prior
///   snapshot's buffers are freed when the last shared_ptr drops. To
///   make sure that doesn't happen mid-paint or mid-rebuild, this view
///   holds its OWN shared_ptr<const StemSession::Snapshot> + an index
///   into snap.stems. The pointer keeps the entire Snapshot (and thus
///   every stem buffer inside it) alive for as long as we reference it.
class WaveformStrip : public juce::Component, private juce::Timer
{
public:
    WaveformStrip (dsp::Transport& transport);
    ~WaveformStrip() override;

    /// Anchor this strip to one specific stem inside a snapshot. Pass
    /// snap=nullptr to clear. Re-pointing is safe; the prior snapshot's
    /// refcount drops only when the next snapshot is wired in (or this
    /// view is destroyed).
    void setStem (std::shared_ptr<const dsp::StemSession::Snapshot> snap,
                  int stemIndex);

    /// Convenience for the rare case where we want to display the
    /// original mix instead of a stem.
    void setOriginal (std::shared_ptr<const dsp::StemSession::Snapshot> snap);

    /// Drop any source. Pointer goes null, view shows placeholder text.
    void clearSource();

    void setColour (juce::Colour c) { col = c; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void rebuildPeaks();
    long long sampleAtX (int x) const;

    // Returns nullptr if there's no current source.
    const std::vector<float>* sourceBuffer() const noexcept;

    dsp::Transport& transport;
    juce::Colour    col { Theme::col (Theme::kAccent) };

    // Strong ref to the snapshot that owns our buffer. Either points to a
    // valid snapshot with sourceIdx in range, OR (when showingOriginal is
    // true) we read from snapPtr->original. Either way, the data is
    // anchored for our lifetime.
    std::shared_ptr<const dsp::StemSession::Snapshot> snapPtr;
    int  sourceIdx       { -1 };
    bool showingOriginal { false };

    // Cached peaks: one (min, max) pair per pixel column, rebuilt on
    // resize or source change.
    struct Peak { float lo { 0.f }; float hi { 0.f }; };
    std::vector<Peak> peaks;
    int               peaksWidth { 0 };
};

} // namespace stemmerizer::ui
