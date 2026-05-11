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
    constexpr int kFooterH   = 48;

    // A/B pill metrics. Lives in the top-right of the header now, so a
    // shorter height than the old footer pill is needed.
    constexpr int kAbPillW   = 140;
    constexpr int kAbPillH   = 24;     // <= kHeaderH so it fits vertically

    // Minimum width (in px) of the BPM/key caption before we drop it to
    // make room for the A/B pill on narrow panels.
    constexpr int kHeaderInfoMinW = 100;

    constexpr int kDragDistThreshold = 6;
    constexpr int kErrorVisibleMs    = 4000;

    // Middle dot separator (U+00B7). JUCE's String(const char*) parses raw
    // bytes as Latin-1, so the UTF-8 sequence \xc2\xb7 has to go through
    // fromUTF8 to yield the actual dot.
    static const juce::String kMiddleDot = juce::String::fromUTF8 (" \xc2\xb7 ");
}

StemMixerPanel::StemMixerPanel (dsp::StemSession& s, dsp::Transport& t)
    : session (s), transport (t)
{
    rebuild();
}

StemMixerPanel::~StemMixerPanel()
{
    stopTimer();
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

    // Run BPM / key detection on the original mix. Synchronous on the
    // message thread is acceptable: rebuild() only fires when a split
    // completes (rare), and a few-minute song at 44.1 kHz analyses well
    // under a second with these parameters. Conservative thresholds inside
    // the analyzers ensure we surface nothing rather than misleading info.
    bpmInfo.reset();
    keyInfo.reset();
    if (snap && ! snap->original.empty())
    {
        bpmInfo = dsp::analyzeBpm (snap->original.data(),
                                   snap->numFrames,
                                   snap->numChannels,
                                   snap->sampleRate);
        keyInfo = dsp::analyzeKey (snap->original.data(),
                                   snap->numFrames,
                                   snap->numChannels,
                                   snap->sampleRate);
    }

    resized();
    repaint();
}

dsp::AudioFileIO::ExportFormat StemMixerPanel::currentFormat() const
{
    return formatProvider ? formatProvider()
                          : dsp::AudioFileIO::ExportFormat::Wav24;
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

    if (rows.empty())
    {
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::caption());
        g.drawText ("drop a file to begin", header, juce::Justification::centredRight);

        // Streamlined onboarding hint. The drop zone on the left already
        // says "drop audio to split"; we don't want to repeat the same
        // instruction here — instead point at the result of the next step.
        g.setColour (Theme::col (Theme::kTextSecondary));
        g.setFont (Theme::heading());
        g.drawText ("split a track to begin",
                    inner.withTrimmedBottom (inner.getHeight() / 2),
                    juce::Justification::centredBottom);

        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::body());
        g.drawText ("your stems, transport and per-stem mixer will appear here",
                    inner.withTrimmedTop (inner.getHeight() / 2),
                    juce::Justification::centredTop);
        return;
    }

    const bool playingOrig = session.playOriginal();

    // ---- Header right side: BPM/key caption (when room) + A/B pill ----
    if (! headerInfoBounds.isEmpty())
    {
        g.setColour (Theme::col (Theme::kTextTertiary));
        g.setFont (Theme::caption());

        const bool hasBpm = bpmInfo.has_value();
        const bool hasKey = keyInfo.has_value();
        juce::String info;
        if (hasBpm || hasKey)
        {
            if (hasBpm)
                info = juce::String (juce::roundToInt (bpmInfo->bpm)) + " BPM";
            if (hasKey)
            {
                if (info.isNotEmpty()) info += kMiddleDot;
                info += dsp::keyName (keyInfo);
            }
        }
        else
        {
            info = juce::String ((int) rows.size()) + " stems";
        }
        g.drawText (info, headerInfoBounds, juce::Justification::centredRight);
    }

    // Segmented A/B pill — header right edge.
    if (! abBounds.isEmpty())
    {
        const auto fullF = abBounds.toFloat();
        const float radius = Theme::kRadiusMedium;

        g.setColour (Theme::col (Theme::kSurface));
        g.fillRoundedRectangle (fullF, radius);

        const auto activeRect = playingOrig ? abOriginalBounds : abStemsBounds;
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;
            clip.addRoundedRectangle (fullF, radius);
            g.reduceClipRegion (clip);
            g.setColour (Theme::col (Theme::kAccent).withAlpha (0.18f));
            g.fillRect (activeRect.toFloat());
        }

        g.setColour (Theme::col (Theme::kBorder));
        const float dividerX = (float) abOriginalBounds.getX();
        g.fillRect (juce::Rectangle<float> (dividerX, fullF.getY() + 3.f,
                                            1.f, fullF.getHeight() - 6.f));

        g.setColour (Theme::col (Theme::kBorder));
        g.drawRoundedRectangle (fullF, radius, 1.f);

        g.setFont (Theme::caption());

        const auto stemsColor = (! playingOrig) ? Theme::col (Theme::kAccent)
                                                : Theme::col (Theme::kTextTertiary);
        const auto origColor  = playingOrig     ? Theme::col (Theme::kAccent)
                                                : Theme::col (Theme::kTextTertiary);

        g.setColour (stemsColor);
        g.drawText ("stems",    abStemsBounds,    juce::Justification::centred);
        g.setColour (origColor);
        g.drawText ("original", abOriginalBounds, juce::Justification::centred);
    }

    // (The "playing original" veil over the rows area is drawn in
    // paintOverChildren so it actually sits ON TOP of the StemRow
    // children — drawing it here would put it under them.)

    // ---- Footer: two drag pills ----
    auto drawPill = [&] (const juce::Rectangle<int>& r, const juce::String& label, bool armed)
    {
        const auto rF = r.toFloat();
        g.setColour (armed ? Theme::col (Theme::kSurfaceHi)
                           : Theme::col (Theme::kSurface));
        g.fillRoundedRectangle (rF, Theme::kRadiusMedium);
        g.setColour (Theme::col (Theme::kBorder));
        g.drawRoundedRectangle (rF, Theme::kRadiusMedium, 1.f);
        g.setColour (Theme::col (Theme::kTextPrimary));
        g.setFont (Theme::body());
        g.drawText (label, r, juce::Justification::centred);
    };

    drawPill (dragStemsBounds, "drag stems out", dragStemsArmed);
    drawPill (dragMixBounds,   "drag mix out",   dragMixArmed);

    // ---- Transient error caption ----
    const auto now = juce::Time::getMillisecondCounter();
    if (lastDragErrorAtMs != 0
        && (juce::int64) now - lastDragErrorAtMs < kErrorVisibleMs
        && lastDragError.isNotEmpty())
    {
        auto errArea = juce::Rectangle<int> (dragStemsBounds.getX(),
                                             dragStemsBounds.getBottom() + 2,
                                             dragMixBounds.getRight() - dragStemsBounds.getX(),
                                             14);
        g.setColour (Theme::col (Theme::kError));
        g.setFont (Theme::caption());
        g.drawText ("render failed: " + lastDragError, errArea,
                    juce::Justification::centred, true);
    }
}

