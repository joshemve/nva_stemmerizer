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
    resized();
    repaint();
}

void StemmerizerEditor::refreshRecentBar()
{
    auto entries = processor.recentProjects().entries();
    const bool show = ! entries.empty();
    recentBar.setEntries (std::move (entries));
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
}

void StemmerizerEditor::resized()
{
    auto r = getLocalBounds();

    // ---- header ----
    auto header = r.removeFromTop (ui::Theme::kHeaderHeight).reduced (ui::Theme::kPad, 0);
    titleLabel.setBounds   (header.removeFromLeft (160).withTrimmedTop (16).withTrimmedBottom (16));
    versionLabel.setBounds (header.removeFromLeft (60).withTrimmedTop (20).withTrimmedBottom (16));
    folderButton.setBounds (header.removeFromRight (36).withSizeKeepingCentre (28, 28));

    // ---- recent splits strip (full-width, between header and body) ----
    // Only visible if we have at least one entry — collapses to zero height
    // otherwise so the empty state isn't pushed down by dead chrome.
    if (recentBar.isVisible())
    {
        constexpr int kRecentBarH = 80;
        auto barArea = r.removeFromTop (kRecentBarH).reduced (ui::Theme::kPad, 0);
        recentBar.setBounds (barArea);
        r.removeFromTop (ui::Theme::kGap);
    }

    // ---- body ----
    r.reduce (ui::Theme::kPad, ui::Theme::kPad);

    const bool sessionLoaded = processor.session().isLoaded();
    transport .setVisible (sessionLoaded);
    loopRegion.setVisible (sessionLoaded);
    mixer     .setVisible (sessionLoaded);

    // ---- Settings strip: now a FULL-WIDTH band at the top of the body.
    // Was previously inside the left column, which forced the column to
    // be ~470 px wide just to fit "model · format · output folder" on a
    // single row. Lifting it out lets the body split below get aggressive
    // (260 px sidebar / ~75% hero) without cramping the dropdowns.
    {
        const bool stack = getWidth() < kStackBelowPx;
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

            const auto place = [&] (juce::Rectangle<int> area, juce::Label& lbl, juce::Component& ctrl)
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

    // ---- Body split: sidebar (drop + queue) | hero (transport + mixer) ----
    juce::Rectangle<int> left;
    juce::Rectangle<int> right;

    if (sessionLoaded)
    {
        // Stems are the centerpiece — give them ~75% of the body.
        // Sidebar is just for the drop-another affordance + queue strip.
        const int sidebarW = juce::jlimit (220, 280, r.getWidth() / 4);
        left  = r.removeFromLeft (sidebarW);
        r.removeFromLeft (ui::Theme::kPad);
        right = r;
    }
    else
    {
        // No session yet — drop zone fills the whole body. Right column
        // doesn't exist on screen until the first split completes.
        left  = r;
        right = {};
    }

    // ---- Left column: drop zone (top/middle) + queue strip (bottom) ----
    {
        // Queue state -> panel height.
        const int totalJobs  = jobList.totalJobCount();
        const int activeJobs = jobList.activeJobCount();
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

        // Drop zone bounds:
        //   * Session loaded  -> compact "drop another?" affordance at
        //     the top of the sidebar (max 220 px).
        //   * Empty state     -> centred "hero card" capped at 760×420 so
        //     the bordered drop area doesn't stretch to fill 1100×600 of
        //     surface (which read as overwhelming, per audit feedback).
        if (sessionLoaded)
        {
            const int dropH = juce::jmin (220, left.getHeight());
            auto drop = left.removeFromTop (dropH);
            dropZone.setBounds (drop);
        }
        else
        {
            const int dropW = juce::jmin (kEmptyDropMaxW, left.getWidth());
            const int dropH = juce::jmin (kEmptyDropMaxH, left.getHeight());
            const int dropX = left.getX() + (left.getWidth()  - dropW) / 2;
            const int dropY = left.getY() + (left.getHeight() - dropH) / 2;
            dropZone.setBounds (dropX, dropY, dropW, dropH);
        }

        juce::ignoreUnused (activeJobs);   // reserved for future badge UI
    }

    // ---- Right column (hero): transport, loop, stems mixer ----
    if (sessionLoaded && right.getWidth() > 0)
    {
        transport.setBounds (right.removeFromTop (56));
        right.removeFromTop (ui::Theme::kPadSm);
        loopRegion.setBounds (right.removeFromTop (56));
        right.removeFromTop (ui::Theme::kGap);
        // Stems mixer fills everything else and gets the lion's share of
        // the body real estate — that's the user's primary work surface
        // once a split is loaded.
        mixer.setBounds (right);
    }
    else
    {
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
