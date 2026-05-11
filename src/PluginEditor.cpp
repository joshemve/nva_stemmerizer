#include "PluginEditor.h"

namespace stemmerizer
{

namespace
{
    using ui::Theme::col;

    constexpr int kWindowWidth  = 1180;
    constexpr int kWindowHeight = 760;
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
    addAndMakeVisible (settingsButton);
    folderButton.setTooltip ("Reveal output folder");
    folderButton.onClick = [this]
    {
        const juce::File f (processor.state().getProperty ("outputDir").toString());
        if (f.isDirectory()) f.revealToUser();
    };
    settingsButton.setTooltip ("Settings");

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
            juce::Array<juce::File> picked;
            for (const auto& r : c.getResults()) picked.add (r);
            if (! picked.isEmpty()) onFilesDropped (picked);
        });
    };

    // ---- model selector ----
    modelLabel.setFont (ui::Theme::caption());
    modelLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextSecondary));
    addAndMakeVisible (modelLabel);

    modelSelector.addItem ("4-Stem  |  fast",            1);
    modelSelector.addItem ("4-Stem  |  high quality",    2);
    modelSelector.addItem ("6-Stem  |  +guitar/piano",   3);
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
    addAndMakeVisible (modelSelector);

    // ---- format selector ----
    formatLabel.setFont (ui::Theme::caption());
    formatLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextSecondary));
    addAndMakeVisible (formatLabel);

    formatSelector.addItem ("WAV  |  24-bit",    1);
    formatSelector.addItem ("WAV  |  16-bit",    2);
    formatSelector.addItem ("WAV  |  32f",       3);
    formatSelector.addItem ("FLAC",              4);
    formatSelector.addItem ("MP3  |  add-on",    5);
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
    addAndMakeVisible (formatSelector);

    // ---- output folder ----
    outputLabel.setFont (ui::Theme::caption());
    outputLabel.setColour (juce::Label::textColourId, col (ui::Theme::kTextSecondary));
    addAndMakeVisible (outputLabel);

    outputPath.setFont (ui::Theme::mono());
    outputPath.setColour (juce::Label::textColourId, col (ui::Theme::kTextPrimary));
    outputPath.setMinimumHorizontalScale (1.f);
    outputPath.setText (processor.state().getProperty ("outputDir").toString(),
                        juce::dontSendNotification);
    addAndMakeVisible (outputPath);

    chooseOutput.onClick = [this] { browseOutputDir(); };
    addAndMakeVisible (chooseOutput);

    addAndMakeVisible (jobList);
    jobList.setQueue (&processor.jobQueue());
    processor.jobQueue().setChangeCallback ([this] { jobList.refresh(); });

    // ---- right side: player ----
    addAndMakeVisible (transport);
    addAndMakeVisible (loopRegion);
    addAndMakeVisible (mixer);

    processor.session().addListener ([this] { onSessionChanged(); });

    startTimerHz (30);
}

StemmerizerEditor::~StemmerizerEditor()
{
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
    settingsButton.setBounds (header.removeFromRight (36).withSizeKeepingCentre (28, 28));
    header.removeFromRight (8);
    folderButton.setBounds   (header.removeFromRight (36).withSizeKeepingCentre (28, 28));

    // ---- body ----
    r.reduce (ui::Theme::kPad, ui::Theme::kPad);
    const int leftWidth = juce::jmax (380, r.getWidth() * 4 / 10);
    auto left  = r.removeFromLeft (leftWidth);
    r.removeFromLeft (ui::Theme::kPad);
    auto right = r;

    // Left: drop zone (top, large), then settings (model/format/output),
    // then job queue (bottom).
    {
        auto controlsAndJobs = left.removeFromBottom (340);
        left.removeFromBottom (ui::Theme::kPad);
        dropZone.setBounds (left);

        auto settingsArea = controlsAndJobs.removeFromTop (180);
        controlsAndJobs.removeFromTop (ui::Theme::kPad);
        jobList.setBounds (controlsAndJobs);

        auto row = settingsArea.removeFromTop (66);
        auto modelArea  = row.removeFromLeft (row.getWidth() / 2 - ui::Theme::kPadSm);
        row.removeFromLeft (ui::Theme::kPad);
        auto formatArea = row;

        modelLabel .setBounds (modelArea.removeFromTop (18));
        modelArea.removeFromTop (4);
        modelSelector.setBounds (modelArea.removeFromTop (38));

        formatLabel.setBounds (formatArea.removeFromTop (18));
        formatArea.removeFromTop (4);
        formatSelector.setBounds (formatArea.removeFromTop (38));

        settingsArea.removeFromTop (ui::Theme::kPad);
        auto outRow = settingsArea.removeFromTop (66);
        outputLabel.setBounds (outRow.removeFromTop (18));
        outRow.removeFromTop (4);
        auto outControls = outRow.removeFromTop (38);
        chooseOutput.setBounds (outControls.removeFromRight (110));
        outControls.removeFromRight (ui::Theme::kPadSm);
        outputPath.setBounds (outControls);
    }

    // Right: transport (top), loop region (under), mixer (rest).
    {
        transport .setBounds (right.removeFromTop (56));
        right.removeFromTop (ui::Theme::kPadSm);
        loopRegion.setBounds (right.removeFromTop (28));
        right.removeFromTop (ui::Theme::kPad);
        mixer.setBounds (right);
    }
}

void StemmerizerEditor::onFilesDropped (const juce::Array<juce::File>& files)
{
    for (const auto& f : files)
        if (f.existsAsFile()) enqueueFile (f);
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
        const juce::String shownDir = weightsDir.empty()
            ? juce::String ("(not found in any expected location)")
            : juce::String (weightsDir);

        const juce::String body =
            juce::String ("Stemmerizer needs the AI model weights, and they aren't installed yet.\n\n"
                          "To get them, run this once from the project root:\n\n"
                          "    python scripts/fetch_weights.py --all\n\n"
                          "First run downloads ~1.2 GB from Meta's public CDN and converts to "
                          "~270 MB of ggml in resources/weights/. Then restart the plugin.\n\n"
                          "Searched location:\n  ")
            + shownDir
            + juce::String ("\n\nDetails: ") + juce::String (err);

        juce::AlertWindow::showAsync (
            juce::MessageBoxOptions()
                .withTitle ("Model weights not installed yet")
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
        const auto r = c.getResult();
        if (r.isDirectory())
        {
            processor.state().setProperty ("outputDir", r.getFullPathName(), nullptr);
            outputPath.setText (r.getFullPathName(), juce::dontSendNotification);
        }
    });
}

} // namespace stemmerizer
