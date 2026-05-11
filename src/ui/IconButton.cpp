#include "IconButton.h"

namespace stemmerizer::ui
{

IconButton::IconButton (Glyph g) : glyph (g)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::mouseEnter (const juce::MouseEvent&) { hovered = true;  repaint(); }
void IconButton::mouseExit  (const juce::MouseEvent&) { hovered = false; pressed = false; repaint(); }
void IconButton::mouseDown  (const juce::MouseEvent&) { pressed = true;  repaint(); }

void IconButton::mouseUp (const juce::MouseEvent& e)
{
    pressed = false;
    repaint();
    if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
}

void IconButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    juce::Colour bg;
    if (active)            bg = Theme::col (Theme::kAccent).withAlpha (0.18f);
    else if (pressed)      bg = Theme::col (Theme::kSurfaceHi).brighter (0.04f);
    else if (hovered)      bg = Theme::col (Theme::kSurfaceHi);
    else if (subtle)       bg = juce::Colours::transparentBlack;
    else                   bg = Theme::col (Theme::kSurface);

    g.setColour (bg);
    g.fillRoundedRectangle (bounds, 8.f);

    if (! subtle && ! active)
    {
        g.setColour (Theme::col (Theme::kBorder));
        g.drawRoundedRectangle (bounds, 8.f, 1.f);
    }

    juce::Colour fg = active ? Theme::col (Theme::kAccent)
                             : hovered ? Theme::col (Theme::kTextPrimary)
                                       : Theme::col (Theme::kTextSecondary);

    drawGlyph (g, bounds.reduced (6.f), fg);
}

