#include "PluginEditor.h"

#include <fstream>

namespace stemmerizer
{

namespace
{
    using ui::Theme::col;

    /// Append-only diagnostic log at %APPDATA%/Stemmerizer/crash.log.
    /// Used to record what the plugin tried to do at every failure point —
    /// so even when an error dialog shows, the user can paste a real trail
    /// of the underlying technical detail to support.
    void crashlog (const juce::String& msg)
    {
        const auto path = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("Stemmerizer").getChildFile ("crash.log");
        path.getParentDirectory().createDirectory();
        std::ofstream f (path.getFullPathName().toStdString(), std::ios::app);
        if (f)
        {
            f << juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S").toStdString()
              << "  " << msg.toStdString() << "\n";
            f.flush();
        }
    }

    // Default window dimensions. Was 1180×760 — felt sparse in the
    // empty state. 1080×640 lands in a more compact, intentional
    // composition while still leaving room for the stems mixer hero
    // once a session loads.
    constexpr int kWindowWidth  = 1080;
    constexpr int kWindowHeight = 640;

    // Compact settings strip height (label row + control row).
    constexpr int kSettingsStripH = 64;
    // When the window is narrow, stack settings into two rows.
    constexpr int kSettingsStripH2Row = 64 + 64 + ui::Theme::kPadSm;
    // Lowered from 1280 -> 900 so the default 1180-wide window keeps the
    // settings on a single horizontal row (model | format | output). Two
    // stacked rows made the empty state feel top-heavy with very little
    // useful information to show.
    constexpr int kStackBelowPx = 900;

    // Empty-state drop-zone "card" dimensions. We constrain the bordered
    // drop area to these values and centre it inside the body — without
    // the cap the box stretches across 1100×600 px of dead surface,
    // which reads as overwhelming rather than welcoming.
    constexpr int kEmptyDropMaxW = 760;
    constexpr int kEmptyDropMaxH = 420;

