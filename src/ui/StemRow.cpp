#include "StemRow.h"

#include <cmath>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kRowH       = 56;
    // Was 110 — far too narrow once the stems panel became the hero. A
    // wider fader is both visually obvious and easier to grab/drag
    // precisely. 200 px is comfortable on the new ~75%-wide right column.
    constexpr int kFaderW     = 200;
    constexpr int kSquareBtn  = 32;

    // Hard ceilings — the fader's max gain is +6 dB at frac=1.0.
    // ABSOLUTE_MAX_GAIN is the very last line of defence: even if some
    // input path slips a corrupt value past our clamps (drag past the
    // right edge, automation overshoot, scroll wheel, etc.) we never
    // let the audio thread see a gain higher than 2.0× (+6 dB). Without
    // this, a 5x sloppy click at the fader's right edge could feed 100+
    // dB of gain into the mixer — physically dangerous and a real
    // safety risk.
    constexpr float kMaxGain      = 2.0f;       // +6 dB
    constexpr float kMaxDbDisplay = 6.0f;

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
        // CRITICAL: clamp the input fraction before doing anything else.
        // A drag that extends past the right edge of the fader produced
        // frac > 1 and would then yield gain = 10^((frac*66-60)/20) —
        // at frac=2.5 that's ~178,000× linear. Hearing-damage territory.
        frac = juce::jlimit (0.f, 1.f, frac);
        if (frac <= 0.001f) return 0.f;
        const float db = frac * 66.f - 60.f;
        const float gain = std::pow (10.f, db / 20.f);
        // Final safety cap. If anyone ever lifts the frac clamp above
        // we still hit kMaxGain (+6 dB) and the user's monitors survive.
        return juce::jmin (gain, kMaxGain);
    }
    inline juce::String dbLabel (float gain)
    {
        if (gain <= 0.0001f) return "-inf";
        const float db = juce::jmin (kMaxDbDisplay, 20.f * std::log10 (gain));
        return juce::String (db, 1) + " dB";
    }
}

StemRow::StemRow (dsp::StemSession& s, dsp::Transport& t, int stemIdx)
    : session (s), transport (t), idx (stemIdx), waveform (t)
{
    addAndMakeVisible (waveform);
    waveform.setColour (Theme::stemColor (idx, session.mixState().stemCount()));

    // Drag-out / selection: forward the waveform's gestures up to the
    // parent panel. Plain click → "select this row" (the panel decides
    // what that means based on modifiers). A drag past the threshold →
    // kick off an OS drag-and-drop of the current selection.
    waveform.onClicked = [this] (juce::ModifierKeys mods)
    {
        if (onRowClicked) onRowClicked (idx, mods);
    };
    waveform.onDragOutRequested = [this] (juce::ModifierKeys mods)
    {
        if (onDragRequested) onDragRequested (idx, mods);
    };
}

void StemRow::setStemIndex (int i)
{
    idx = i;
    waveform.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    refreshFromSession();
}

