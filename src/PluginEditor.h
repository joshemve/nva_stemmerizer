#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/DropZone.h"
#include "ui/StemMixerPanel.h"
#include "ui/TransportBar.h"
#include "ui/LoopRegionView.h"
#include "ui/JobList.h"
#include "ui/IconButton.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace stemmerizer
{

class StemmerizerEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit StemmerizerEditor (StemmerizerProcessor&);
    ~StemmerizerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void onFilesDropped (const juce::Array<juce::File>&);
    void enqueueFile (const juce::File&);
    void browseOutputDir();
    void onSessionChanged();   // called when the in-memory session updates

    StemmerizerProcessor& processor;

    ui::StemmerizerLookAndFeel laf;

    // ---- Header ----
    juce::Label    titleLabel    { {}, "STEMMERIZER" };
    juce::Label    versionLabel;
    ui::IconButton settingsButton { ui::IconButton::Glyph::Gear   };
    ui::IconButton folderButton   { ui::IconButton::Glyph::Folder };

    // ---- Left column: drop / settings / job queue ----
    ui::DropZone   dropZone;
    juce::ComboBox modelSelector;
    juce::ComboBox formatSelector;
    juce::Label    modelLabel  { {}, "model"  };
    juce::Label    formatLabel { {}, "format" };
    juce::Label    outputLabel { {}, "output folder" };
    juce::Label    outputPath;
    juce::TextButton chooseOutput { "Choose..." };
    ui::JobList    jobList;

    // ---- Right column: in-plugin player ----
    ui::TransportBar    transport;
    ui::LoopRegionView  loopRegion;
    ui::StemMixerPanel  mixer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemmerizerEditor)
};

} // namespace stemmerizer