    // Loaded-state metrics.
    constexpr int kContextStripH      = 52;     // collapsed summary row
    constexpr int kLoadedTransportH   = 56;
    constexpr int kLoadedLoopH        = 36;
    constexpr int kLoadedJobStripH    = 80;     // compact joblist when active
    constexpr int kEditPillW          = 56;     // "edit" affordance hit-area width
    constexpr int kEditPillH          = 24;
}

StemmerizerEditor::StemmerizerEditor (StemmerizerProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      transport  (p.transport()),
      loopRegion (p.transport()),
      mixer      (p.session(), p.transport())
{
    setLookAndFeel (&laf);
    setResizable (true, true);
    setResizeLimits (1000, 620, 2400, 1500);
    setSize (kWindowWidth, kWindowHeight);

    // Allow keyboard transport shortcuts to land here.
    setWantsKeyboardFocus (true);

    // Mark the editor opaque — paint() fills every pixel with the
    // background gradient below. Without this, JUCE assumes we may be
    // transparent and clears each invalidated region to the host
    // window's background between repaints. During a fast drag of the
    // bottom-right resize corner that shows up as a visible flash on
    // the newly-exposed strip on every resize tick.
    setOpaque (true);

    // ---- header ----
    titleLabel.setFont (ui::Theme::heading());
    titleLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextPrimary));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    versionLabel.setText ("v" STEMMERIZER_VERSION_STRING, juce::dontSendNotification);
    versionLabel.setFont (ui::Theme::caption());
    versionLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextTertiary));
    versionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (versionLabel);

    addAndMakeVisible (folderButton);
    folderButton.setTooltip ("Reveal output folder");
    folderButton.onClick = [this]
    {
        const juce::File f (processor.state().getProperty ("outputDir").toString());
        if (f.isDirectory()) f.revealToUser();
    };
    // settingsButton intentionally omitted — no settings sheet exists yet.

    // ---- drop zone ----
    addAndMakeVisible (dropZone);
    dropZone.onFilesDropped = [this] (const juce::Array<juce::File>& fs) { onFilesDropped (fs); };
    dropZone.onClickToBrowse = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Choose audio file(s)", juce::File(),
            "*.wav;*.flac;*.mp3;*.aif;*.aiff;*.ogg");
        auto* raw = chooser.get();
        raw->launchAsync (juce::FileBrowserComponent::openMode |
                          juce::FileBrowserComponent::canSelectFiles |
                          juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, chooser] (const juce::FileChooser& c)
        {
            // Async completion runs on JUCE's message thread via a
            // noexcept trampoline. Belt-and-braces try/catch — even
            // though onFilesDropped is already wrapped internally,
            // a raw juce::Array copy CAN throw bad_alloc and that
            // path runs before onFilesDropped's own try/catch.
            try
            {
                juce::Array<juce::File> picked;
                for (const auto& r : c.getResults()) picked.add (r);
                if (! picked.isEmpty()) onFilesDropped (picked);
            }
            catch (...) { /* swallow — file chooser results unusable */ }
        });
    };

    // ---- model selector ----
    modelLabel.setFont (ui::Theme::caption());
    modelLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextSecondary));
    addAndMakeVisible (modelLabel);

    modelSelector.addItem (juce::String::fromUTF8 ("4-stem \xc2\xb7 fast"),            1);
    modelSelector.addItem (juce::String::fromUTF8 ("4-stem \xc2\xb7 high quality"),    2);
    modelSelector.addItem (juce::String::fromUTF8 ("6-stem \xc2\xb7 +guitar / piano"), 3);
    {
        const auto curr = processor.state().getProperty ("model").toString();
        modelSelector.setSelectedId (curr == "htdemucs_ft" ? 2 : curr == "htdemucs_6s" ? 3 : 1,
                                     juce::dontSendNotification);
    }
    modelSelector.onChange = [this]
    {
        const auto id = modelSelector.getSelectedId();
        const char* key = id == 2 ? "htdemucs_ft" : id == 3 ? "htdemucs_6s" : "htdemucs";
        processor.state().setProperty ("model", key, nullptr);
    };
    modelSelector.setTooltip ("Choose stem split model");
    addAndMakeVisible (modelSelector);

    // ---- format selector ----
    formatLabel.setFont (ui::Theme::caption());
    formatLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextSecondary));
    addAndMakeVisible (formatLabel);

    formatSelector.addItem (juce::String::fromUTF8 ("WAV \xc2\xb7 24-bit"),       1);
    formatSelector.addItem (juce::String::fromUTF8 ("WAV \xc2\xb7 16-bit"),       2);
    formatSelector.addItem (juce::String::fromUTF8 ("WAV \xc2\xb7 32-bit float"), 3);
    formatSelector.addItem ("FLAC",                                               4);
    formatSelector.addItem (juce::String::fromUTF8 ("MP3 \xc2\xb7 add-on"),       5);
    formatSelector.setSelectedId (1, juce::dontSendNotification);
    formatSelector.onChange = [this]
    {
        const char* k = "wav24";
        switch (formatSelector.getSelectedId())
        {
            case 2: k = "wav16";  break;
            case 3: k = "wav32f"; break;
            case 4: k = "flac";   break;
            case 5: k = "mp3";    break;
            default: break;
        }
        processor.state().setProperty ("exportFormat", k, nullptr);
    };
    formatSelector.setTooltip ("Output audio format");
    addAndMakeVisible (formatSelector);

    // ---- output folder ----
    outputLabel.setFont (ui::Theme::caption());
    outputLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextSecondary));
    addAndMakeVisible (outputLabel);

    outputPath.setFont (ui::Theme::mono());
    outputPath.setBaseColour (col (ui::Theme::kTextPrimary));
    outputPath.setMinimumHorizontalScale (1.f);
    outputPath.setText (processor.state().getProperty ("outputDir").toString(),
                        juce::dontSendNotification);
    outputPath.setTooltip (processor.state().getProperty ("outputDir").toString());
    outputPath.onClicked = [this] { browseOutputDir(); };
    addAndMakeVisible (outputPath);

    addAndMakeVisible (jobList);
    jobList.setQueue (&processor.jobQueue());
    // When jobs come and go, also re-run the editor layout so the queue
    // panel grows / shrinks / disappears based on activity (see resized()
    // for the rules — empty -> hidden, 1 job -> slim strip, 2+ -> full).
    //
    // Two safety hooks (audit N1, N3):
    //   * SafePointer guard — setChangeCallback(nullptr) in ~Editor only
    //     stops FUTURE notifies; it can't recall callAsync lambdas that
    //     are already on the message queue with `this` baked in. A
    //     captured Component::SafePointer becomes null once we destruct,
    //     so a late-fire is a no-op instead of a use-after-free.
    //   * Only relayout when the total job count actually changes —
    //     this callback fires on every progress tick during inference,
    //     and a full editor-wide resized() per tick is a perf hazard.
    processor.jobQueue().setChangeCallback (
        [safe = juce::Component::SafePointer<StemmerizerEditor> (this)]
        {
            auto* self = safe.getComponent();
            if (self == nullptr) return;
            const int before = self->jobList.totalJobCount();
            self->jobList.refresh();
            if (self->jobList.totalJobCount() != before)
                self->resized();
            // Worker writes stems AFTER the finishedCallback fires (see
            // JobQueue.cpp). Re-running the bar layout when the job state
            // transitions to Done picks up the now-on-disk files so the
            // card no longer shows "missing files" for the short window
            // between session-handoff and the encode completing.
            self->refreshRecentBar();
        });

    // ---- recent splits strip ----
    addChildComponent (recentBar);   // hidden until refreshRecentBar() finds entries
    recentBar.onRemoveRequested = [this] (const juce::String& id)
    {
        processor.recentProjects().remove (id);
        // The remove() call fires onChanged on this (message) thread, which
        // hops via callAsync back into refreshRecentBar.
    };
    recentBar.onClearAll = [this]
    {
        processor.recentProjects().clear();
    };
    // RecentProjects::add/remove/clear all fire onChanged on whatever thread
    // touched the list — that can be the JobQueue worker via the finished-
    // callback path. SafePointer the editor so a queued callAsync after
    // teardown is a no-op, and never reach into the recentBar member from a
    // raw `this` capture.
    processor.recentProjects().onChanged =
        [safe = juce::Component::SafePointer<StemmerizerEditor> (this)]
        {
            juce::MessageManager::callAsync ([safe]
            {
                if (auto* self = safe.getComponent())
                    self->refreshRecentBar();
            });
        };
    refreshRecentBar();

    // ---- right side: player ----
    addAndMakeVisible (transport);
    addAndMakeVisible (loopRegion);
    addAndMakeVisible (mixer);

    // The mixer's drag pills need the currently-selected export format to
    // pass through to DragExporter. We read it lazily from the processor
    // ValueTree so any later format-selector change is picked up without
    // wiring a separate listener here.
    mixer.formatProvider = [this]
    {
        const auto k = processor.state().getProperty ("exportFormat").toString();
        if (k == "wav16")  return dsp::AudioFileIO::ExportFormat::Wav16;
        if (k == "wav32f") return dsp::AudioFileIO::ExportFormat::Wav32f;
        if (k == "flac")   return dsp::AudioFileIO::ExportFormat::Flac;
        if (k == "mp3")    return dsp::AudioFileIO::ExportFormat::Mp3;
        return dsp::AudioFileIO::ExportFormat::Wav24;
    };

    // Same SafePointer pattern (audit N1) — session.notifyChanged copies
    // listeners into an async lambda BEFORE the editor's destructor calls
    // removeListener, so already-queued asyncs would otherwise re-enter
    // a freed editor.
    sessionListener = processor.session().addListener (
        [safe = juce::Component::SafePointer<StemmerizerEditor> (this)]
        {
            if (auto* self = safe.getComponent())
                self->onSessionChanged();
        });

    startTimerHz (30);
}

