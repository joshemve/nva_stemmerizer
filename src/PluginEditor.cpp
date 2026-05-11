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

    constexpr int kWindowWidth  = 1180;
    constexpr int kWindowHeight = 760;

    // Compact settings strip height (label row + control row).
    constexpr int kSettingsStripH = 64;
    // When the window is narrow, stack settings into two rows.
    constexpr int kSettingsStripH2Row = 64 + 64 + ui::Theme::kPadSm;
    constexpr int kStackBelowPx = 1280;  // window width threshold for stacking
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
    processor.jobQueue().setChangeCallback ([this] { jobList.refresh(); });

    // ---- right side: player ----
    addAndMakeVisible (transport);
    addAndMakeVisible (loopRegion);
    addAndMakeVisible (mixer);

    sessionListener = processor.session().addListener ([this] { onSessionChanged(); });

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
    setLookAndFeel (nullptr);
}

void StemmerizerEditor::timerCallback()
{
    dropZone.tick();
}

void StemmerizerEditor::onSessionChanged()
{
    mixer.rebuild();
    repaint();
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
    return false;
}

void StemmerizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (col (ui::Theme::kBackground));

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

    // ---- body ----
    r.reduce (ui::Theme::kPad, ui::Theme::kPad);
    const int leftWidth = juce::jmax (380, r.getWidth() * 4 / 10);
    auto left  = r.removeFromLeft (leftWidth);
    r.removeFromLeft (ui::Theme::kPad);
    auto right = r;

    // ---- Left column: settings (top, compact) → drop zone (middle, hero)
    //                    → job queue (bottom).
    {
        // Decide between a single-row strip and a stacked two-row strip.
        const bool stack = getWidth() < kStackBelowPx;
        const int stripH = stack ? kSettingsStripH2Row : kSettingsStripH;

        auto strip = left.removeFromTop (stripH);
        left.removeFromTop (ui::Theme::kGap);

        // Job queue at the bottom.
        auto jobs = left.removeFromBottom (300);
        left.removeFromBottom (ui::Theme::kGap);
        jobList.setBounds (jobs);

        // Drop zone fills the middle — visual hero.
        dropZone.setBounds (left);

        // --- Settings strip layout ---
        constexpr int kLabelH = 14;
        constexpr int kCtrlH  = 38;
        constexpr int kGapY   = 4;

        if (! stack)
        {
            // One row, three inline groups: model | format | output (wide).
            const int totalW = strip.getWidth();
            const int gap    = ui::Theme::kPad;

            // ~22% model, ~22% format, rest output.
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
            // Stacked: row 1 = model + format, row 2 = output folder.
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

    // ---- Right column: transport (top), loop strip, mixer fills the rest. ----
    {
        transport.setBounds (right.removeFromTop (56));
        right.removeFromTop (ui::Theme::kPadSm);
        loopRegion.setBounds (right.removeFromTop (40));
        right.removeFromTop (ui::Theme::kGap);
        mixer.setBounds (right);
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
