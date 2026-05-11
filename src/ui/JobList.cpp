#include "JobList.h"

#include <algorithm>
#include <filesystem>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kRowH      = 56;
    constexpr int kRowGap    = 8;
    constexpr int kHeaderH   = 28;

    // Each action button is 32x32 with 4 px between adjacent buttons. The
    // widest action area carries two buttons (done: reveal + dismiss), so we
    // reserve enough horizontal space on the right edge for both plus a small
    // breathing margin.
    constexpr int kBtnSize       = 32;
    constexpr int kBtnSpacing    = 4;
    constexpr int kActionGutter  = (kBtnSize * 2) + kBtnSpacing + 8; // 76 px

    // Middle dot separator (U+00B7) used in the subtext line.
    static const juce::String kMiddleDot = juce::String::fromUTF8 (" \xc2\xb7 ");

    juce::String subtext (const auto& rs)
    {
        switch (rs.state)
        {
            case dsp::Job::State::Queued:
                return "queued";
            case dsp::Job::State::Running:
                return rs.stage.isNotEmpty()
                           ? juce::String ("running") + kMiddleDot + rs.stage
                           : juce::String ("running");
            case dsp::Job::State::Done:
                return rs.elapsed.isNotEmpty()
                           ? juce::String ("done") + kMiddleDot + rs.elapsed
                           : juce::String ("done");
            case dsp::Job::State::Failed:
                return "failed";
            case dsp::Job::State::Cancelled:
                return "cancelled";
        }
        return {};
    }

    void drawXIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour stroke)
    {
        const float cx = r.getCentreX();
        const float cy = r.getCentreY();
        juce::Path p;
        p.startNewSubPath (cx - 5, cy - 5);
        p.lineTo          (cx + 5, cy + 5);
        p.startNewSubPath (cx + 5, cy - 5);
        p.lineTo          (cx - 5, cy + 5);
        g.setColour (stroke);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    void drawArrowIcon (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour stroke)
    {
        const float cx = r.getCentreX();
        const float cy = r.getCentreY();
        juce::Path p;
        p.startNewSubPath (cx - 4, cy);
        p.lineTo          (cx + 5, cy);
        p.startNewSubPath (cx + 1, cy - 4);
        p.lineTo          (cx + 5, cy);
        p.lineTo          (cx + 1, cy + 4);
        g.setColour (stroke);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

JobList::JobList() = default;

void JobList::refresh()
{
    rebuildSnapshot();
    resized();
    repaint();
}

void JobList::rebuildSnapshot()
{
    snapshot.clear();
    if (queue == nullptr) return;

    auto jobs = queue->snapshot();
    snapshot.reserve (jobs.size());

    for (const auto& j : jobs)
    {
        RowState rs;
        rs.id        = j.id;
        rs.filename  = juce::String (std::filesystem::path (j.inputPath).filename().string());
        rs.outputDir = juce::String (j.outputDir);
        rs.stage     = juce::String (j.stage);
        rs.state     = j.state;
        rs.progress  = j.progress;
        rs.elapsed   = j.elapsedSeconds > 0.0
                       ? juce::String (j.elapsedSeconds, 1) + "s"
                       : juce::String();
        snapshot.push_back (std::move (rs));
    }
}

void JobList::clampScroll()
{
    if (snapshot.empty()) { scrollY = 0; return; }

    auto inner = getLocalBounds().reduced (Theme::kPad);
    inner.removeFromTop (kHeaderH + Theme::kPad);

    const int contentH = (int) snapshot.size() * (kRowH + kRowGap) - kRowGap;
    const int maxScroll = std::max (0, contentH - inner.getHeight());
    scrollY = juce::jlimit (0, maxScroll, scrollY);
}

void JobList::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.f);

    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (bounds, Theme::kRadiusLarge);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (bounds, Theme::kRadiusLarge, 1.f);

    auto inner = getLocalBounds().reduced (Theme::kPad);
    auto header = inner.removeFromTop (kHeaderH);

    g.setColour (Theme::col (Theme::kTextPrimary));
    g.setFont (Theme::heading());
    g.drawText ("queue", header.removeFromLeft (200), juce::Justification::centredLeft);

    int active = 0, done = 0;
    for (const auto& s : snapshot)
    {
        if (s.state == dsp::Job::State::Running || s.state == dsp::Job::State::Queued) active++;
        if (s.state == dsp::Job::State::Done) done++;
    }
    g.setColour (Theme::col (Theme::kTextTertiary));
    g.setFont (Theme::caption());
    g.drawText (snapshot.empty()
                  ? juce::String ("0 jobs")
                  : juce::String (active) + " active   |   " + juce::String (done) + " done",
                header, juce::Justification::centredRight);

    if (snapshot.empty())
    {
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::body());
        g.drawText ("no jobs yet  -  drop a file to begin",
                    inner.withTrimmedTop (40),
                    juce::Justification::centredTop);
        return;
    }

    inner.removeFromTop (Theme::kPad);

    // Clip rows to the inner area so off-screen rows don't smear the header
    // / panel border while scrolling.
    juce::Graphics::ScopedSaveState saver (g);
    g.reduceClipRegion (inner);

    const auto visible = inner;
    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        const auto& rs = snapshot[i];

        // Skip rows that don't intersect the visible area.
        if (rs.bounds.getBottom() < visible.getY()) continue;
        if (rs.bounds.getY() > visible.getBottom()) break;

        const bool hovered = (int) i == hoverRow;
        const auto rowF = rs.bounds.toFloat();

        g.setColour (hovered ? Theme::col (Theme::kSurfaceHi)
                             : Theme::col (Theme::kBackground));
        g.fillRoundedRectangle (rowF, Theme::kRadiusMedium);

        auto inside = rs.bounds.reduced (12, 8);
        // Reserve space on the right for up to two action buttons.
        inside.removeFromRight (kActionGutter);

        // Top line: filename only (state moved to subtext line).
        g.setColour (Theme::col (Theme::kTextPrimary));
        g.setFont (Theme::body());
        const auto topLine = inside.removeFromTop (20);
        g.drawText (rs.filename, topLine, juce::Justification::centredLeft, true);

        // Progress bar
        const auto barRow = inside.removeFromTop (8).withTrimmedTop (2).withTrimmedBottom (2);
        const auto barF   = barRow.toFloat();

        g.setColour (Theme::col (Theme::kBorderStrong));
        g.fillRoundedRectangle (barF, 2.f);

        if (rs.state == dsp::Job::State::Done)
        {
            g.setColour (Theme::col (Theme::kSuccess));
            g.fillRoundedRectangle (barF, 2.f);
        }
        else if (rs.state == dsp::Job::State::Failed || rs.state == dsp::Job::State::Cancelled)
        {
            g.setColour (Theme::col (Theme::kBorderStrong));
            g.fillRoundedRectangle (barF, 2.f);
        }
        else
        {
            const float w = juce::jlimit (0.f, barF.getWidth(), barF.getWidth() * rs.progress);
            if (w > 0.f)
            {
                g.setColour (Theme::col (Theme::kAccent));
                g.fillRoundedRectangle (barF.withWidth (w), 2.f);
            }
        }

        // Subtext line (state + detail)
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::caption());
        const auto bot = inside.withTrimmedTop (4);
        g.drawText (subtext (rs), bot, juce::Justification::centredLeft, true);

        // Action buttons (right-justified).
        // Active/Queued: [cancel (x)] only
        // Done:          [reveal (->)] [dismiss (x)]
        // Failed:        [dismiss (x)] only
        // Cancelled:     [dismiss (x)] only
        const bool isActive = rs.state == dsp::Job::State::Running
                              || rs.state == dsp::Job::State::Queued;
        const bool isDone   = rs.state == dsp::Job::State::Done;

        if (isActive)
        {
            const auto act = rs.actionBounds.toFloat();
            g.setColour (Theme::col (Theme::kError).withAlpha (0.18f));
            g.fillRoundedRectangle (act, 6.f);
            drawXIcon (g, act, Theme::col (Theme::kError));
        }
        else if (isDone)
        {
            const auto reveal = rs.actionBounds.toFloat();
            g.setColour (Theme::col (Theme::kSurfaceHi));
            g.fillRoundedRectangle (reveal, 6.f);
            drawArrowIcon (g, reveal, Theme::col (Theme::kTextSecondary));

            const auto dismiss = rs.secondaryActionBounds.toFloat();
            g.setColour (Theme::col (Theme::kSurfaceHi));
            g.fillRoundedRectangle (dismiss, 6.f);
            drawXIcon (g, dismiss, Theme::col (Theme::kTextSecondary));
        }
        else
        {
            // Failed or Cancelled — single dismiss button.
            const auto dismiss = rs.actionBounds.toFloat();
            g.setColour (Theme::col (Theme::kSurfaceHi));
            g.fillRoundedRectangle (dismiss, 6.f);
            drawXIcon (g, dismiss, Theme::col (Theme::kTextSecondary));
        }
    }
}