StemmerizerEditor::~StemmerizerEditor()
{
    // Order matters: stop the timer first so no late tick fires after
    // children start destroying. Then drop every back-edge from the
    // AudioProcessor's long-lived members to us.
    stopTimer();
    if (sessionListener   != 0) processor.session().removeListener (sessionListener);
    if (transportListener != 0) processor.transport().removeListener (transportListener);
    processor.jobQueue().setChangeCallback (nullptr);
    processor.recentProjects().onChanged = nullptr;
    setLookAndFeel (nullptr);
}

void StemmerizerEditor::timerCallback()
{
    dropZone.tick();
}

void StemmerizerEditor::onSessionChanged()
{
    // Layout flips between "no session" (drop zone full-width, no right
    // column) and "session loaded" (split body). The mixer also needs to
    // rebuild its rows whenever the snapshot changes.
    mixer.rebuild();

    // The recents strip filters out the now-loaded session — refresh so
    // the filter picks up the new sourceFilePath() (or clears it on
    // session teardown). refreshRecentBar() itself calls resized() if the
    // strip's visibility changes; we still resized() unconditionally
    // below so the empty-vs-loaded body layout flips.
    refreshRecentBar();

    resized();
    repaint();
}

void StemmerizerEditor::refreshRecentBar()
{
    auto entries = processor.recentProjects().entries();

    // Push the current source path into the bar BEFORE setEntries so the
    // filter runs in setEntries itself (the cached path is a member,
    // setCurrentInputPath is a no-op when unchanged).
    recentBar.setCurrentInputPath (
        juce::String (processor.session().sourceFilePath()));

    recentBar.setEntries (std::move (entries));

    // We have to recompute visibility from the bar's post-filter state,
    // not the raw fetched entries — otherwise we'd briefly show an empty
    // bar when the only recent IS the now-loaded project.
    const bool show = ! recentBar.getEntries().empty();
    if (recentBar.isVisible() != show)
    {
        recentBar.setVisible (show);
        resized();   // bar appearing/disappearing changes the body layout
    }
    else
    {
        recentBar.repaint();
    }
}

bool StemmerizerEditor::hasSession() const noexcept
{
    return processor.session().isLoaded();
}

bool StemmerizerEditor::isAcceptedAudioPath (const juce::String& path)
{
    // Mirror DropZone::isAcceptedAudio so dropping outside the (hidden)
    // dropzone still works in the loaded layout.
    const auto ext = juce::File (path).getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".flac" || ext == ".mp3"
        || ext == ".aif" || ext == ".aiff" || ext == ".ogg";
}

bool StemmerizerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAcceptedAudioPath (f)) return true;
    return false;
}

void StemmerizerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> picked;
    for (const auto& f : files)
        if (isAcceptedAudioPath (f)) picked.add (juce::File (f));
    if (! picked.isEmpty()) onFilesDropped (picked);
}

