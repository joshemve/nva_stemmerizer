#pragma once

#include "Theme.h"
#include "StemRow.h"
#include "../dsp/StemSession.h"
#include "../dsp/Transport.h"
#include "../dsp/MusicAnalysis.h"
#include "../dsp/AudioFileIO.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>
#include <memory>
#include <optional>

namespace stemmerizer::ui
{

/// The new (post-v0.1) mixer: header (with A/B toggle on the right) + N
/// stem rows + a footer with two drag pills ("drag stems out" /
/// "drag mix out"). Replaces the older `StemMixer` class which was just
/// an export-toggle list.
class StemMixerPanel : public juce::Component,
                       public juce::SettableTooltipClient,
                       private juce::Timer
{
public:
    StemMixerPanel (dsp::StemSession& session, dsp::Transport& transport);
    ~StemMixerPanel() override;

    /// Recreate rows after a new session is loaded.
    void rebuild();

    /// Supplies the user's currently-selected export format. Set by the
    /// editor; if unset we fall back to WAV 24-bit.
    std::function<dsp::AudioFileIO::ExportFormat()> formatProvider;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void requestDragForStem (int idx);
    void requestDragAll();
    void requestDragMix();
    void timerCallback() override;
    void flashError (const juce::String& msg);
    dsp::AudioFileIO::ExportFormat currentFormat() const;

    dsp::StemSession& session;
    dsp::Transport&   transport;

    std::vector<std::unique_ptr<StemRow>> rows;

    // Footer drag pills.
    juce::Rectangle<int>  dragStemsBounds;
    juce::Rectangle<int>  dragMixBounds;

    // A/B pill — lives in the header right side now.
    juce::Rectangle<int>  abBounds;            // union of the two halves
    juce::Rectangle<int>  abStemsBounds;       // left half — "stems"
    juce::Rectangle<int>  abOriginalBounds;    // right half — "original"

    // Header BPM/key caption rect (squeezed left of the A/B pill).
    juce::Rectangle<int>  headerInfoBounds;

    // Rows area — used to overlay the "playing original" veil.
    juce::Rectangle<int>  rowsArea;

    // mouseDrag-armed state for the two footer pills. Set on mouseDown
    // over the pill, consumed on mouseDrag once the 6-px threshold is
    // crossed, and cleared on mouseUp so a plain click never fires a drag.
    bool dragStemsArmed { false };
    bool dragMixArmed   { false };

    // Transient "render failed: ..." status under the footer.
    juce::String lastDragError;
    juce::int64  lastDragErrorAtMs { 0 };

    // Optional musical analysis populated on rebuild(); shown in the header
    // right-side caption when present (e.g. "120 BPM · C minor").
    std::optional<dsp::BpmResult>  bpmInfo;
    std::optional<dsp::KeyResult>  keyInfo;
};

} // namespace stemmerizer::ui
