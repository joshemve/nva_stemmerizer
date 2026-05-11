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

void WaveformStrip::setSource (const float* interleaved,
                               long long numFrames,
                               int numChannels,
                               int sampleRate)
{
    src       = interleaved;
    srcFrames = numFrames;
    srcChans  = std::max (1, numChannels);
    srcRate   = std::max (1, sampleRate);
    peaksWidth = -1;     // force rebuild
    repaint();
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
    if (src == nullptr || srcFrames <= 0 || w <= 0) return;

    const double per = (double) srcFrames / (double) w;
    for (int x = 0; x < w; ++x)
    {
        const long long s0 = (long long) std::floor (x * per);
        const long long s1 = std::min<long long> (srcFrames,
                                                  (long long) std::ceil ((x + 1) * per));
        float lo = 0.f, hi = 0.f;
        for (long long i = s0; i < s1; ++i)
        {
            // Take the louder of L/R for visualization (peak meter style).
            const float v0 = src[(size_t) (i * srcChans)];
            const float v1 = srcChans > 1 ? src[(size_t) (i * srcChans + 1)] : v0;
            const float v  = std::abs (v0) > std::abs (v1) ? v0 : v1;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        peaks[(size_t) x] = { lo, hi };
    }
}

long long WaveformStrip::sampleAtX (int x) const
{
    if (getWidth() <= 0 || srcFrames <= 0) return 0;
    const double frac = (double) x / (double) getWidth();
    return (long long) std::floor (juce::jlimit (0.0, 1.0, frac) * (double) srcFrames);
}

void WaveformStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (bounds, Theme::kRadiusSmall);

    if (peaksWidth != getWidth()) rebuildPeaks();

    if (peaks.empty() || src == nullptr)
    {
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::caption());
        g.drawText ("- no audio loaded -", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const float midY = bounds.getCentreY();
    const float halfH = bounds.getHeight() * 0.45f;

    g.setColour (col.withAlpha (0.85f));
    juce::Path p;
    for (int x = 0; x < (int) peaks.size(); ++x)
    {
        const auto& pk = peaks[(size_t) x];
        const float yHi = midY - pk.hi * halfH;
        const float yLo = midY - pk.lo * halfH;
        // One subpath per column so columns aren't connected diagonally.
        p.startNewSubPath ((float) x, yHi);
        p.lineTo ((float) x, yLo);
    }
    g.strokePath (p, juce::PathStrokeType (1.0f));

    // Loop region overlay
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
    transport.seek (sampleAtX (e.x));
}

void WaveformStrip::mouseDrag (const juce::MouseEvent& e)
{
    transport.seek (sampleAtX (e.x));
}

} // namespace stemmerizer::ui