void StemmerizerEditor::mouseUp (const juce::MouseEvent& e)
{
    // Only the loaded-state context strip has clickable hit areas owned
    // by the editor (the empty-state widgets are all real child
    // components). The strip's "edit" pill toggles settingsExpanded.
    if (! hasSession()) return;
    if (e.mouseWasDraggedSinceMouseDown()) return;
    if (contextStripEditHit.isEmpty()) return;
    if (contextStripEditHit.contains (e.getPosition()))
    {
        settingsExpanded = ! settingsExpanded;
        applyStateVisibility (true);
        resized();
        repaint();
    }
}

void StemmerizerEditor::applyStateVisibility (bool sessionLoaded)
{
    // Single place that decides which top-level widgets are visible per
    // state. resized() calls this first so every later setBounds() lands
    // on a component whose visibility matches its layout intent.
    //
    //   Empty state: dropzone hero + the full settings strip controls.
    //   Loaded state (collapsed): no dropzone, no labels/selectors —
    //     they collapse into the context-strip summary text.
    //   Loaded state (expanded): selectors come back; the strip grows.
    const bool showEmpty       = ! sessionLoaded;
    const bool showFullControls = ! sessionLoaded || settingsExpanded;

    dropZone     .setVisible (showEmpty);
    modelLabel   .setVisible (showFullControls);
    formatLabel  .setVisible (showFullControls);
    outputLabel  .setVisible (showFullControls);
    modelSelector.setVisible (showFullControls);
    formatSelector.setVisible (showFullControls);
    outputPath   .setVisible (showFullControls);

    transport .setVisible (sessionLoaded);
    loopRegion.setVisible (sessionLoaded);
    mixer     .setVisible (sessionLoaded);
}

juce::String StemmerizerEditor::settingsSummary() const
{
    // Build the single-line summary used in the loaded-state context
    // strip. Mirrors the wording of the dropdowns so the user maps
    // between collapsed and expanded views without thinking.
    const auto modelKey  = processor.state().getProperty ("model").toString();
    const auto formatKey = processor.state().getProperty ("exportFormat").toString();
    const auto outPath   = processor.state().getProperty ("outputDir").toString();

    juce::String modelStr =
        modelKey == "htdemucs_ft" ? juce::String::fromUTF8 ("4-stem \xc2\xb7 high quality")
      : modelKey == "htdemucs_6s" ? juce::String::fromUTF8 ("6-stem \xc2\xb7 +guitar / piano")
      :                             juce::String::fromUTF8 ("4-stem \xc2\xb7 fast");

    juce::String fmtStr =
        formatKey == "wav16"  ? juce::String::fromUTF8 ("WAV \xc2\xb7 16-bit")
      : formatKey == "wav32f" ? juce::String::fromUTF8 ("WAV \xc2\xb7 32-bit float")
      : formatKey == "flac"   ? juce::String ("FLAC")
      : formatKey == "mp3"    ? juce::String ("MP3")
      :                         juce::String::fromUTF8 ("WAV \xc2\xb7 24-bit");

    // Path is rendered separately (mono font) by paintContextStrip — we
    // return it joined here only for non-rendering callers / debugging.
    juce::ignoreUnused (outPath);
    return modelStr + "    " + fmtStr;
}

