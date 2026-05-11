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

    // Footer buttons
    auto footer = inner.removeFromBottom (kFooterH).reduced (0, Theme::kPadSm);
    const bool playingOrig = session.playOriginal();

    // Drag-all
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (dragAllBounds.toFloat(), Theme::kRadiusMedium);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (dragAllBounds.toFloat(), Theme::kRadiusMedium, 1.f);
    g.setColour (Theme::col (Theme::kTextPrimary));
    g.setFont (Theme::body());
    g.drawText ("drag all stems out", dragAllBounds, juce::Justification::centred);

    // A/B
    g.setColour (playingOrig ? Theme::col (Theme::kAccent).withAlpha (0.18f)
                             : Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (abBounds.toFloat(), Theme::kRadiusMedium);
    g.setColour (playingOrig ? Theme::col (Theme::kAccent) : Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (abBounds.toFloat(), Theme::kRadiusMedium, 1.f);
    g.setColour (playingOrig ? Theme::col (Theme::kAccent) : Theme::col (Theme::kTextPrimary));
    g.drawText (playingOrig ? "A | playing ORIGINAL"
                            : "A | playing STEMS",
                abBounds, juce::Justification::centred);
}

void StemMixerPanel::resized()
{
    auto inner = getLocalBounds().reduced (Theme::kPad);
    inner.removeFromTop (kHeaderH);

    if (rows.empty())
    {
        dragAllBounds = {};
        abBounds      = {};
        return;
    }

    auto footer = inner.removeFromBottom (kFooterH).reduced (0, Theme::kPadSm);
    const int half = (footer.getWidth() - Theme::kPad) / 2;
    dragAllBounds = footer.removeFromLeft (half);
    footer.removeFromLeft (Theme::kPad);
    abBounds      = footer;

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
    if (dragAllBounds.contains (e.getPosition())) { requestDragAll(); return; }
    if (abBounds     .contains (e.getPosition())) { toggleAB();        return; }
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

void StemMixerPanel::toggleAB()
{
    session.setPlayOriginal (! session.playOriginal());
    repaint();
}

} // namespace stemmerizer::ui
