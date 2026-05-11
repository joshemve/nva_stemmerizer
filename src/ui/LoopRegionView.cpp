#include "LoopRegionView.h"

#include <algorithm>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kEdgeGrabPx = 8;
}

LoopRegionView::LoopRegionView (dsp::Transport& t) : transport (t)
{
    startTimerHz (30);
    setMouseCursor (juce::MouseCursor::IBeamCursor);
}

LoopRegionView::~LoopRegionView() = default;

void LoopRegionView::timerCallback() { repaint(); }

long long LoopRegionView::sampleAtX (int x) const
{
    if (getWidth() <= 0 || transport.length() <= 0) return 0;
    const double frac = juce::jlimit (0.0, 1.0, (double) x / (double) getWidth());
    return (long long) (frac * (double) transport.length());
}

void LoopRegionView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.f);
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (bounds, Theme::kRadiusSmall);

    const long long len = transport.length();
    if (len <= 0)
    {
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::caption());
        g.drawText ("- timeline -", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Time ticks underneath everything. They give the strip enough visual
    // density to feel like a real scrub bar instead of a 2-px bug.
    drawTimeTicks (g, bounds);

    const float w = bounds.getWidth();
    const float xS = (float) transport.loopStart() / (float) len * w + bounds.getX();
    const float xE = (float) transport.loopEnd()   / (float) len * w + bounds.getX();
    const float xP = (float) transport.position()  / (float) len * w + bounds.getX();

    if (transport.isLoopOn())
    {
        g.setColour (Theme::col (Theme::kAccent).withAlpha (0.18f));
        g.fillRect (xS, bounds.getY(), xE - xS, bounds.getHeight());

        g.setColour (Theme::col (Theme::kAccent));
        g.fillRect (xS - 1, bounds.getY(), 2.f, bounds.getHeight());
        g.fillRect (xE - 1, bounds.getY(), 2.f, bounds.getHeight());

        // Edge grip ticks — two small dots stacked vertically on each handle,
        // to advertise that the edges are draggable. Drawn slightly inset
        // from the edge bar so they're visible against the accent line.
        const float cy   = bounds.getCentreY();
        const float dotR = 1.5f;
        const float dotOff = 4.f;
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        for (float dy : { -dotOff, dotOff })
        {
            g.fillEllipse (xS - dotR, cy + dy - dotR, dotR * 2.f, dotR * 2.f);
            g.fillEllipse (xE - dotR, cy + dy - dotR, dotR * 2.f, dotR * 2.f);
        }
    }

    // Playhead
    g.setColour (Theme::col (Theme::kTextPrimary));
    g.fillRect (xP - 0.5f, bounds.getY(), 1.f, bounds.getHeight());
}

void LoopRegionView::mouseMove (const juce::MouseEvent& e)
{
    const long long len = transport.length();
    if (len <= 0)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    const float w  = (float) getWidth();
    const float xS = (float) transport.loopStart() / (float) len * w;
    const float xE = (float) transport.loopEnd()   / (float) len * w;

    if (transport.isLoopOn() &&
        (std::abs (e.x - (int) xS) <= kEdgeGrabPx
         || std::abs (e.x - (int) xE) <= kEdgeGrabPx))
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }
    else if (transport.isLoopOn() && e.x > (int) xS && e.x < (int) xE)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    else
    {
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    }
}

void LoopRegionView::mouseDown (const juce::MouseEvent& e)
{
    const long long len = transport.length();
    if (len <= 0) { mode = DragMode::None; return; }

    const float w = (float) getWidth();
    const float xS = (float) transport.loopStart() / (float) len * w;
    const float xE = (float) transport.loopEnd()   / (float) len * w;

    anchorStart = transport.loopStart();
    anchorEnd   = transport.loopEnd();
    anchorX     = e.x;

    if (transport.isLoopOn() && std::abs (e.x - (int) xS) <= kEdgeGrabPx)      mode = DragMode::ResizeStart;
    else if (transport.isLoopOn() && std::abs (e.x - (int) xE) <= kEdgeGrabPx) mode = DragMode::ResizeEnd;
    else if (transport.isLoopOn() && e.x > (int) xS && e.x < (int) xE)         mode = DragMode::Move;
    else                                                                       mode = DragMode::Seek;

    if (mode == DragMode::Seek) transport.seek (sampleAtX (e.x));
}