void StemmerizerEditor::paintContextStrip (juce::Graphics& g, juce::Rectangle<int> strip)
{
    if (strip.isEmpty()) return;

    // ---- background --------------------------------------------------
    {
        const auto stripF = strip.toFloat().reduced (0.5f);
        g.setColour (col (ui::Theme::kSurface));
        g.fillRoundedRectangle (stripF, ui::Theme::kRadiusMedium);
        g.setColour (col (ui::Theme::kBorder));
        g.drawRoundedRectangle (stripF, ui::Theme::kRadiusMedium, 1.f);
    }

    // The pill (edit / done) is drawn in BOTH variants — its hit area is
    // already cached by resized() in the expanded case, by the block
    // below in the collapsed case.
    {
        const juce::String pillText = settingsExpanded ? "done" : "edit";
        auto inner = strip.reduced (ui::Theme::kPad, 0);
        const int pillY = inner.getCentreY() - kEditPillH / 2;

        if (! settingsExpanded)
        {
            contextStripEditHit = juce::Rectangle<int> (
                inner.getRight() - kEditPillW, pillY, kEditPillW, kEditPillH);
        }
        // Otherwise resized() already populated contextStripEditHit.

        const auto pillF = contextStripEditHit.toFloat();
        g.setColour (col (ui::Theme::kSurfaceHi));
        g.fillRoundedRectangle (pillF, kEditPillH * 0.5f);
        g.setColour (col (ui::Theme::kTextSecondary));
        g.setFont (ui::Theme::caption());
        g.drawText (pillText, contextStripEditHit,
                    juce::Justification::centred, false);
    }

    // When expanded, the real ComboBox + Label children sit in the strip
    // and own the rendering of the controls — we just painted the
    // background and the pill, so we're done.
    if (settingsExpanded) return;

    auto inner = strip.reduced (ui::Theme::kPad, 0);
    inner.removeFromRight (kEditPillW + ui::Theme::kPad);

    // Left cluster: model · format    /path/...
    // Approach: lay out left-to-right with explicit measured widths so
    // the model+format pair stays anchored on the left and the path
    // grows / shrinks against whatever room remains, truncated from
    // the LEFT (tail-visible) when it can't fit.
    const auto summary = settingsSummary();   // "model · variant    FMT"
    const auto path    = processor.state().getProperty ("outputDir").toString();

    const auto bodyFont    = ui::Theme::body();
    const auto monoFont    = ui::Theme::mono();
    const auto captionFont = ui::Theme::caption();

    const int leftTextY = inner.getY();
    const int leftTextH = inner.getHeight();

    // Draw the summary (body font, primary color).
    g.setFont (bodyFont);
    g.setColour (col (ui::Theme::kTextPrimary));
    const auto summaryW = (int) juce::GlyphArrangement::getStringWidth (
                              bodyFont, summary) + 4;
    auto summaryRect = juce::Rectangle<int> (
        inner.getX(), leftTextY, summaryW, leftTextH);
    g.drawText (summary, summaryRect, juce::Justification::centredLeft, false);

    // Path on the right of the summary, with leading ellipsis if needed.
    auto pathRect = juce::Rectangle<int> (
        summaryRect.getRight() + ui::Theme::kPad, leftTextY,
        inner.getRight() - (summaryRect.getRight() + ui::Theme::kPad),
        leftTextH);

    if (pathRect.getWidth() > 24 && path.isNotEmpty())
    {
        const auto ell = juce::String::fromUTF8 ("\xe2\x80\xa6");
        auto displayed = path;
        const float maxW = (float) pathRect.getWidth();
        // Left-truncate: keep the tail of the path visible.
        if (juce::GlyphArrangement::getStringWidth (monoFont, displayed) > maxW)
        {
            while (displayed.length() > 1
                   && juce::GlyphArrangement::getStringWidth (
                          monoFont, ell + displayed) > maxW)
                displayed = displayed.substring (1);
            displayed = ell + displayed;
        }
        g.setFont (monoFont);
        g.setColour (col (ui::Theme::kTextSecondary));
        g.drawText (displayed, pathRect, juce::Justification::centredLeft, false);
    }

    juce::ignoreUnused (captionFont);
}

bool StemmerizerEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)
    {
        transport.togglePlay();
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        processor.transport().stop();
        return true;
    }
    if (k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L'
        || k.getKeyCode() == 'L')
    {
        transport.toggleLoop();
        return true;
    }
    if (k.getTextCharacter() == 'a' || k.getTextCharacter() == 'A'
        || k.getKeyCode() == 'A')
    {
        // Flip the A/B compare. Auto-start playback if paused — a silent
        // toggle gives the user no feedback that anything happened, and
        // pressing 'A' clearly signals an intent to hear the difference.
        auto& sess = processor.session();
        sess.setPlayOriginal (! sess.playOriginal());
        if (! processor.transport().isPlaying()) processor.transport().play();
        mixer.repaint();
        return true;
    }
    return false;
}

void StemmerizerEditor::paint (juce::Graphics& g)
{
    // Single opaque gradient pass covers the entire local bounds — the
    // gradient endpoints are both kBackground (alpha=ff), so no fillAll
    // pre-pass is needed. setOpaque(true) in the ctor confirms to JUCE
    // we own every pixel here.
    juce::ColourGradient grad (col (ui::Theme::kBackground).brighter (0.02f), 0.f, 0.f,
                               col (ui::Theme::kBackground),                  0.f, (float) getHeight(),
                               false);
    g.setGradientFill (grad);
    g.fillRect (getLocalBounds());

    auto h = getLocalBounds();
    auto headerArea = h.removeFromTop (ui::Theme::kHeaderHeight);
    g.setColour (col (ui::Theme::kBorder));
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1,
                headerArea.getWidth(), 1);

    // In the loaded state the context strip's background and summary
    // text are owned by the editor (no child component), so paint here.
    // contextStripPaintBounds is populated by resized() so we don't
    // re-derive geometry; if it's empty (empty state or no session) the
    // helper bails out cleanly.
    if (hasSession())
        paintContextStrip (g, contextStripPaintBounds);
}

