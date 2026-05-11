#include "DropZone.h"

#include <cmath>

namespace stemmerizer::ui
{

DropZone::DropZone()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setInterceptsMouseClicks (true, false);
    setTooltip ("Drop audio files or click to browse");
}

void DropZone::tick()
{
    // Only animate the idle pulse when the user is actually engaged with the
    // zone, OR when the eased glow factors haven't yet settled back to zero.
    // When nothing is happening we skip both the phase advance and the
    // repaint entirely so the editor isn't hammering 30 Hz redraws for
    // a static panel.
    const bool isAnimating = hovered || draggingOver
                          || hoverGlow > 0.01f || dragGlow > 0.01f;

    const float targetHover = hovered      ? 1.f : 0.f;
    const float targetDrag  = draggingOver ? 1.f : 0.f;

    const float prevHoverGlow = hoverGlow;
    const float prevDragGlow  = dragGlow;
    const float prevPulse     = pulse;

    if (isAnimating)
    {
        pulse += 0.015f;
        if (pulse > juce::MathConstants<float>::twoPi)
            pulse -= juce::MathConstants<float>::twoPi;

        hoverGlow += (targetHover - hoverGlow) * 0.18f;
        dragGlow  += (targetDrag  - dragGlow)  * 0.25f;
    }
    else
    {
        // Snap residuals fully to zero so we don't keep waking up over noise.
        hoverGlow = 0.f;
        dragGlow  = 0.f;
    }

    constexpr float kEps = 0.005f;
    const bool changed = std::abs (hoverGlow - prevHoverGlow) > kEps
                      || std::abs (dragGlow  - prevDragGlow)  > kEps
                      || (isAnimating && std::abs (pulse - prevPulse) > kEps);

    if (changed)
        repaint();
}

void DropZone::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.f);
    const float r     = Theme::kRadiusLarge;

    // base panel
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (bounds, r);

    // dashed border (denser when dragging)
    const float dashLen = juce::jmap (dragGlow, 0.f, 1.f, 8.f, 14.f);
    const float dashGap = juce::jmap (dragGlow, 0.f, 1.f, 6.f, 4.f);
    const float lineW   = 1.5f + dragGlow * 1.5f;

    auto borderColor = draggingOver
                     ? Theme::col (Theme::kAccent)
                     : Theme::col (Theme::kBorderStrong).interpolatedWith (
                         Theme::col (Theme::kAccentMuted), hoverGlow * 0.6f);

    g.setColour (borderColor);
    juce::Path p;
    p.addRoundedRectangle (bounds, r);
    juce::Path dashed;
    const float dashes[2] = { dashLen, dashGap };
    juce::PathStrokeType (lineW).createDashedStroke (dashed, p, dashes, 2);
    g.strokePath (dashed, juce::PathStrokeType (lineW));

    // accent glow ring (idle pulse + drag boost)
    const float pulseAmt = (std::sin (pulse) * 0.5f + 0.5f) * 0.15f;
    const float glow     = juce::jmin (1.f, dragGlow + hoverGlow * 0.4f + pulseAmt);
    if (glow > 0.01f)
    {
        g.setColour (Theme::col (Theme::kAccent).withAlpha (glow * 0.10f));
        g.fillRoundedRectangle (bounds, r);
    }

    // ---- center content ----
    const auto centre = bounds.getCentre();

    // Big circular icon — concentric arcs, abstract "stem split" mark.
    {
        const float iconR = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.13f;
        juce::Path arc;

        const auto baseColor = Theme::col (Theme::kAccent).withAlpha (0.85f);

        for (int i = 0; i < 4; ++i)
        {
            const float radius = iconR * (1.f - i * 0.2f);
            arc.clear();
            const float startA = -juce::MathConstants<float>::halfPi
                               - 0.7f + i * 0.18f + std::sin (pulse + i * 0.7f) * 0.08f;
            const float endA   = startA + 1.4f + std::sin (pulse * 1.3f + i) * 0.05f;
            arc.addCentredArc (centre.getX(), centre.getY() - 30.f,
                               radius, radius, 0.f, startA, endA, true);
            g.setColour (baseColor.withAlpha (0.25f + i * 0.18f));
            g.strokePath (arc, juce::PathStrokeType (2.f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
    }

    // Title
    g.setColour (Theme::col (Theme::kTextPrimary));
    g.setFont (Theme::display());
    g.drawText (draggingOver ? "drop to split"
                             : "drop audio to split",
                bounds.withTrimmedTop (bounds.getHeight() * 0.55f).withHeight (40),
                juce::Justification::centred);

    // Caption — formats and click-to-browse hint combined onto one line.
    g.setColour (Theme::col (Theme::kTextSecondary));
    g.setFont (Theme::caption());
    g.drawText ("WAV \xc2\xb7 FLAC \xc2\xb7 MP3 \xc2\xb7 AIFF \xc2\xb7 OGG  \xe2\x80\x94  or click to browse",
                bounds.withTrimmedTop (bounds.getHeight() * 0.55f + 46).withHeight (22),
                juce::Justification::centred);
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
