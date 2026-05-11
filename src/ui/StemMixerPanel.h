#pragma once

#include "Theme.h"
#include "StemRow.h"
#include "../dsp/StemSession.h"
#include "../dsp/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>
#include <memory>

namespace stemmerizer::ui
{

/// The new (post-v0.1) mixer: header + N stem rows + a footer with
/// "drag all stems out" and a segmented "STEMS / ORIGINAL" A/B toggle.
/// Replaces the older `StemMixer` class which was just an export-toggle
/// list.
class StemMixerPanel : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    StemMixerPanel (dsp::StemSession& session, dsp::Transport& transport);
    ~StemMixerPanel() override = default;

    /// Recreate rows after a new session is loaded.
    void rebuild();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void requestDragForStem (int idx);
    void requestDragAll();

    dsp::StemSession& session;
    dsp::Transport&   transport;

    std::vector<std::unique_ptr<StemRow>> rows;

    juce::Rectangle<int>  dragAllBounds;
    juce::Rectangle<int>  abBounds;            // union of the two halves
    juce::Rectangle<int>  abStemsBounds;       // left half — "STEMS"
    juce::Rectangle<int>  abOriginalBounds;    // right half — "ORIGINAL"
};

} // namespace stemmerizer::ui