void StemmerizerEditor::resized()
{
    const bool sessionLoaded = hasSession();

    // Reset cached hit-areas — only the active layout populates them.
    contextStripEditHit     = {};
    contextStripPaintBounds = {};

    // Centralised visibility update; every child below assumes its
    // visibility matches the current state.
    applyStateVisibility (sessionLoaded);

    auto r = getLocalBounds();

    // ---- header ----
    auto header = r.removeFromTop (ui::Theme::kHeaderHeight).reduced (ui::Theme::kPad, 0);
    titleLabel.setBounds   (header.removeFromLeft (160).withTrimmedTop (16).withTrimmedBottom (16));
    versionLabel.setBounds (header.removeFromLeft (60).withTrimmedTop (20).withTrimmedBottom (16));
    folderButton.setBounds (header.removeFromRight (36).withSizeKeepingCentre (28, 28));

    // ---- recent splits strip (full-width, between header and body) ----
    // In the loaded state the bar is *filtered* to exclude the current
    // session — see refreshRecentBar(). Visibility is owned there too,
    // so we just check isVisible() here.
    if (recentBar.isVisible())
    {
        constexpr int kRecentBarH = 80;
        auto barArea = r.removeFromTop (kRecentBarH).reduced (ui::Theme::kPad, 0);
        recentBar.setBounds (barArea);
        r.removeFromTop (ui::Theme::kGap);
    }

    // ---- body ----
    r.reduce (ui::Theme::kPad, ui::Theme::kPad);

    if (sessionLoaded)
    {
        // ============================================================
        // LOADED LAYOUT — mixer-hero, full-width.
        //
        //   [ context strip: settings summary  …  edit ]
        //   [ transport (play / stop / loop / time)    ]
        //   [ loop region                              ]
        //   [ mixer (fills the rest)                   ]
        //   [ joblist strip — only if active jobs > 0  ]
        // ============================================================

        // ---- 1) context strip ------------------------------------------
        // Collapsed: 52 px summary band. Expanded: full settings strip
        // height with the real ComboBox + Label children inside.
        const int collapsedH = kContextStripH;
        const int expandedH  = kSettingsStripH;
        const int stripH     = settingsExpanded ? expandedH : collapsedH;

        auto strip = r.removeFromTop (stripH);
        r.removeFromTop (ui::Theme::kGap);
        contextStripPaintBounds = strip;   // paint() will draw bg + text

        if (settingsExpanded)
        {
            // Same layout as the empty-state inline strip, single row.
            // We just place the controls here; paintContextStrip() only
            // paints the panel background under them.
            auto controls = strip.reduced (ui::Theme::kPad, 0);

            constexpr int kLabelH = 14;
            constexpr int kCtrlH  = 38;
            constexpr int kGapY   = 4;

            const int totalW  = controls.getWidth();
            const int gap     = ui::Theme::kPad;
            const int modelW  = juce::jmax (160, totalW * 22 / 100);
            const int formatW = juce::jmax (160, totalW * 22 / 100);

            // Reserve the right edge for the "edit" pill so the user can
            // collapse the expanded view back to the summary.
            const int outW = totalW - modelW - formatW - gap * 3 - kEditPillW;

            auto modelArea  = controls.removeFromLeft (modelW);
            controls.removeFromLeft (gap);
            auto formatArea = controls.removeFromLeft (formatW);
            controls.removeFromLeft (gap);
            auto outArea    = controls.removeFromLeft (juce::jmax (40, outW));

            const auto place = [&] (juce::Rectangle<int> area,
                                    juce::Label& lbl, juce::Component& ctrl)
            {
                lbl.setBounds  (area.removeFromTop (kLabelH));
                area.removeFromTop (kGapY);
                ctrl.setBounds (area.removeFromTop (kCtrlH));
            };
            place (modelArea,  modelLabel,  modelSelector);
            place (formatArea, formatLabel, formatSelector);
            place (outArea,    outputLabel, outputPath);

            // Edit pill on the far right — mouseUp() reads
            // contextStripEditHit to know where the "collapse" hit area
            // lives in the expanded variant too.
            controls.removeFromLeft (gap);
            const int pillY = strip.getCentreY() - kEditPillH / 2;
            contextStripEditHit = juce::Rectangle<int> (
                strip.getRight() - kEditPillW - ui::Theme::kPad,
                pillY, kEditPillW, kEditPillH);
            // (No paint here; paintContextStrip handles the pill, but
            // when expanded it bails out — so paint a faint outline so
            // the user can still see the click target.)
        }
        // else: collapsed — paintContextStrip() draws the summary text
        // and the "edit" pill, populates contextStripEditHit itself.

        // ---- 2) transport (full-width row directly under the strip) ----
        transport.setBounds (r.removeFromTop (kLoadedTransportH));
        r.removeFromTop (ui::Theme::kPadSm);

        // ---- 3) loop region --------------------------------------------
        loopRegion.setBounds (r.removeFromTop (kLoadedLoopH));
        r.removeFromTop (ui::Theme::kGap);

        // ---- 5) joblist strip (active-only, compact, only if jobs) -----
        // Configure the joblist FIRST so activeJobCount() reflects the
        // post-filter state for this layout.
        jobList.setShowDoneJobs (false);
        jobList.setCompactMode  (true);
        jobList.refresh();   // re-apply the filter against the current queue
        const int activeJobs = jobList.activeJobCount();
        const bool showJobs  = activeJobs > 0;
        jobList.setVisible (showJobs);

        if (showJobs)
        {
            auto jobs = r.removeFromBottom (kLoadedJobStripH);
            r.removeFromBottom (ui::Theme::kGap);
            jobList.setBounds (jobs);
        }
        else
        {
            jobList.setBounds ({});
        }

        // ---- 4) mixer fills ALL remaining vertical space --------------
        mixer.setBounds (r);

        // Components used only in the empty layout collapse to zero
        // bounds (visibility is already false from applyStateVisibility).
        dropZone.setBounds ({});
        if (! settingsExpanded)
        {
            modelLabel    .setBounds ({});
            formatLabel   .setBounds ({});
            outputLabel   .setBounds ({});
            modelSelector .setBounds ({});
            formatSelector.setBounds ({});
            outputPath    .setBounds ({});
        }
    }
    else
    {
        // ============================================================
        // EMPTY LAYOUT — "drop-zone hero" (original design).
        // ============================================================

        // ---- Settings strip: now a FULL-WIDTH band at the top of the
        // body. Was previously inside the left column, which forced the
        // column to be ~470 px wide just to fit "model · format · output
        // folder" on a single row. Lifting it out lets the body split
        // below get aggressive (260 px sidebar / ~75% hero) without
        // cramping the dropdowns.
        {
            const bool stack  = getWidth() < kStackBelowPx;
            const int  stripH = stack ? kSettingsStripH2Row : kSettingsStripH;

            auto strip = r.removeFromTop (stripH);
            r.removeFromTop (ui::Theme::kGap);

            constexpr int kLabelH = 14;
            constexpr int kCtrlH  = 38;
            constexpr int kGapY   = 4;

            if (! stack)
            {
                const int totalW = strip.getWidth();
                const int gap    = ui::Theme::kPad;
                const int modelW  = juce::jmax (160, totalW * 22 / 100);
                const int formatW = juce::jmax (160, totalW * 22 / 100);
                const int outW    = totalW - modelW - formatW - gap * 2;

                auto modelArea  = strip.removeFromLeft (modelW);
                strip.removeFromLeft (gap);
                auto formatArea = strip.removeFromLeft (formatW);
                strip.removeFromLeft (gap);
                auto outArea    = strip.withWidth (outW);

                const auto place = [&] (juce::Rectangle<int> area,
                                        juce::Label& lbl, juce::Component& ctrl)
                {
                    lbl.setBounds (area.removeFromTop (kLabelH));
                    area.removeFromTop (kGapY);
                    ctrl.setBounds (area.removeFromTop (kCtrlH));
                };

                place (modelArea,  modelLabel,  modelSelector);
                place (formatArea, formatLabel, formatSelector);
                place (outArea,    outputLabel, outputPath);
            }
            else
            {
                auto row1 = strip.removeFromTop (kSettingsStripH);
                strip.removeFromTop (ui::Theme::kPadSm);
                auto row2 = strip;

                const int half = (row1.getWidth() - ui::Theme::kPad) / 2;
                auto modelArea  = row1.removeFromLeft (half);
                row1.removeFromLeft (ui::Theme::kPad);
                auto formatArea = row1;

                modelLabel .setBounds (modelArea.removeFromTop (kLabelH));
                modelArea.removeFromTop (kGapY);
                modelSelector.setBounds (modelArea.removeFromTop (kCtrlH));

                formatLabel.setBounds (formatArea.removeFromTop (kLabelH));
                formatArea.removeFromTop (kGapY);
                formatSelector.setBounds (formatArea.removeFromTop (kCtrlH));

                outputLabel.setBounds (row2.removeFromTop (kLabelH));
                row2.removeFromTop (kGapY);
                outputPath.setBounds (row2.removeFromTop (kCtrlH));
            }
        }

        // Empty-state JobList rules: show everything (incl. Done rows)
        // since there's no recents bar pickup happening yet.
        jobList.setShowDoneJobs (true);

        // Drop zone fills the whole body. Queue strip is bottom-anchored
        // when there are any jobs in the queue.
        auto left = r;

        const int totalJobs  = jobList.totalJobCount();
        const bool compact   = (totalJobs == 1);
        const bool hidden    = (totalJobs == 0);

        jobList.setCompactMode (compact);
        jobList.setVisible (! hidden);

        if (! hidden)
        {
            const int queueH = compact ? 92 : 300;
            auto jobs = left.removeFromBottom (queueH);
            left.removeFromBottom (ui::Theme::kGap);
            jobList.setBounds (jobs);
        }
        else
        {
            jobList.setBounds ({});
        }

        // Centred hero card capped at 760×420 so the bordered drop area
        // doesn't stretch to fill 1100×600 of surface.
        const int dropW = juce::jmin (kEmptyDropMaxW, left.getWidth());
        const int dropH = juce::jmin (kEmptyDropMaxH, left.getHeight());
        const int dropX = left.getX() + (left.getWidth()  - dropW) / 2;
        const int dropY = left.getY() + (left.getHeight() - dropH) / 2;
        dropZone.setBounds (dropX, dropY, dropW, dropH);

        // Loaded-only widgets collapse to zero bounds (visibility is
        // already false from applyStateVisibility).
        transport .setBounds ({});
        loopRegion.setBounds ({});
        mixer     .setBounds ({});
    }
}