void IconButton::drawGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fg) const
{
    g.setColour (fg);
    juce::Path p;
    const float cx = r.getCentreX();
    const float cy = r.getCentreY();
    const float w  = r.getWidth();
    const float h  = r.getHeight();

    switch (glyph)
    {
        case Glyph::Gear:
        {
            const int teeth = 8;
            const float rO = w * 0.5f;
            const float rI = w * 0.36f;
            const float hub = w * 0.18f;
            for (int i = 0; i < teeth; ++i)
            {
                const float a0 = juce::MathConstants<float>::twoPi * i / teeth - 0.18f;
                const float a1 = juce::MathConstants<float>::twoPi * i / teeth + 0.18f;
                p.addPieSegment (cx - rO, cy - rO, rO * 2, rO * 2, a0, a1, rI / rO);
            }
            p.addEllipse (cx - rI, cy - rI, rI * 2, rI * 2);
            p.addEllipse (cx - hub, cy - hub, hub * 2, hub * 2);
            p.setUsingNonZeroWinding (false);
            g.fillPath (p);
            break;
        }
        case Glyph::Folder:
        {
            p.startNewSubPath (r.getX(),                r.getY() + h * 0.30f);
            p.lineTo          (r.getX() + w * 0.30f,   r.getY() + h * 0.30f);
            p.lineTo          (r.getX() + w * 0.40f,   r.getY() + h * 0.18f);
            p.lineTo          (r.getRight(),            r.getY() + h * 0.18f);
            p.lineTo          (r.getRight(),            r.getBottom() - h * 0.10f);
            p.lineTo          (r.getX(),                r.getBottom() - h * 0.10f);
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            break;
        }
        case Glyph::Play:
            p.addTriangle (r.getX() + w * 0.18f, r.getY() + h * 0.12f,
                           r.getRight() - w * 0.10f, cy,
                           r.getX() + w * 0.18f, r.getBottom() - h * 0.12f);
            g.fillPath (p);
            break;

        case Glyph::Pause:
            g.fillRoundedRectangle (r.getX() + w * 0.20f, r.getY() + h * 0.15f, w * 0.18f, h * 0.70f, 1.5f);
            g.fillRoundedRectangle (r.getX() + w * 0.62f, r.getY() + h * 0.15f, w * 0.18f, h * 0.70f, 1.5f);
            break;

        case Glyph::Stop:
            g.fillRoundedRectangle (r.getX() + w * 0.20f, r.getY() + h * 0.20f, w * 0.60f, h * 0.60f, 2.f);
            break;

        case Glyph::Cross:
            p.startNewSubPath (r.getX() + w * 0.22f, r.getY() + h * 0.22f);
            p.lineTo          (r.getRight() - w * 0.22f, r.getBottom() - h * 0.22f);
            p.startNewSubPath (r.getRight() - w * 0.22f, r.getY() + h * 0.22f);
            p.lineTo          (r.getX() + w * 0.22f, r.getBottom() - h * 0.22f);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            break;

        case Glyph::Check:
            p.startNewSubPath (r.getX() + w * 0.18f, cy);
            p.lineTo          (cx - w * 0.05f,        r.getBottom() - h * 0.22f);
            p.lineTo          (r.getRight() - w * 0.18f, r.getY() + h * 0.22f);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            break;

        case Glyph::Trash:
            p.addRoundedRectangle (r.getX() + w * 0.18f, r.getY() + h * 0.30f,
                                   w * 0.64f, h * 0.55f, 2.f);
            p.startNewSubPath (r.getX() + w * 0.10f, r.getY() + h * 0.28f);
            p.lineTo          (r.getRight() - w * 0.10f, r.getY() + h * 0.28f);
            p.startNewSubPath (r.getX() + w * 0.36f, r.getY() + h * 0.18f);
            p.lineTo          (r.getRight() - w * 0.36f, r.getY() + h * 0.18f);
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            break;

        case Glyph::Reveal:
            p.startNewSubPath (r.getX() + w * 0.20f, cy);
            p.lineTo          (r.getRight() - w * 0.20f, cy);
            p.startNewSubPath (r.getRight() - w * 0.40f, r.getY() + h * 0.25f);
            p.lineTo          (r.getRight() - w * 0.20f, cy);
            p.lineTo          (r.getRight() - w * 0.40f, r.getBottom() - h * 0.25f);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            break;

        case Glyph::Mute:
            // speaker + slash
            p.startNewSubPath (r.getX() + w * 0.20f, r.getY() + h * 0.38f);
            p.lineTo          (r.getX() + w * 0.36f, r.getY() + h * 0.38f);
            p.lineTo          (r.getX() + w * 0.52f, r.getY() + h * 0.22f);
            p.lineTo          (r.getX() + w * 0.52f, r.getBottom() - h * 0.22f);
            p.lineTo          (r.getX() + w * 0.36f, r.getBottom() - h * 0.38f);
            p.lineTo          (r.getX() + w * 0.20f, r.getBottom() - h * 0.38f);
            p.closeSubPath();
            g.fillPath (p);
            // slash
            g.setColour (Theme::col (Theme::kError));
            p.clear();
            p.startNewSubPath (r.getX() + w * 0.18f, r.getY() + h * 0.20f);
            p.lineTo          (r.getRight() - w * 0.18f, r.getBottom() - h * 0.20f);
            g.strokePath (p, juce::PathStrokeType (1.8f));
            break;

        case Glyph::Solo:
            // bold "S"
            g.setFont (Theme::heading());
            g.drawText ("S", r, juce::Justification::centred);
            break;

        case Glyph::Plus:
            p.startNewSubPath (cx, r.getY() + h * 0.20f);
            p.lineTo          (cx, r.getBottom() - h * 0.20f);
            p.startNewSubPath (r.getX() + w * 0.20f, cy);
            p.lineTo          (r.getRight() - w * 0.20f, cy);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
            break;

        case Glyph::Reload:
            p.addCentredArc (cx, cy, w * 0.32f, w * 0.32f, 0.f,
                             0.4f, 5.5f, true);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            // arrowhead
            p.clear();
            p.addTriangle (cx + w * 0.12f, r.getY() + h * 0.15f,
                           cx + w * 0.34f, r.getY() + h * 0.30f,
                           cx + w * 0.18f, r.getY() + h * 0.42f);
            g.fillPath (p);
            break;
    }
}

} // namespace stemmerizer::ui