void LoopRegionView::mouseDrag (const juce::MouseEvent& e)
{
    const long long len = transport.length();
    if (len <= 0) return;

    if (mode == DragMode::Seek)
    {
        transport.seek (sampleAtX (e.x));
    }
    else if (mode == DragMode::ResizeStart)
    {
        long long s = sampleAtX (e.x);
        s = std::min (s, transport.loopEnd() - 1);
        transport.setLoopRegion (s, transport.loopEnd());
    }
    else if (mode == DragMode::ResizeEnd)
    {
        long long e1 = sampleAtX (e.x);
        e1 = std::max (e1, transport.loopStart() + 1);
        transport.setLoopRegion (transport.loopStart(), e1);
    }
    else if (mode == DragMode::Move)
    {
        const long long delta = sampleAtX (e.x) - sampleAtX (anchorX);
        long long s = anchorStart + delta;
        long long en = anchorEnd  + delta;
        const long long span = anchorEnd - anchorStart;
        if (s < 0)        { s = 0; en = span; }
        if (en > len)     { en = len; s = len - span; }
        transport.setLoopRegion (s, en);
    }
}

void LoopRegionView::mouseUp (const juce::MouseEvent&)
{
    mode = DragMode::None;
}

void LoopRegionView::drawTimeTicks (juce::Graphics& g,
                                    const juce::Rectangle<float>& area) const
{
    // Pick a tick spacing that yields ~6-10 majors across the strip so it
    // never gets visually busy. Track length is unknown at compile time so
    // we round to a nice musical/clock interval (1s/2s/5s/10s/30s/60s/...).
    const int sr = std::max (1, transport.sampleRate());
    const double totalSec = (double) transport.length() / (double) sr;
    if (totalSec <= 0.0) return;

    const float w = area.getWidth();
    constexpr int kIdealMajors = 8;

    static const double kSteps[] = {
        1, 2, 5, 10, 15, 30, 60, 120, 300, 600
    };
    double step = kSteps[0];
    for (double cand : kSteps)
    {
        step = cand;
        if (totalSec / cand <= kIdealMajors) break;
    }

    const float pxPerSec = w / (float) totalSec;
    const float tickAlpha = 0.35f;

    g.setColour (Theme::col (Theme::kTextTertiary).withAlpha (tickAlpha));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));

    // Minor ticks every `step/5` seconds when the panel is wide enough to
    // show them without crowding.
    const double minorStep = step / 5.0;
    if (minorStep * pxPerSec >= 4.0)
    {
        for (double s = minorStep; s < totalSec; s += minorStep)
        {
            const float x = area.getX() + (float) s * pxPerSec;
            g.fillRect (x, area.getBottom() - 4.f, 1.f, 3.f);
        }
    }

    // Major ticks + caption.
    g.setColour (Theme::col (Theme::kTextTertiary).withAlpha (0.7f));
    for (double s = step; s < totalSec; s += step)
    {
        const float x = area.getX() + (float) s * pxPerSec;
        g.fillRect (x, area.getBottom() - 7.f, 1.f, 6.f);

        const int  mins = (int) (s / 60.0);
        const int  secs = (int) s - mins * 60;
        const auto label = juce::String::formatted ("%d:%02d", mins, secs);
        const juce::Rectangle<float> capR (x - 18.f, area.getY() + 1.f, 36.f, 11.f);
        g.drawText (label, capR.toNearestInt(), juce::Justification::centred, false);
    }
}

} // namespace stemmerizer::ui