void StemmerizerEditor::onFilesDropped (const juce::Array<juce::File>& files)
{
    // Last-line-of-defence try/catch. Anything below (enqueueFile -> the
    // DSP backend) may throw an Ort::Exception or std::exception. If the
    // exception escapes us, JUCE's message-proc trampoline is effectively
    // noexcept and Windows raises FAST_FAIL_FATAL_APP_EXIT (0xc0000409),
    // killing the entire DAW. Show a clean error dialog instead.
    try
    {
        for (const auto& f : files)
            if (f.existsAsFile()) enqueueFile (f);
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withTitle ("Stemmerizer couldn't queue that file")
                .withMessage (juce::String ("An error occurred while preparing the split:\n\n")
                              + e.what())
                .withButton ("OK"),
            nullptr);
    }
    catch (...)
    {
        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withTitle ("Stemmerizer couldn't queue that file")
                .withMessage ("An unknown error occurred while preparing the split.")
                .withButton ("OK"),
            nullptr);
    }
}

void StemmerizerEditor::enqueueFile (const juce::File& f)
{
    dsp::Job job;
    job.inputPath  = f.getFullPathName().toStdString();
    job.outputDir  = processor.state().getProperty ("outputDir").toString().toStdString();
    job.exportFormat = processor.state().getProperty ("exportFormat").toString().toStdString();

    const auto modelKey = processor.state().getProperty ("model").toString();
    job.options.model = modelKey == "htdemucs_ft" ? dsp::ModelVariant::Htdemucs4StemFt
                       : modelKey == "htdemucs_6s" ? dsp::ModelVariant::Htdemucs6Stem
                       :                             dsp::ModelVariant::Htdemucs4Stem;

    const int n = dsp::numStems (job.options.model);
    job.enabledStems.assign ((size_t) n, true);

    std::string err;
    const auto weightsDir = processor.resolveWeightsDir().getFullPathName().toStdString();
    if (! processor.jobQueue().ensureModel (weightsDir, job.options.model, err))
    {
        // Surface the actual technical reason in the dialog AND in
        // crash.log. With the host-stability fixes in place, every failure
        // path returns a meaningful err string instead of crashing —
        // the only way the user is going to be unstuck is by seeing it.
        const juce::String detail = juce::String (err);
        crashlog ("[ensureModel] searched=\"" + juce::String (weightsDir)
                   + "\"  detail=\"" + detail + "\"");

        // Decide title based on whether the technical error is a missing
        // file ("Weights file not found: ...") or something deeper.
        const bool weightsMissing = detail.containsIgnoreCase ("weights file not found")
                                 || weightsDir.empty();

        const juce::String title = weightsMissing
            ? "AI models not installed"
            : "Stemmerizer couldn't initialise the AI runtime";

        juce::String body;
        if (weightsMissing)
        {
            body =
                "Stemmerizer needs its AI models installed before it can split audio.\n\n"
                "The models are about 270 MB and only need to be downloaded once.\n"
                "Please contact support or re-run the installer to fetch them.\n\n"
                "Searched: " + juce::String (weightsDir) + "\n"
                "Details:  " + detail;
        }
        else
        {
            body =
                "Stemmerizer couldn't load the AI runtime in this session.\n"
                "Please send the line below to support — it pinpoints what went wrong:\n\n"
                + detail
                + "\n\nWeights dir: " + juce::String (weightsDir);
        }

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withTitle (title)
                .withMessage (body)
                .withButton ("OK"),
            nullptr);
        return;
    }

    processor.jobQueue().enqueue (std::move (job));
}

void StemmerizerEditor::browseOutputDir()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Choose output folder",
        juce::File (processor.state().getProperty ("outputDir").toString()));
    auto* raw = chooser.get();
    raw->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                      [this, chooser] (const juce::FileChooser& c)
    {
        // FileChooser's async completion lambda runs on JUCE's message
        // thread via a noexcept trampoline. If anything inside throws
        // (ValueTree reallocation, Label setText on stale component,
        // etc.) it escapes the trampoline and kills the host. Guard.
        try
        {
            const auto r = c.getResult();
            if (r.isDirectory())
            {
                processor.state().setProperty ("outputDir", r.getFullPathName(), nullptr);
                outputPath.setText (r.getFullPathName(), juce::dontSendNotification);
                outputPath.setTooltip (r.getFullPathName());
            }
        }
        catch (...) { /* user picked a weird path; ignore */ }
    });
}

} // namespace stemmerizer
