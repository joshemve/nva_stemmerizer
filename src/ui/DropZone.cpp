#include "DropZone.h"

#include <cmath>

namespace stemmerizer::ui
{

namespace
{
    // Content cap so the hero doesn't sprawl across a 1200-px wide DropZone:
    // we always centre a fixed-width "card" of text inside the panel, no
    // matter how wide the panel itself is.
    constexpr int kContentMaxW    = 540;
    constexpr int kIconSize       = 72;     // base icon diameter (before breath scale)
    constexpr int kIconTitleGap   = 14;
    constexpr int kTitleCapGap    = 8;
    constexpr int kTitleH         = 38;
    constexpr int kCaptionH       = 22;
}

DropZone::DropZone()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setInterceptsMouseClicks (true, false);
    setTooltip ("Drop audio files or click to browse");
}

void DropZone::tick()
{
    // The DropZone is the hero of the empty state — we want a quiet,
    // continuous motion to communicate "this thing is alive and ready".
    // So we always advance the slow pulse + dash phase, and additionally
    // ease in the hover/drag glow when the user interacts.
    pulse += 0.018f;
    if (pulse > juce::MathConstants<float>::twoPi)
        pulse -= juce::MathConstants<float>::twoPi;

    // Dash phase: linear accumulation makes the border feel like it's
    // flowing around the perimeter. Wraps at the dash-pattern length so
    // it never accumulates floating-point drift.
    dashPhase += 0.6f;
    if (dashPhase > 100.f) dashPhase -= 100.f;

    const float targetHover = hovered      ? 1.f : 0.f;
    const float targetDrag  = draggingOver ? 1.f : 0.f;
    hoverGlow += (targetHover - hoverGlow) * 0.18f;
    dragGlow  += (targetDrag  - dragGlow)  * 0.25f;

    repaint();
}

