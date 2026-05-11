#pragma once

#include "Theme.h"
#include "StemRow.h"
#include "../dsp/StemSession.h"
#include "../dsp/Transport.h"
#include "../dsp/MusicAnalysis.h"
#include "../dsp/AudioFileIO.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdint>
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
    // Selection model.
    void onRowClicked (int idx, juce::ModifierKeys mods);
    void onRowDragRequested (int idx, juce::ModifierKeys mods);
    void applySelectionVisuals();
    void clearSelection();
    int  selectionCount() const noexcept;
    std::vector<int> selectionAsIndices() const;

    void requestDragForStem (int idx);
    void requestDragSelection();
    void requestDragAll();
    void requestDragMix();
    void timerCallback() override;
    void flashError (const juce::String& msg);
    dsp::AudioFileIO::ExportFormat currentFormat() const;

    dsp::StemSession& session;
    dsp::Transport&   transport;

    std::vector<std::unique_ptr<StemRow>> rows;

    // Multi-select state. `selected[i]` mirrors `rows[i]->isSelected()`;
    // `anchorRow` is the last solo-clicked / ctrl-clicked row, used as the
    // pivot for shift-range selection. Both reset on rebuild().
    std::vector<bool> selected;
    int               anchorRow { -1 };

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

    // Optional musical analysis populated AFTER rebuild() by a background
    // worker thread (audit N4). The header right-side caption shows
    // "120 BPM · C minor" once these land; before then we display the
    // stem-count fallback. Bumping `analysisVersion` on each rebuild
    // invalidates any in-flight analysis from the previous session so a
    // stale result can't paint over fresh content.
    std::optional<dsp::BpmResult>  bpmInfo;
    std::optional<dsp::KeyResult>  keyInfo;
    std::atomic<std::uint64_t>     analysisVersion { 0 };
};

} // namespace stemmerizer::ui