void JobList::resized()
{
    auto inner = getLocalBounds().reduced (Theme::kPad);
    inner.removeFromTop (kHeaderH + Theme::kPad);

    clampScroll();

    int y = inner.getY() - scrollY;
    for (auto& rs : snapshot)
    {
        rs.bounds = juce::Rectangle<int> (inner.getX(), y, inner.getWidth(), kRowH);

        // Center the action button(s) vertically within the row.
        const int btnY = rs.bounds.getCentreY() - kBtnSize / 2;
        const int rightX = rs.bounds.getRight() - 4; // small inset from the row edge

        const bool isDone = rs.state == dsp::Job::State::Done;

        if (isDone)
        {
            // Two buttons: reveal (primary, leftmost) + dismiss (secondary, rightmost).
            const int dismissX = rightX - kBtnSize;
            const int revealX  = dismissX - kBtnSpacing - kBtnSize;
            rs.actionBounds          = juce::Rectangle<int> (revealX,  btnY, kBtnSize, kBtnSize);
            rs.secondaryActionBounds = juce::Rectangle<int> (dismissX, btnY, kBtnSize, kBtnSize);
        }
        else
        {
            // Single button (cancel, dismiss, or none for unknown states).
            rs.actionBounds          = juce::Rectangle<int> (rightX - kBtnSize, btnY,
                                                             kBtnSize, kBtnSize);
            rs.secondaryActionBounds = {};
        }

        y += kRowH + kRowGap;
    }
}

