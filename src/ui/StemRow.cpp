#include "StemRow.h"

#include <cmath>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kRowH       = 56;
    constexpr int kFaderW     = 110;
    constexpr int kSquareBtn  = 32;

    /// Logarithmic fader position math.
    ///   fader 0%   -> -inf dB (gain 0)
    ///   fader 10%  -> ~-60 dB
    ///   fader 60/66 (~90.9%) -> 0 dB (unity, gain 1.0)
    ///   fader 100% -> +6 dB
    inline float gainToFraction (float gainLinear)
    {
        if (gainLinear <= 0.0001f) return 0.f;
        const float db = 20.f * std::log10 (gainLinear);
        return juce::jlimit (0.f, 1.f, (db + 60.f) / 66.f);
    }
    inline float fractionToGain (float frac)
    {
        if (frac <= 0.001f) return 0.f;
        const float db = frac * 66.f - 60.f;
        return std::pow (10.f, db / 20.f);
    }
    inline juce::String dbLabel (float gain)
    {
        if (gain <= 0.0001f) return "-inf";
        const float db = 20.f * std::log10 (gain);
        return juce::String (db, 1) + " dB";
    }
}

StemRow::StemRow (dsp::StemSession& s, dsp::Transport& t, int stemIdx)
    : session (s), transport (t), idx (stemIdx), waveform (t)
{
    addAndMakeVisible (waveform);
    waveform.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
}

void StemRow::setStemIndex (int i)
{
    idx = i;
    waveform.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    refreshFromSession();
}

void StemRow::refreshFromSession()
{
    // Hand the WHOLE snapshot to the waveform — the strong shared_ptr
    // keeps every internal buffer alive for as long as the waveform
    // references it, even if the session swaps to a new snapshot.
    auto snap = session.currentSnapshot();
    if (snap && idx >= 0 && idx < (int) snap->stems.size())
        waveform.setStem (std::move (snap), idx);
    else
        waveform.clearSource();
    repaint();
}

void StemRow::resized()
{
    layOutControls();
    waveform.setBounds (waveBounds);
}

void StemRow::layOutControls()
{
    auto r = getLocalBounds().reduced (Theme::kPadSm, 4);
    dotBounds  = r.removeFromLeft (16).withSizeKeepingCentre (10, 10);
    r.removeFromLeft (Theme::kPadSm);

    nameBounds = r.removeFromLeft (78);
    r.removeFromLeft (Theme::kPadSm);

    muteBounds = r.removeFromLeft (kSquareBtn).withSizeKeepingCentre (kSquareBtn, kSquareBtn);
    r.removeFromLeft (4);
    soloBounds = r.removeFromLeft (kSquareBtn).withSizeKeepingCentre (kSquareBtn, kSquareBtn);
    r.removeFromLeft (Theme::kPadSm);

    dragBounds = r.removeFromRight (kSquareBtn).withSizeKeepingCentre (kSquareBtn, kSquareBtn);
    r.removeFromRight (Theme::kPadSm);

    dbBounds   = r.removeFromRight (60);
    r.removeFromRight (Theme::kPadSm);

    faderBounds = r.removeFromRight (kFaderW).withSizeKeepingCentre (kFaderW, 16);
    r.removeFromRight (Theme::kPadSm);

    waveBounds = r;
}

void StemRow::paint (juce::Graphics& g)
{
    auto& slot = session.mixState().slot (idx);
    const auto color = Theme::stemColor (idx, session.mixState().stemCount());
    const bool muted  = slot.muted.load();
    const bool soloed = slot.soloed.load();

    // Background row
    g.setColour (Theme::col (Theme::kBackground));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.f), Theme::kRadiusMedium);

    // Color dot
    g.setColour (color.withAlpha (muted ? 0.4f : 1.f));
    g.fillEllipse (dotBounds.toFloat());

    // Name
    g.setColour (muted ? Theme::col (Theme::kTextDisabled) : Theme::col (Theme::kTextPrimary));
    g.setFont (Theme::body());
    auto snap = session.currentSnapshot();
    juce::String name = "stem";
    if (snap && idx < (int) snap->stems.size())
        name = juce::String (snap->stems[(size_t) idx].name).toUpperCase();
    g.drawText (name, nameBounds, juce::Justification::centredLeft);

    drawMuteSolo (g);
    drawFader (g);

    // dB label
    g.setColour (Theme::col (Theme::kTextSecondary));
    g.setFont (Theme::mono());
    g.drawText (dbLabel (slot.gain.load()), dbBounds, juce::Justification::centredRight);

    drawDragHandle (g);
}

