#include "StemMixerPanel.h"

#include "../dsp/DragExporter.h"

#include <filesystem>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kRowH      = 56;
    constexpr int kRowGap    = 6;
    constexpr int kHeaderH   = 32;
    constexpr int kFooterH   = 44;
}

StemMixerPanel::StemMixerPanel (dsp::StemSession& s, dsp::Transport& t)
    : session (s), transport (t)
{
    rebuild();
}

void StemMixerPanel::rebuild()
{
    rows.clear();
    auto snap = session.currentSnapshot();
    const int n = snap ? (int) snap->stems.size() : 0;

    for (int i = 0; i < n; ++i)
    {
        auto row = std::make_unique<StemRow> (session, transport, i);
        row->onDragRequested = [this](int idx) { requestDragForStem (idx); };
        addAndMakeVisible (*row);
        row->refreshFromSession();
        rows.push_back (std::move (row));
    }
    resized();
    repaint();
}

void StemMixerPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.f);
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (bounds, Theme::kRadiusLarge);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (bounds, Theme::kRadiusLarge, 1.f);

    auto inner  = getLocalBounds().reduced (Theme::kPad);
    auto header = inner.removeFromTop (kHeaderH);

    g.setColour (Theme::col (Theme::kTextPrimary));
    g.setFont (Theme::heading());
    g.drawText ("stems", header.removeFromLeft (200), juce::Justification::centredLeft);

    g.setColour (Theme::col (Theme::kTextTertiary));
    g.setFont (Theme::caption());
    g.drawText (rows.empty() ? juce::String ("drop a file to begin")
                             : juce::String (rows.size()) + " stems loaded",
                header, juce::Justification::centredRight);

    if (rows.empty())
    {
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::body());
        g.drawText ("once a split finishes, your stems will appear here",
                    inner, juce::Justification::centred);
        return;
    }

    // ---- Footer: drag-all (left) and segmented A/B pill (right) ----
    const bool playingOrig = session.playOriginal();

    // Drag-all pill.
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (dragAllBounds.toFloat(), Theme::kRadiusMedium);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (dragAllBounds.toFloat(), Theme::kRadiusMedium, 1.f);
    g.setColour (Theme::col (Theme::kTextPrimary));
    g.setFont (Theme::body());
    g.drawText ("drag all stems out", dragAllBounds, juce::Justification::centred);

    // Segmented A/B — one rounded panel split by a 1 px divider.
    {
        const auto fullF = abBounds.toFloat();
        const float radius = Theme::kRadiusMedium;

        // Outer panel.
        g.setColour (Theme::col (Theme::kSurface));
        g.fillRoundedRectangle (fullF, radius);

        // Active half fill — clip to a rounded version of the union so the
        // accent wash inherits the pill's outer curvature.
        const auto activeRect = playingOrig ? abOriginalBounds : abStemsBounds;
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (fullF, radius);
            g.reduceClipRegion (clip);
            g.setColour (Theme::col (Theme::kAccent).withAlpha (0.18f));
            g.fillRect (activeRect.toFloat());
        }

        // Divider.
        g.setColour (Theme::col (Theme::kBorder));
        const float dividerX = (float) abOriginalBounds.getX();
        g.fillRect (juce::Rectangle<float> (dividerX, fullF.getY() + 4.f,
                                            1.f, fullF.getHeight() - 8.f));

        // Outer border.
        g.setColour (Theme::col (Theme::kBorder));
        g.drawRoundedRectangle (fullF, radius, 1.f);

        // Labels.
        g.setFont (Theme::body());

        const auto stemsColor = (! playingOrig) ? Theme::col (Theme::kAccent)
                                                : Theme::col (Theme::kTextTertiary);
        const auto origColor  = playingOrig     ? Theme::col (Theme::kAccent)
                                                : Theme::col (Theme::kTextTertiary);

        g.setColour (stemsColor);
        g.drawText ("STEMS",    abStemsBounds,    juce::Justification::centred);
        g.setColour (origColor);
        g.drawText ("ORIGINAL", abOriginalBounds, juce::Justification::centred);
    }
}

void StemMixerPanel::resized()
{
    auto inner = getLocalBounds().reduced (Theme::kPad);
    inner.removeFromTop (kHeaderH);

    if (rows.empty())
    {
        dragAllBounds      = {};
        abBounds           = {};
        abStemsBounds      = {};
        abOriginalBounds   = {};
        return;
    }

    auto footer = inner.removeFromBottom (kFooterH).reduced (0, Theme::kPadSm);
    const int half = (footer.getWidth() - Theme::kPad) / 2;
    dragAllBounds = footer.removeFromLeft (half);
    footer.removeFromLeft (Theme::kPad);
    abBounds      = footer;

    // Split A/B union into two halves.
    const int abHalf = abBounds.getWidth() / 2;
    abStemsBounds    = abBounds.withWidth (abHalf);
    abOriginalBounds = abBounds.withTrimmedLeft (abHalf);

    inner.removeFromTop (Theme::kPadSm);
    inner.removeFromBottom (Theme::kPadSm);

    int y = inner.getY();
    for (auto& r : rows)
    {
        r->setBounds (inner.getX(), y, inner.getWidth(), kRowH);
        y += kRowH + kRowGap;
    }
}

void StemMixerPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (dragAllBounds.contains (pos))      { requestDragAll(); return; }
    if (abStemsBounds.contains (pos))      { session.setPlayOriginal (false); repaint(); return; }
    if (abOriginalBounds.contains (pos))   { session.setPlayOriginal (true);  repaint(); return; }
}

void StemMixerPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (dragAllBounds.contains (pos))
        setTooltip ("Drag all stems out as a folder");
    else if (abStemsBounds.contains (pos))
        setTooltip ("Listen to the separated stems mix");
    else if (abOriginalBounds.contains (pos))
        setTooltip ("Listen to the original input audio (A/B compare)");
    else
        setTooltip ({});
}

void StemMixerPanel::requestDragForStem (int idx)
{
    auto snap = session.currentSnapshot();
    if (! snap) return;
    const auto base = std::filesystem::path (session.sourceFilePath()).stem().string();
    dsp::DragExporter::dragStem (this, *snap, idx, base);
}

void StemMixerPanel::requestDragAll()
{
    auto snap = session.currentSnapshot();
    if (! snap) return;
    const auto base = std::filesystem::path (session.sourceFilePath()).stem().string();
    dsp::DragExporter::dragAllStems (this, *snap, base);
}

} // namespace stemmerizer::ui