void JobList::mouseDown (const juce::MouseEvent& e)
{
    if (queue == nullptr) return;
    const auto pos = e.getPosition();
    for (const auto& rs : snapshot)
    {
        // Done: reveal (primary) + dismiss (secondary)
        if (rs.state == dsp::Job::State::Done)
        {
            if (rs.actionBounds.contains (pos))
            {
                juce::File (rs.outputDir).revealToUser();
                return;
            }
            if (rs.secondaryActionBounds.contains (pos))
            {
                // No public dismiss API on JobQueue; cancel(id) is a no-op
                // on terminal states, so the row stays put. This is the
                // safest available fallback.
                queue->cancel (rs.id);
                return;
            }
        }
        // Running / Queued: cancel
        else if (rs.state == dsp::Job::State::Running
                 || rs.state == dsp::Job::State::Queued)
        {
            if (rs.actionBounds.contains (pos))
            {
                queue->cancel (rs.id);
                return;
            }
        }
        // Failed / Cancelled: dismiss
        else
        {
            if (rs.actionBounds.contains (pos))
            {
                // No public dismiss API on JobQueue; cancel(id) is a no-op
                // on terminal states. Best-effort fallback.
                queue->cancel (rs.id);
                return;
            }
        }
    }
}

void JobList::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    int newHover = -1;
    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        if (snapshot[i].bounds.contains (pos))
        {
            newHover = (int) i;
            break;
        }
    }
    if (newHover != hoverRow) { hoverRow = newHover; repaint(); }

    // Cursor: pointing hand over either action button, normal elsewhere.
    bool overAction = false;
    for (const auto& rs : snapshot)
    {
        if (rs.actionBounds.contains (pos)
            || rs.secondaryActionBounds.contains (pos))
        {
            overAction = true;
            break;
        }
    }
    setMouseCursor (overAction ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::NormalCursor);

    // Tooltip: show output path for done rows, or stage detail for running
    // rows. Empty string clears the tooltip.
    juce::String tip;
    if (newHover >= 0 && newHover < (int) snapshot.size())
    {
        const auto& rs = snapshot[(size_t) newHover];
        if (rs.state == dsp::Job::State::Done)
            tip = rs.outputDir;
        else if (rs.state == dsp::Job::State::Running && rs.stage.isNotEmpty())
            tip = rs.stage;
    }
    setTooltip (tip);
}

void JobList::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow != -1) { hoverRow = -1; repaint(); }
    setMouseCursor (juce::MouseCursor::NormalCursor);
    setTooltip ({});
}

void JobList::mouseWheelMove (const juce::MouseEvent&,
                              const juce::MouseWheelDetails& wheel)
{
    if (snapshot.empty()) return;
    if (wheel.deltaY == 0.f) return;

    // deltaY > 0 = wheel scrolled up = content should move down (scrollY -=)
    // Use one row's worth per "detent" so even a small delta produces visible
    // motion; the clamp keeps us in range.
    const float raw = -wheel.deltaY * (float) (kRowH + kRowGap);
    int step = (int) raw;
    if (step == 0) step = wheel.deltaY > 0.f ? -1 : 1;

    const int prev = scrollY;
    scrollY += step;
    clampScroll();
    if (scrollY != prev)
    {
        resized();
        repaint();
    }
}

} // namespace stemmerizer::ui
