#pragma once

#include "Theme.h"
#include "../dsp/StemSession.h"
#include "../dsp/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace stemmerizer::ui
{

/// Single-track waveform render. Computes a peak summary on demand from
/// an interleaved buffer and draws it filled, with an animated playhead.
/// Click to seek, drag to scrub. Allocation-free during paint.
class WaveformStrip : public juce::Component, private juce::Timer
{
public:
    WaveformStrip (dsp::Transport& transport);
    ~WaveformStrip() override;

    /// Bind the strip to a stem buffer (or to the original mix). Pass
    /// nullptr to clear.
    void setSource (const float* interleaved,
                    long long numFrames,
                    int numChannels,
                    int sampleRate);

    void setColour (juce::Colour c) { col = c; repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void rebuildPeaks();
    long long sampleAtX (int x) const;

    dsp::Transport& transport;
    juce::Colour    col { Theme::col (Theme::kAccent) };

    // Source buffer (not owned).
    const float* src       { nullptr };
    long long    srcFrames { 0 };
    int          srcChans  { 2 };
    int          srcRate   { 44100 };

    // Cached peaks: one (min, max) pair per pixel column, recomputed on
    // resize / source change.
    struct Peak { float lo { 0.f }; float hi { 0.f }; };
    std::vector<Peak> peaks;
    int               peaksWidth { 0 };
};

} // namespace stemmerizer::ui