void StemMixerPanel::paintOverChildren (juce::Graphics& g)
{
    // Veil the rows area when the user is listening to ORIGINAL — the
    // per-stem mute/solo/fader/drag controls have no audible effect in
    // that mode, so we dim them. This must run after children paint
    // (hence paintOverChildren) so the StemRow contents sit under the
    // veil. Header and footer remain fully opaque.
    if (! rows.empty() && session.playOriginal() && ! rowsArea.isEmpty())
    {
        g.setColour (Theme::col (Theme::kSurface).withAlpha (0.45f));
        g.fillRect (rowsArea);
    }
}

void StemMixerPanel::resized()
{
    auto inner = getLocalBounds().reduced (Theme::kPad);
    auto header = inner.removeFromTop (kHeaderH);

    // ---- Header layout: "stems" label on left, BPM/key caption + A/B
    //                      pill on right.
    header.removeFromLeft (200);   // "stems" heading occupies first 200 px

    // A/B pill on the right edge; only show if there's room and rows
    // exist. Below ~ (kAbPillW + tiny gap) we'd run into the left label.
    if (! rows.empty() && header.getWidth() >= kAbPillW + 8)
    {
        // Right-align the pill in the header, vertically centred.
        auto right = header.removeFromRight (kAbPillW);
        abBounds = right.withSizeKeepingCentre (kAbPillW, kAbPillH);

        const int abHalf = abBounds.getWidth() / 2;
        abStemsBounds    = abBounds.withWidth (abHalf);
        abOriginalBounds = abBounds.withTrimmedLeft (abHalf);

        // Gap between caption and pill.
        header.removeFromRight (Theme::kPadSm);

        // Whatever's left becomes the BPM/key caption area — but only
        // if it's actually wide enough to be useful.
        if (header.getWidth() >= kHeaderInfoMinW)
            headerInfoBounds = header;
        else
            headerInfoBounds = {};
    }
    else
    {
        abBounds = abStemsBounds = abOriginalBounds = {};
        headerInfoBounds = rows.empty() ? juce::Rectangle<int>{} : header;
    }

    if (rows.empty())
    {
        dragStemsBounds = {};
        dragMixBounds   = {};
        rowsArea        = {};
        return;
    }

    // ---- Footer: two drag pills, left half + right half ----
    auto footer = inner.removeFromBottom (kFooterH).reduced (0, Theme::kPadSm);
    const int half = (footer.getWidth() - Theme::kPad) / 2;
    dragStemsBounds = footer.removeFromLeft (half);
    footer.removeFromLeft (Theme::kPad);
    dragMixBounds   = footer.withWidth (half);

    inner.removeFromTop (Theme::kPadSm);
    inner.removeFromBottom (Theme::kPadSm);

    rowsArea = inner;

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

    // A/B pill (now in the header).
    if (! abStemsBounds.isEmpty() && abStemsBounds.contains (pos))
    {
        session.setPlayOriginal (false);
        if (! transport.isPlaying()) transport.play();
        repaint();
        return;
    }
    if (! abOriginalBounds.isEmpty() && abOriginalBounds.contains (pos))
    {
        session.setPlayOriginal (true);
        if (! transport.isPlaying()) transport.play();
        repaint();
        return;
    }

    // Footer drag pills — arm only; the actual drag fires from
    // mouseDrag once we've crossed the distance threshold. A plain
    // click (no drag) never triggers a drag, but it also never does
    // anything else here, which is the intended behaviour.
    if (dragStemsBounds.contains (pos)) { dragStemsArmed = true; repaint(); return; }
    if (dragMixBounds.contains (pos))   { dragMixArmed   = true; repaint(); return; }
}

