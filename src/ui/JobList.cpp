#include "JobList.h"

#include <filesystem>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kRowH      = 64;
    constexpr int kRowGap    = 8;
    constexpr int kHeaderH   = 28;

    juce::String stateLabel (dsp::Job::State s)
    {
        switch (s)
        {
            case dsp::Job::State::Queued:    return "queued";
            case dsp::Job::State::Running:   return "running";
            case dsp::Job::State::Done:      return "done";
            case dsp::Job::State::Failed:    return "failed";
            case dsp::Job::State::Cancelled: return "cancelled";
        }
        return "?";
    }

    juce::Colour stateColor (dsp::Job::State s)
    {
        switch (s)
        {
            case dsp::Job::State::Queued:    return Theme::col (Theme::kTextSecondary);
            case dsp::Job::State::Running:   return Theme::col (Theme::kAccent);
            case dsp::Job::State::Done:      return Theme::col (Theme::kSuccess);
            case dsp::Job::State::Failed:    return Theme::col (Theme::kError);
            case dsp::Job::State::Cancelled: return Theme::col (Theme::kTextTertiary);
        }
        return Theme::col (Theme::kTextTertiary);
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

    // Rows
    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        const auto& rs = snapshot[i];
        const bool hovered = (int) i == hoverRow;
        const auto rowF = rs.bounds.toFloat();

        g.setColour (hovered ? Theme::col (Theme::kSurfaceHi)
                             : Theme::col (Theme::kBackground));
        g.fillRoundedRectangle (rowF, Theme::kRadiusMedium);

        auto inside = rs.bounds.reduced (12, 8);

        // Reserve the right edge so the progress bar and stage text never
        // slide under the action (X / reveal) button. The action button is
        // 26 px wide and lives at right - 32; we carve out a 38 px gutter
        // (button width + breathing room) so nothing overdraws it.
        constexpr int kActionGutter = 38;
        inside.removeFromRight (kActionGutter);

        // Filename + state
        g.setColour (Theme::col (Theme::kTextPrimary));
        g.setFont (Theme::body());
        auto topLine  = inside.removeFromTop (20);
        auto stateArea = topLine.removeFromRight (130);
        const auto fnArea = topLine;
        g.drawText (rs.filename, fnArea, juce::Justification::centredLeft, true);

        g.setColour (stateColor (rs.state));
        g.setFont (Theme::caption());
        const auto label = stateLabel (rs.state) +
            (! rs.elapsed.isEmpty() ? "   |   " + rs.elapsed : juce::String());
        g.drawText (label, stateArea, juce::Justification::centredRight);

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

        // Stage / output dir
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::caption());
        const auto bot = inside.withTrimmedTop (4);
        const auto subText = rs.state == dsp::Job::State::Failed
            ? juce::String ("error")
            : (rs.state == dsp::Job::State::Done
                  ? rs.outputDir
                  : (rs.stage.isNotEmpty() ? rs.stage : juce::String()));
        g.drawText (subText, bot, juce::Justification::centredLeft, true);

        // Action button (cancel for in-progress, reveal for done)
        const auto act = rs.actionBounds.toFloat();
        if (rs.state == dsp::Job::State::Done ||
            rs.state == dsp::Job::State::Running ||
            rs.state == dsp::Job::State::Queued)
        {
            const bool isCancel = rs.state != dsp::Job::State::Done;
            g.setColour (isCancel ? Theme::col (Theme::kError).withAlpha (0.18f)
                                  : Theme::col (Theme::kSurfaceHi));
            g.fillRoundedRectangle (act, 6.f);

            g.setColour (isCancel ? Theme::col (Theme::kError)
                                  : Theme::col (Theme::kTextSecondary));
            juce::Path p;
            const float cx = act.getCentreX();
            const float cy = act.getCentreY();
            if (isCancel)
            {
                p.startNewSubPath (cx - 5, cy - 5);
                p.lineTo          (cx + 5, cy + 5);
                p.startNewSubPath (cx + 5, cy - 5);
                p.lineTo          (cx - 5, cy + 5);
                g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }
            else
            {
                p.startNewSubPath (cx - 4, cy);
                p.lineTo          (cx + 5, cy);
                p.startNewSubPath (cx + 1, cy - 4);
                p.lineTo          (cx + 5, cy);
                p.lineTo          (cx + 1, cy + 4);
                g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            }
        }
    }
}

void JobList::resized()
{
    auto inner = getLocalBounds().reduced (Theme::kPad);
    inner.removeFromTop (kHeaderH + Theme::kPad);

    int y = inner.getY();
    for (auto& rs : snapshot)
    {
        rs.bounds = juce::Rectangle<int> (inner.getX(), y, inner.getWidth(), kRowH);
        rs.actionBounds = juce::Rectangle<int> (rs.bounds.getRight() - 32,
                                                rs.bounds.getY() + 18, 26, 26);
        y += kRowH + kRowGap;
    }
}

void JobList::mouseDown (const juce::MouseEvent& e)
{
    if (queue == nullptr) return;
    const auto pos = e.getPosition();
    for (const auto& rs : snapshot)
    {
        if (rs.actionBounds.contains (pos))
        {
            if (rs.state == dsp::Job::State::Done)
            {
                juce::File (rs.outputDir).revealToUser();
            }
            else if (rs.state == dsp::Job::State::Running ||
                     rs.state == dsp::Job::State::Queued)
            {
                queue->cancel (rs.id);
            }
            return;
        }
    }
}

void JobList::mouseMove (const juce::MouseEvent& e)
{
    int newHover = -1;
    for (size_t i = 0; i < snapshot.size(); ++i)
    {
        if (snapshot[i].bounds.contains (e.getPosition()))
        {
            newHover = (int) i;
            break;
        }
    }
    if (newHover != hoverRow) { hoverRow = newHover; repaint(); }
}

void JobList::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow != -1) { hoverRow = -1; repaint(); }
}

} // namespace stemmerizer::ui