void StemRow::drawMuteSolo (juce::Graphics& g)
{
    auto& slot = session.mixState().slot (idx);
    const bool muted  = slot.muted.load();
    const bool soloed = slot.soloed.load();

    // M — neutral gray when muted (red is reserved for destructive actions)
    g.setColour (muted ? Theme::col (Theme::kTextSecondary)
                       : Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (muteBounds.toFloat(), 6.f);
    g.setColour (muted ? Theme::col (Theme::kTextSecondary) : Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (muteBounds.toFloat(), 6.f, 1.f);
    g.setColour (muted ? juce::Colours::white : Theme::col (Theme::kTextSecondary));
    g.setFont (Theme::caption().boldened());
    g.drawText ("M", muteBounds, juce::Justification::centred);

    // S
    g.setColour (soloed ? Theme::col (Theme::kAccent).withAlpha (0.85f)
                        : Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (soloBounds.toFloat(), 6.f);
    g.setColour (soloed ? Theme::col (Theme::kAccent) : Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (soloBounds.toFloat(), 6.f, 1.f);
    g.setColour (soloed ? juce::Colours::white : Theme::col (Theme::kTextSecondary));
    g.drawText ("S", soloBounds, juce::Justification::centred);
}

void StemRow::drawFader (juce::Graphics& g)
{
    auto& slot = session.mixState().slot (idx);
    const float gain = slot.gain.load();
    const float frac = gainToFraction (gain);

    const auto track = faderBounds.toFloat();
    const float midY = track.getCentreY();
    const float trackH = 4.f;
    juce::Rectangle<float> trackR (track.getX(), midY - trackH * 0.5f,
                                   track.getWidth(), trackH);

    g.setColour (Theme::col (Theme::kBorderStrong));
    g.fillRoundedRectangle (trackR, trackH * 0.5f);

    // Unity tick at frac=60/66 (where log-scale gain = 1.0)
    const float unityFrac = 60.f / 66.f;
    const float unityX = track.getX() + track.getWidth() * unityFrac;
    g.setColour (Theme::col (Theme::kBorder));
    g.drawLine (unityX, track.getY() + 2, unityX, track.getBottom() - 2, 1.f);

    // Filled track
    const float fillW = track.getWidth() * frac;
    g.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    g.fillRoundedRectangle ({ track.getX(), midY - trackH * 0.5f, fillW, trackH },
                            trackH * 0.5f);

    // Thumb
    const float thumbX = track.getX() + fillW;
    const float thumbR = 9.f;
    g.setColour (juce::Colours::white);
    g.fillEllipse (thumbX - thumbR, midY - thumbR, thumbR * 2, thumbR * 2);
    g.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    g.fillEllipse (thumbX - thumbR + 2, midY - thumbR + 2,
                   (thumbR - 2) * 2, (thumbR - 2) * 2);
}

void StemRow::drawDragHandle (juce::Graphics& g)
{
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (dragBounds.toFloat(), 6.f);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (dragBounds.toFloat(), 6.f, 1.f);

    g.setColour (Theme::col (Theme::kTextSecondary));
    const float cx = dragBounds.toFloat().getCentreX();
    const float cy = dragBounds.toFloat().getCentreY();
    // Six-dot grip glyph (vertical 2x3)
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 2; ++i)
            g.fillEllipse (cx - 3 + i * 4 - 1, cy - 5 + j * 4 - 1, 2, 2);
}

void StemRow::mouseDown (const juce::MouseEvent& e)
{
    auto& slot = session.mixState().slot (idx);
    const auto pos = e.getPosition();

    if (muteBounds.contains (pos))
    {
        slot.muted.store (! slot.muted.load());
        repaint(); return;
    }
    if (soloBounds.contains (pos))
    {
        slot.soloed.store (! slot.soloed.load());
        repaint(); return;
    }
    if (faderBounds.contains (pos))
    {
        draggingFader = true;
        const float frac = (float) (pos.x - faderBounds.getX()) / (float) faderBounds.getWidth();
        slot.gain.store (fractionToGain (frac));
        repaint(); return;
    }
    if (dragBounds.contains (pos))
    {
        dragArmed = true;
        return;
    }
}

void StemRow::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingFader)
    {
        auto& slot = session.mixState().slot (idx);
        const float frac = (float) (e.x - faderBounds.getX()) / (float) faderBounds.getWidth();
        slot.gain.store (fractionToGain (frac));
        repaint();
        return;
    }
    if (dragArmed && e.getDistanceFromDragStart() > 6)
    {
        dragArmed = false;
        if (onDragRequested) onDragRequested (idx);
        return;
    }
}

void StemRow::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (faderBounds.contains (e.getPosition()))
    {
        auto& slot = session.mixState().slot (idx);
        slot.gain.store (1.0f);
        repaint();
        return;
    }
}

void StemRow::mouseWheelMove (const juce::MouseEvent& e,
                              const juce::MouseWheelDetails& wheel)
{
    if (! faderBounds.contains (e.getPosition())) return;
    if (wheel.deltaY == 0.f) return;

    auto& slot = session.mixState().slot (idx);
    const float currentGain = slot.gain.load();
    // Treat -inf as -60 dB for stepping purposes so the wheel can pull
    // the gain back out of "off".
    const float currentDb = currentGain <= 0.0001f
                                ? -60.f
                                : 20.f * std::log10 (currentGain);
    const float step = wheel.deltaY > 0.f ? 1.f : -1.f;
    const float newDb = juce::jlimit (-60.f, 6.f, currentDb + step);
    const float newGain = newDb <= -60.f + 0.0001f ? 0.f
                                                   : std::pow (10.f, newDb / 20.f);
    slot.gain.store (newGain);
    repaint();
}

void StemRow::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (muteBounds.contains (pos) || soloBounds.contains (pos) || dragBounds.contains (pos))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (faderBounds.contains (pos))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (waveBounds.contains (pos))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

} // namespace stemmerizer::ui
