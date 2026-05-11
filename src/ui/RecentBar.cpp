#include "RecentBar.h"

namespace stemmerizer::ui
{

using Theme::col;

namespace
{
    constexpr int kRemoveSize  = 16;   // hit area for the × button
    constexpr int kRemoveInset = 4;    // inset from card top-right corner
    constexpr int kClearW      = 44;   // reserved width for the "clear" link
    constexpr int kDragOutPx   = 6;

    /// Human-readable relative time: "just now", "5m ago", "2h ago",
    /// "3d ago" or an absolute "MMM d" for anything older than a week.
    /// Deliberately compact — these strings sit on a 160 px-wide card and
    /// share the line with the stem count.
    juce::String relativeTime (juce::int64 thenMs)
    {
        const auto nowMs = juce::Time::getCurrentTime().toMilliseconds();
        if (thenMs <= 0 || thenMs > nowMs + 60'000)
            return {};

        const auto deltaMs = nowMs - thenMs;
        const auto sec     = deltaMs / 1000;

        if (sec < 60)               return "just now";
        if (sec < 60 * 60)          return juce::String ((int) (sec / 60))         + "m ago";
        if (sec < 60 * 60 * 24)     return juce::String ((int) (sec / 3600))       + "h ago";
        if (sec < 60 * 60 * 24 * 7) return juce::String ((int) (sec / (3600 * 24))) + "d ago";

        // strftime "%b %-d" isn't portable on Windows (no %-d). Build the
        // "MMM d" string by hand from the components instead.
        const juce::Time t (thenMs);
        static const char* months[] = { "Jan","Feb","Mar","Apr","May","Jun",
                                        "Jul","Aug","Sep","Oct","Nov","Dec" };
        const int m = juce::jlimit (0, 11, t.getMonth());
        return juce::String (months[m]) + " " + juce::String (t.getDayOfMonth());
    }

    juce::String truncatedToWidth (const juce::String& text,
                                   juce::Font font, float maxWidth)
    {
        const auto width = [&] (const juce::String& s)
        {
            return juce::GlyphArrangement::getStringWidth (font, s);
        };
        if (width (text) <= maxWidth) return text;
        const auto ell = juce::String::fromUTF8 ("\xe2\x80\xa6");   // "…"
        juce::String s = text;
        while (s.isNotEmpty() && width (s + ell) > maxWidth)
            s = s.dropLastCharacters (1);
        return s + ell;
    }
}

// ---------------------------------------------------------------------------
RecentBar::RecentBar()
{
    setInterceptsMouseClicks (true, false);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void RecentBar::setEntries (std::vector<dsp::RecentEntry> e)
{
    entries  = std::move (e);
    hoverIdx = -1;
    armedIdx = -1;
    rebuildLayout();
    repaint();
}

void RecentBar::resized()
{
    rebuildLayout();
}

void RecentBar::rebuildLayout()
{
    layout.clear();
    clearBounds = {};

    if (entries.empty() || getWidth() <= 0 || getHeight() <= 0) return;

    auto bounds = getLocalBounds();

    // Reserve a slim column on the right for the "clear" link, but only
    // when we actually have entries to clear.
    clearBounds = bounds.removeFromRight (kClearW)
                        .withSizeKeepingCentre (kClearW, 18);
    bounds.removeFromRight (Theme::kPadSm);

    const int n        = (int) entries.size();
    const int gap      = Theme::kPadSm;
    const int totalGap = gap * juce::jmax (0, n - 1);
    const int availW   = juce::jmax (0, bounds.getWidth() - totalGap);
    const int cardW    = juce::jmax (kCardMinW, availW / juce::jmax (1, n));

    int x = bounds.getX();
    const int y = bounds.getY() + (bounds.getHeight() - kCardH) / 2;

    for (int i = 0; i < n; ++i)
    {
        CardLayout cl;
        cl.id     = entries[(size_t) i].id;
        cl.bounds = juce::Rectangle<int> (x, y, cardW, kCardH);

        // × hit area top-right.
        cl.removeBounds = juce::Rectangle<int> (
            cl.bounds.getRight() - kRemoveSize - kRemoveInset,
            cl.bounds.getY()     + kRemoveInset,
            kRemoveSize, kRemoveSize);

        cl.stemsExist = entries[(size_t) i].stemsExist();

        layout.push_back (cl);
        x += cardW + gap;
    }
}

void RecentBar::paint (juce::Graphics& g)
{
    if (layout.empty()) return;

    auto bodyFont    = Theme::body();
    auto captionFont = Theme::caption();

    for (size_t i = 0; i < layout.size(); ++i)
    {
        const auto& cl    = layout[i];
        const auto& entry = entries[i];
        const bool  hover = ((int) i == hoverIdx);
        const bool  ok    = cl.stemsExist;

        // ---- card surface ----
        juce::Colour fill   = hover ? col (Theme::kSurfaceHi) : col (Theme::kSurface);
        juce::Colour border = col (Theme::kBorder);

        const float alpha = ok ? 1.f : 0.5f;
        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (cl.bounds.toFloat(), Theme::kRadiusMedium);
        g.setColour (border.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (cl.bounds.toFloat().reduced (0.5f),
                                Theme::kRadiusMedium, 1.f);

        // ---- text area (leave room for the × on the right) ----
        auto textArea = cl.bounds.reduced (Theme::kPadSm, 6);
        textArea.removeFromRight (kRemoveSize + kRemoveInset);

        // Title line: filename (truncated).
        {
            g.setFont (bodyFont);
            g.setColour (col (Theme::kTextPrimary).withMultipliedAlpha (alpha));
            const auto label = truncatedToWidth (
                entry.inputBasename.isNotEmpty() ? entry.inputBasename : entry.id,
                bodyFont, (float) textArea.getWidth());
            g.drawText (label,
                        textArea.removeFromTop ((int) bodyFont.getHeight() + 2),
                        juce::Justification::topLeft, false);
        }

        textArea.removeFromTop (2);

        // Subline: "N stems · 2h ago" or "missing files".
        {
            g.setFont (captionFont);
            juce::String sub;
            juce::Colour subCol;
            if (! ok)
            {
                sub    = "missing files";
                subCol = col (Theme::kError);
            }
            else
            {
                const int nStems = entry.stemFiles.size();
                sub = juce::String (nStems) + (nStems == 1 ? " stem" : " stems");
                const auto rel = relativeTime (entry.timestamp);
                if (rel.isNotEmpty()) sub += juce::String::fromUTF8 (" \xc2\xb7 ") + rel;
                subCol = col (Theme::kTextTertiary);
            }
            g.setColour (subCol.withMultipliedAlpha (alpha));
            const auto subDrawn = truncatedToWidth (sub, captionFont,
                                                    (float) textArea.getWidth());
            g.drawText (subDrawn,
                        textArea.removeFromTop ((int) captionFont.getHeight() + 2),
                        juce::Justification::topLeft, false);
        }

        // ---- × glyph in top-right ----
        {
            const auto rb = cl.removeBounds.toFloat();
            const bool overRemove = hover && rb.contains (getMouseXYRelative().toFloat());
            const auto xCol = (overRemove ? col (Theme::kTextPrimary)
                                          : col (Theme::kTextTertiary))
                                  .withMultipliedAlpha (alpha);
            g.setColour (xCol);
            const float pad = 4.f;
            g.drawLine (rb.getX() + pad,    rb.getY() + pad,
                        rb.getRight() - pad, rb.getBottom() - pad, 1.4f);
            g.drawLine (rb.getRight() - pad, rb.getY() + pad,
                        rb.getX() + pad,    rb.getBottom() - pad, 1.4f);
        }
    }

    // ---- "clear" text link ----
    if (! clearBounds.isEmpty())
    {
        const bool hover = clearBounds.contains (getMouseXYRelative());
        g.setFont (Theme::caption());
        g.setColour (hover ? col (Theme::kTextPrimary)
                           : col (Theme::kTextTertiary));
        g.drawText ("clear", clearBounds, juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------
int RecentBar::cardIndexAt (juce::Point<int> p) const
{
    for (size_t i = 0; i < layout.size(); ++i)
        if (layout[i].bounds.contains (p)) return (int) i;
    return -1;
}

bool RecentBar::isOverRemove (int cardIdx, juce::Point<int> p) const
{
    if (cardIdx < 0 || cardIdx >= (int) layout.size()) return false;
    return layout[(size_t) cardIdx].removeBounds.contains (p);
}

void RecentBar::mouseMove (const juce::MouseEvent& e)
{
    const int idx = cardIndexAt (e.getPosition());
    if (idx != hoverIdx)
    {
        hoverIdx = idx;
        repaint();
    }
    else if (! clearBounds.isEmpty())
    {
        // Clear-link / × icon hover state changes are detected in paint()
        // (it reads getMouseXYRelative), so just nudge a cheap repaint.
        repaint();
    }
    // Update cursor depending on where we are.
    if (idx >= 0 && layout[(size_t) idx].stemsExist)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else if (! clearBounds.isEmpty() && clearBounds.contains (e.getPosition()))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void RecentBar::mouseExit (const juce::MouseEvent&)
{
    if (hoverIdx != -1)
    {
        hoverIdx = -1;
        repaint();
    }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void RecentBar::mouseDown (const juce::MouseEvent& e)
{
    pressPos = e.getPosition();
    armedIdx = -1;

    // Clear-all hit?
    if (! clearBounds.isEmpty() && clearBounds.contains (pressPos))
        return;

    const int idx = cardIndexAt (pressPos);
    if (idx < 0) return;

    // × hit on this card?
    if (isOverRemove (idx, pressPos)) return;

    // Arm a possible drag; we only commit on mouseDrag past the threshold.
    if (layout[(size_t) idx].stemsExist)
        armedIdx = idx;
}

void RecentBar::mouseDrag (const juce::MouseEvent& e)
{
    if (armedIdx < 0) return;
    if (e.getDistanceFromDragStart() < kDragOutPx) return;

    const int idx = armedIdx;
    armedIdx = -1;   // consume — we're firing the drag now

    if (idx >= (int) entries.size()) return;
    const auto& entry = entries[(size_t) idx];
    if (! entry.stemsExist()) return;

    // External drag of the stem files. Anything below can throw (OLE init,
    // bad_alloc); a mouseDrag dispatcher is effectively noexcept, so guard.
    try
    {
        juce::StringArray paths = entry.stemFiles;
        juce::DragAndDropContainer::performExternalDragDropOfFiles (paths, false, this);
    }
    catch (...) { /* user can retry; don't crash the DAW */ }
}

void RecentBar::mouseUp (const juce::MouseEvent& e)
{
    armedIdx = -1;
    const auto pos = e.getPosition();

    if (e.mouseWasDraggedSinceMouseDown()) return;

    // "clear" link
    if (! clearBounds.isEmpty() && clearBounds.contains (pos))
    {
        if (onClearAll) onClearAll();
        return;
    }

    // × on a card
    const int idx = cardIndexAt (pos);
    if (idx >= 0 && isOverRemove (idx, pos))
    {
        if (onRemoveRequested)
            onRemoveRequested (layout[(size_t) idx].id);
    }
}

} // namespace stemmerizer::ui