void DropZone::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.f);
    const float r     = Theme::kRadiusLarge;

    // ---- Base panel --------------------------------------------------
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (bounds, r);

    // ---- Flowing dashed border --------------------------------------
    // Implemented by drawing two staggered dash sequences with opposite
    // alpha gradients along their length. As `dashPhase` ticks up, the
    // brighter dashes appear to march around the perimeter. When the
    // user drags a file over, the whole border becomes solid accent.
    {
        const float dashLen = juce::jmap (dragGlow, 0.f, 1.f, 9.f, 16.f);
        const float dashGap = juce::jmap (dragGlow, 0.f, 1.f, 6.f, 3.f);
        const float lineW   = 1.5f + dragGlow * 1.6f;

        const auto borderBase = draggingOver
                              ? Theme::col (Theme::kAccent)
                              : Theme::col (Theme::kBorderStrong);
        const auto borderTint = Theme::col (Theme::kAccent)
                                  .withAlpha (0.55f + 0.45f * dragGlow);

        juce::Path perimeter;
        perimeter.addRoundedRectangle (bounds, r);

        // Static dim dashes (always visible) — the bed.
        {
            juce::Path dashed;
            const float dashes[2] = { dashLen, dashGap };
            juce::PathStrokeType (lineW).createDashedStroke (dashed, perimeter, dashes, 2);
            g.setColour (borderBase.withAlpha (0.6f));
            g.strokePath (dashed, juce::PathStrokeType (lineW));
        }

        // Animated overlay — a phase-shifted accent dash pattern that
        // simulates "flowing" motion. We approximate the phase offset by
        // changing the gap ratio over time (real dash-offset isn't part
        // of JUCE's stroke API).
        if (! draggingOver)
        {
            const float t = std::sin (dashPhase * 0.06f) * 0.5f + 0.5f;   // 0..1
            const float oDashLen = juce::jmap (t, dashLen * 0.7f, dashLen * 1.3f);
            const float oDashGap = juce::jmap (t, dashGap * 1.4f, dashGap * 0.7f);

            juce::Path dashed;
            const float dashes[2] = { oDashLen, oDashGap };
            juce::PathStrokeType (lineW).createDashedStroke (dashed, perimeter, dashes, 2);
            g.setColour (borderTint.withAlpha ((0.10f + hoverGlow * 0.25f) * (0.6f + 0.4f * t)));
            g.strokePath (dashed, juce::PathStrokeType (lineW));
        }
    }

    // ---- Soft accent wash on hover/drag -----------------------------
    {
        const float pulseAmt = (std::sin (pulse) * 0.5f + 0.5f) * 0.06f;
        const float glow     = juce::jmin (1.f, dragGlow * 0.6f + hoverGlow * 0.25f + pulseAmt);
        if (glow > 0.01f)
        {
            g.setColour (Theme::col (Theme::kAccent).withAlpha (glow * 0.08f));
            g.fillRoundedRectangle (bounds, r);
        }
    }

    // ---- Centered content card --------------------------------------
    // Build a fixed-max-width content area and centre it both axes. This
    // is what keeps the hero balanced when the DropZone fills the full
    // body in onboarding mode AND when it shrinks back to the left column
    // after a session loads. The content shape stays identical.
    const int contentW = juce::jmin ((int) bounds.getWidth() - 64, kContentMaxW);
    const int contentH = kIconSize + kIconTitleGap + kTitleH + kTitleCapGap + kCaptionH;
    const int contentX = (int) bounds.getCentreX() - contentW / 2;
    const int contentY = (int) bounds.getCentreY() - contentH / 2;
    auto box = juce::Rectangle<int> (contentX, contentY, contentW, contentH);

    // ---- Breathing icon ---------------------------------------------
    // Concentric arcs that gently fan in/out as `pulse` advances. On
    // drag-over the icon "leans forward" (scales up a touch).
    {
        const auto iconArea = box.removeFromTop (kIconSize);
        const float breath  = 1.0f + 0.05f * std::sin (pulse * 1.3f)
                                   + 0.10f * dragGlow;
        const float baseR   = (float) kIconSize * 0.45f * breath;
        const float cx = (float) iconArea.getCentreX();
        const float cy = (float) iconArea.getCentreY();

        const auto accent = Theme::col (Theme::kAccent);

        for (int i = 0; i < 4; ++i)
        {
            const float radius = baseR * (1.f - i * 0.20f);
            const float wob    = std::sin (pulse + i * 0.7f) * 0.10f;
            const float startA = -juce::MathConstants<float>::halfPi
                                 - 0.7f + i * 0.18f + wob;
            const float endA   = startA + 1.45f + std::sin (pulse * 1.3f + i) * 0.05f;

            juce::Path arc;
            arc.addCentredArc (cx, cy, radius, radius, 0.f, startA, endA, true);
            g.setColour (accent.withAlpha (0.30f + i * 0.18f));
            g.strokePath (arc, juce::PathStrokeType (2.2f,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Soft inner dot — anchors the eye to the centre.
        g.setColour (Theme::col (Theme::kTextPrimary).withAlpha (0.95f));
        g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.f, 5.f);
    }

    box.removeFromTop (kIconTitleGap);

    // ---- Title ------------------------------------------------------
    {
        const auto titleArea = box.removeFromTop (kTitleH);
        g.setColour (Theme::col (Theme::kTextPrimary));
        g.setFont (Theme::display());
        g.drawText (draggingOver ? "drop to split" : "drop audio to split",
                    titleArea, juce::Justification::centred);
    }

    box.removeFromTop (kTitleCapGap);

    // ---- Caption ----------------------------------------------------
    {
        const auto capArea = box.removeFromTop (kCaptionH);
        g.setColour (Theme::col (Theme::kTextSecondary));
        g.setFont (Theme::body());
        g.drawText (juce::String::fromUTF8 (
                        "WAV \xc2\xb7 FLAC \xc2\xb7 MP3 \xc2\xb7 AIFF \xc2\xb7 OGG"
                        "  \xe2\x80\x94  or click to browse"),
                    capArea, juce::Justification::centred);
    }
}

void DropZone::resized() {}

void DropZone::mouseEnter (const juce::MouseEvent&) { hovered = true;  }
void DropZone::mouseExit  (const juce::MouseEvent&) { hovered = false; }

void DropZone::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasDraggedSinceMouseDown() && onClickToBrowse) onClickToBrowse();
}

bool DropZone::isAcceptedAudio (const juce::String& path)
{
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".flac" || ext == ".mp3" ||
           ext == ".aif" || ext == ".aiff" || ext == ".ogg";
}

bool DropZone::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files) if (isAcceptedAudio (f)) return true;
    return false;
}

void DropZone::fileDragEnter (const juce::StringArray&, int, int) { draggingOver = true;  }
void DropZone::fileDragExit  (const juce::StringArray&)           { draggingOver = false; }

void DropZone::filesDropped (const juce::StringArray& files, int, int)
{
    draggingOver = false;
    juce::Array<juce::File> picked;
    for (const auto& f : files) if (isAcceptedAudio (f)) picked.add (juce::File (f));
    if (! picked.isEmpty() && onFilesDropped) onFilesDropped (picked);
}

} // namespace stemmerizer::ui