void StemRow::setSelected (bool s) noexcept
{
    if (selected == s) return;
    selected = s;
    repaint();
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

    // The old 6-dot drag handle on the right edge is gone — the waveform
    // is itself the drag affordance now (see waveform.onDragOutRequested).
    // That reclaimed strip becomes a small breathing margin between the
    // dB label and the row edge, plus extra width for the waveform.
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

    // Background row. Selected rows get a slightly elevated surface tint
    // and a colored left bar so multi-selection is clearly visible.
    const auto bg = selected ? Theme::col (Theme::kSurfaceHi)
                             : Theme::col (Theme::kBackground);
    g.setColour (bg);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.f), Theme::kRadiusMedium);

    if (selected)
    {
        // Left accent bar in the stem's own colour, so the indicator also
        // reinforces which row you're looking at.
        const auto bar = getLocalBounds().toFloat().reduced (1.f).withWidth (3.f);
        g.setColour (color);
        g.fillRect (bar);

        g.setColour (color.withAlpha (0.6f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.f),
                                Theme::kRadiusMedium, 1.f);
    }

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
    // Thicker track + larger thumb for a more obvious "this is grabbable"
    // affordance on the wider fader. Easier to land on with a mouse and
    // it makes accidentally cranking up the gain to +6 dB require a real
    // intentional drag — small visual changes near 0 dB are now visible.
    const float trackH = 6.f;
    juce::Rectangle<float> trackR (track.getX(), midY - trackH * 0.5f,
                                   track.getWidth(), trackH);

    g.setColour (Theme::col (Theme::kBorderStrong));
    g.fillRoundedRectangle (trackR, trackH * 0.5f);

    // Unity tick at frac=60/66 (where log-scale gain = 1.0)
    const float unityFrac = 60.f / 66.f;
    const float unityX = track.getX() + track.getWidth() * unityFrac;
    g.setColour (Theme::col (Theme::kBorder));
    g.drawLine (unityX, track.getY() + 2, unityX, track.getBottom() - 2, 1.f);

    // "0" caption under the unity tick. Tiny, tertiary text so it reads as
    // a scale marker rather than competing with the dB readout on the
    // right of the row. Clipped to the row bounds in case the fader sits
    // hard against the bottom of the row.
    {
        constexpr int kCapW = 12;
        constexpr int kCapH = 10;
        const int capX = juce::roundToInt (unityX) - kCapW / 2;
        const int capY = juce::jmin (juce::roundToInt (track.getBottom()) + 1,
                                     getLocalBounds().getBottom() - kCapH);
        const juce::Rectangle<int> capR (capX, capY, kCapW, kCapH);
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText ("0", capR, juce::Justification::centred, false);
    }

    // Filled track
    const float fillW = track.getWidth() * frac;
    g.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    g.fillRoundedRectangle ({ track.getX(), midY - trackH * 0.5f, fillW, trackH },
                            trackH * 0.5f);

    // Thumb — bigger and softer-edged than before for a more grabbable
    // feel on a 200 px-wide fader. The outer white ring reads as a halo
    // against the stem-colored fill; together they're hard to miss.
    const float thumbX = track.getX() + fillW;
    const float thumbR = 11.f;
    g.setColour (juce::Colours::white);
    g.fillEllipse (thumbX - thumbR, midY - thumbR, thumbR * 2, thumbR * 2);
    g.setColour (Theme::stemColor (idx, session.mixState().stemCount()));
    g.fillEllipse (thumbX - thumbR + 2.5f, midY - thumbR + 2.5f,
                   (thumbR - 2.5f) * 2, (thumbR - 2.5f) * 2);
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
        // Pixel-clamp BEFORE division so the fraction is bounded even if
        // the mouse is sitting on the fader's exact right edge (where
        // pos.x == faderBounds.getRight() would give frac > 1).
        const int xClamped = juce::jlimit (faderBounds.getX(),
                                           faderBounds.getRight(),
                                           pos.x);
        const float frac = (float) (xClamped - faderBounds.getX())
                         / (float) faderBounds.getWidth();
        slot.gain.store (fractionToGain (frac));
        repaint(); return;
    }

    // Everything else (dot, name, gap before/after the M/S buttons, the
    // strip near the dB label) becomes a row "grip" — clicks select,
    // drags export. Mirrors the waveform's gesture model so the row
    // feels uniformly grabbable.
    pressArmed = true;
    pressMods  = e.mods;
}

void StemRow::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingFader)
    {
        auto& slot = session.mixState().slot (idx);
        // Same clamp as mouseDown — once the user starts dragging, the
        // mouse can travel ANYWHERE on screen, but the gain must stay
        // bounded between -inf and +6 dB.
        const int xClamped = juce::jlimit (faderBounds.getX(),
                                           faderBounds.getRight(),
                                           e.x);
        const float frac = (float) (xClamped - faderBounds.getX())
                         / (float) faderBounds.getWidth();
        slot.gain.store (fractionToGain (frac));
        repaint();
        return;
    }
    if (pressArmed && e.getDistanceFromDragStart() > kDragOutPx)
    {
        pressArmed = false;
        if (onDragRequested) onDragRequested (idx, pressMods);
    }
}

void StemRow::mouseUp (const juce::MouseEvent&)
{
    if (draggingFader) { draggingFader = false; return; }
    if (pressArmed)
    {
        pressArmed = false;
        if (onRowClicked) onRowClicked (idx, pressMods);
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
    if (muteBounds.contains (pos) || soloBounds.contains (pos))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (faderBounds.contains (pos))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        // Name / dot / row body / waveform pass-through — drag-to-DAW is
        // armed across the whole row. (The waveform child sets its own
        // cursor too, so this only fires when the move bubbles to us.)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

} // namespace stemmerizer::ui