void StemMixerPanel::mouseDrag (const juce::MouseEvent& e)
{
    if ((dragStemsArmed || dragMixArmed)
        && e.getDistanceFromDragStart() > kDragDistThreshold)
    {
        if (dragStemsArmed)
        {
            dragStemsArmed = false;
            repaint();
            requestDragAll();
        }
        else if (dragMixArmed)
        {
            dragMixArmed = false;
            repaint();
            requestDragMix();
        }
    }
}

void StemMixerPanel::mouseUp (const juce::MouseEvent&)
{
    // Clear armed flags if the user clicked but never crossed the
    // drag threshold — a click alone must not fire a drag.
    if (dragStemsArmed || dragMixArmed)
    {
        dragStemsArmed = false;
        dragMixArmed   = false;
        repaint();
    }
}

void StemMixerPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (dragStemsBounds.contains (pos))
        setTooltip ("Drag the raw separated stems out as a folder");
    else if (dragMixBounds.contains (pos))
        setTooltip ("Drag your current mixdown out (mute/solo/levels baked in)");
    else if (! abStemsBounds.isEmpty() && abStemsBounds.contains (pos))
        setTooltip ("Listen to the separated stems mix");
    else if (! abOriginalBounds.isEmpty() && abOriginalBounds.contains (pos))
        setTooltip ("Listen to the original input audio (A/B compare)");
    else
        setTooltip ({});
}

void StemMixerPanel::requestDragForStem (int idx)
{
    // DragExporter writes a temp WAV then triggers Windows OLE drag-and-
    // drop via performExternalDragDropOfFiles. Both can throw (bad_alloc,
    // disk full, OLE init failure). This function is called from a
    // mouseDrag event whose dispatcher is noexcept — an escaping
    // exception kills the host.
    try
    {
        auto snap = session.currentSnapshot();
        if (! snap) return;
        const auto base = std::filesystem::path (session.sourceFilePath()).stem().string();
        std::string err;
        if (! dsp::DragExporter::dragStem (this, *snap, idx, base, currentFormat(), &err)
            && ! err.empty())
        {
            flashError (juce::String (err));
        }
    }
    catch (...) { /* user can retry; don't crash the DAW */ }
}

void StemMixerPanel::requestDragAll()
{
    try
    {
        auto snap = session.currentSnapshot();
        if (! snap) return;
        const auto base = std::filesystem::path (session.sourceFilePath()).stem().string();
        std::string err;
        if (! dsp::DragExporter::dragAllStems (this, *snap, base, currentFormat(), &err)
            && ! err.empty())
        {
            flashError (juce::String (err));
        }
    }
    catch (...) { /* same */ }
}

void StemMixerPanel::requestDragMix()
{
    try
    {
        auto snap = session.currentSnapshot();
        if (! snap) return;
        const auto base = std::filesystem::path (session.sourceFilePath()).stem().string();
        std::string err;
        if (! dsp::DragExporter::dragMixdown (this, *snap, session.mixState(), base,
                                              currentFormat(), &err)
            && ! err.empty())
        {
            flashError (juce::String (err));
        }
    }
    catch (...) { /* same */ }
}

void StemMixerPanel::flashError (const juce::String& msg)
{
    lastDragError     = msg;
    lastDragErrorAtMs = (juce::int64) juce::Time::getMillisecondCounter();
    // Tick every ~500 ms during the visible window so the caption fades
    // itself on schedule even if nothing else triggers a repaint.
    if (! isTimerRunning())
        startTimer (500);
    repaint();
}

void StemMixerPanel::timerCallback()
{
    const auto now = (juce::int64) juce::Time::getMillisecondCounter();
    if (lastDragErrorAtMs == 0 || now - lastDragErrorAtMs >= kErrorVisibleMs)
    {
        lastDragError.clear();
        lastDragErrorAtMs = 0;
        stopTimer();
    }
    repaint();
}

} // namespace stemmerizer::ui
