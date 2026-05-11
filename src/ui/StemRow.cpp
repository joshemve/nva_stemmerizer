#include "StemRow.h"

#include <cmath>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kRowH       = 56;
    constexpr int kFaderW     = 110;
    constexpr int kSquareBtn  = 26;

    /// Fader position math. Linear gain in [0..2] mapped to a vertical-ish
    /// horizontal slider where 0 = -inf, 1 = unity, 2 = +6 dB.
    inline float gainToFraction (float gainLinear)
    {
        return juce::jlimit (0.f, 1.f, gainLinear * 0.5f);
    }
    inline float fractionToGain (float frac)
    {
        return juce::jlimit (0.f, 2.f, frac * 2.f);
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
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StemRow::setStemIndex (int i)
{
    idx = i;
    waveform.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    refreshFromSession();
}

void StemRow::refreshFromSession()
{
    auto snap = session.currentSnapshot();
    if (snap && idx >= 0 && idx < (int) snap->stems.size())
    {
        const auto& s = snap->stems[(size_t) idx];
        waveform.setSource (s.interleaved.data(),
                            (long long) s.interleaved.size() / std::max (1, s.numChannels),
                            s.numChannels,
                            snap->sampleRate);
    }
    else
    {
        waveform.setSource (nullptr, 0, 0, 0);
    }
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

    // M
    g.setColour (muted ? Theme::col (Theme::kError).withAlpha (0.85f)
                       : Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (muteBounds.toFloat(), 6.f);
    g.setColour (muted ? Theme::col (Theme::kError) : Theme::col (Theme::kBorder));
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

    // Unity tick at frac=0.5
    const float unityX = track.getX() + track.getWidth() * 0.5f;
    g.setColour (Theme::col (Theme::kBorder));
    g.drawLine (unityX, track.getY() + 2, unityX, track.getBottom() - 2, 1.f);

    // Filled track
    const float fillW = track.getWidth() * frac;
    g.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    g.fillRoundedRectangle ({ track.getX(), midY - trackH * 0.5f, fillW, trackH },
                            trackH * 0.5f);

    // Thumb
    const float thumbX = track.getX() + fillW;
    const float thumbR = 7.f;
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

} // namespace stemmerizer::ui
