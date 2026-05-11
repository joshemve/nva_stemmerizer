#include "WaveformStrip.h"

#include <algorithm>
#include <cmath>

namespace stemmerizer::ui
{

WaveformStrip::WaveformStrip (dsp::Transport& t) : transport (t)
{
    startTimerHz (30);
}

WaveformStrip::~WaveformStrip() = default;

void WaveformStrip::setStem (std::shared_ptr<const dsp::StemSession::Snapshot> snap,
                             int stemIndex)
{
    snapPtr          = std::move (snap);
    sourceIdx        = stemIndex;
    showingOriginal  = false;
    peaksWidth       = -1;     // force peak rebuild on next paint
    repaint();
}

void WaveformStrip::setOriginal (std::shared_ptr<const dsp::StemSession::Snapshot> snap)
{
    snapPtr          = std::move (snap);
    sourceIdx        = -1;
    showingOriginal  = true;
    peaksWidth       = -1;
    repaint();
}

void WaveformStrip::clearSource()
{
    snapPtr.reset();
    sourceIdx        = -1;
    showingOriginal  = false;
    peaksWidth       = -1;
    peaks.clear();
    repaint();
}

const std::vector<float>* WaveformStrip::sourceBuffer() const noexcept
{
    if (! snapPtr) return nullptr;
    if (showingOriginal) return snapPtr->original.empty() ? nullptr : &snapPtr->original;
    if (sourceIdx < 0 || sourceIdx >= (int) snapPtr->stems.size()) return nullptr;
    const auto& s = snapPtr->stems[(size_t) sourceIdx];
    return s.interleaved.empty() ? nullptr : &s.interleaved;
}

void WaveformStrip::resized()
{
    rebuildPeaks();
}

void WaveformStrip::rebuildPeaks()
{
    const int w = std::max (0, getWidth());
    if (w == peaksWidth) return;
    peaksWidth = w;
    peaks.assign ((size_t) w, {});

    const auto* buf = sourceBuffer();
    if (buf == nullptr || w <= 0 || ! snapPtr) return;

    const int srcChans = showingOriginal ? snapPtr->numChannels
                                         : snapPtr->stems[(size_t) sourceIdx].numChannels;
    const long long srcFrames = (long long) buf->size() / std::max (1, srcChans);
    if (srcFrames <= 0) return;

    const double per = (double) srcFrames / (double) w;
    for (int x = 0; x < w; ++x)
    {
        const long long s0 = (long long) std::floor (x * per);
        const long long s1 = std::min<long long> (srcFrames,
                                                  (long long) std::ceil ((x + 1) * per));
        float lo = 0.f, hi = 0.f;
        for (long long i = s0; i < s1; ++i)
        {
            const float v0 = (*buf)[(size_t) (i * srcChans)];
            const float v1 = srcChans > 1 ? (*buf)[(size_t) (i * srcChans + 1)] : v0;
            const float v  = std::abs (v0) > std::abs (v1) ? v0 : v1;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

long long WaveformStrip::sampleAtX (int x) const
{
    if (! snapPtr || getWidth() <= 0) return 0;
    const long long srcFrames = snapPtr->numFrames;
    if (srcFrames <= 0) return 0;
    const double frac = (double) x / (double) getWidth();
    return (long long) std::floor (juce::jlimit (0.0, 1.0, frac) * (double) srcFrames);
}

void WaveformStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (bounds, Theme::kRadiusSmall);

    if (peaksWidth != getWidth()) rebuildPeaks();

    const auto* buf = sourceBuffer();
    if (peaks.empty() || buf == nullptr)
    {
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::caption());
        g.drawText ("- no audio loaded -", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const float midY  = bounds.getCentreY();
    const float halfH = bounds.getHeight() * 0.45f;

    g.setColour (col.withAlpha (0.85f));
    juce::Path p;
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const auto& pk = peaks[(size_t) x];
        const float yHi = midY - pk.hi * halfH;
        const float yLo = midY - pk.lo * halfH;
        if (x == 0) p.startNewSubPath ((float) x, yLo);
        p.lineTo ((float) x, yHi);
        p.lineTo ((float) x, yLo);
    }
    g.strokePath (p, juce::PathStrokeType (1.0f));

    // Loop region overlay (transport-driven; safe to read atomically)
    if (transport.isLoopOn() && transport.length() > 0)
    {
        const float xS = (float) transport.loopStart() / (float) transport.length() * bounds.getWidth();
        const float xE = (float) transport.loopEnd()   / (float) transport.length() * bounds.getWidth();
        g.setColour (Theme::col (Theme::kAccent).withAlpha (0.10f));
        g.fillRect (xS, 0.f, xE - xS, bounds.getHeight());
        g.setColour (Theme::col (Theme::kAccent).withAlpha (0.6f));
        g.drawLine (xS, 0.f, xS, bounds.getHeight(), 1.f);
        g.drawLine (xE, 0.f, xE, bounds.getHeight(), 1.f);
    }

    // Playhead
    if (transport.length() > 0)
    {
        const float xP = (float) transport.position() / (float) transport.length() * bounds.getWidth();
        g.setColour (Theme::col (Theme::kTextPrimary));
        g.drawLine (xP, 0.f, xP, bounds.getHeight(), 1.5f);
    }
}

void WaveformStrip::timerCallback() { repaint(); }

void WaveformStrip::mouseDown (const juce::MouseEvent& e)
{
    // Don't seek immediately — we don't know yet whether this is a click
    // (seek) or the start of a drag-out (file drag). mouseUp / mouseDrag
    // settle it. When no drag-out callback is wired we fall back to the
    // old behaviour (seek on press).
    pressArmed = true;
    pressMods  = e.mods;
    if (! onDragOutRequested) transport.seek (sampleAtX (e.x));
}

void WaveformStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressArmed) return;
    if (onDragOutRequested && e.getDistanceFromDragStart() > kDragOutPx)
    {
        pressArmed = false;
        onDragOutRequested (pressMods);
        return;
    }
    // Legacy scrub when no drag-out handler is wired — preserved for any
    // future caller that wants a plain seek waveform.
    if (! onDragOutRequested) transport.seek (sampleAtX (e.x));
}

void WaveformStrip::mouseUp (const juce::MouseEvent& e)
{
    if (! pressArmed) return;
    pressArmed = false;

    // Treat a low-distance press+release as a click. Plain click → seek
    // and announce the click (for selection). Modifier-click → just the
    // selection signal, no seek (lets users multi-select without
    // moving the playhead).
    const bool isClick = e.getDistanceFromDragStart() <= kDragOutPx;
    if (! isClick) return;

    const bool hasModifier = pressMods.isShiftDown() || pressMods.isCtrlDown()
                          || pressMods.isCommandDown();
    if (! hasModifier) transport.seek (sampleAtX (e.x));
    if (onClicked) onClicked (pressMods);
}

void WaveformStrip::mouseMove (const juce::MouseEvent&)
{
    if (onDragOutRequested && ! hovering)
    {
        hovering = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
}

void WaveformStrip::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

} // namespace stemmerizer::ui
