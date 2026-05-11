#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/DropZone.h"
#include "ui/StemMixerPanel.h"
#include "ui/TransportBar.h"
#include "ui/LoopRegionView.h"
#include "ui/JobList.h"
#include "ui/IconButton.h"
#include "ui/RecentBar.h"

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

    // Keyboard transport — Space / Esc / L.
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void onFilesDropped (const juce::Array<juce::File>&);
    void enqueueFile (const juce::File&);
    void browseOutputDir();
    void onSessionChanged();   // called when the in-memory session updates
    void refreshRecentBar();   // pulls fresh entries from processor.recentProjects()

    StemmerizerProcessor& processor;

    ui::StemmerizerLookAndFeel laf;

    // ---- Header ----
    juce::Label    titleLabel    { {}, "STEMMERIZER" };
    juce::Label    versionLabel;
    // NOTE: settingsButton is intentionally not instantiated/added yet — no
    // settings panel exists. Re-enable here and in PluginEditor.cpp once a
    // settings sheet is built.
    ui::IconButton folderButton   { ui::IconButton::Glyph::Folder };

    // ---- Left column: settings / drop / job queue ----
    ui::DropZone   dropZone;
    juce::ComboBox modelSelector;
    juce::ComboBox formatSelector;
    juce::Label    modelLabel  { {}, "model"  };
    juce::Label    formatLabel { {}, "format" };
    juce::Label    outputLabel { {}, "output folder" };
    // outputPath is a clickable label — see PathLabel below.
    ui::JobList    jobList;

    // ---- Right column: in-plugin player ----
    ui::TransportBar    transport;
    ui::LoopRegionView  loopRegion;
    ui::StemMixerPanel  mixer;

    // ---- Recent splits strip (between header and body) ----
    ui::RecentBar       recentBar;

    // ---------------------------------------------------------------
    // Tiny clickable label that routes mouseUp to a callback. Lives
    // inside the editor TU so we don't need a separate component pair.
    // Used to make the output-folder path itself the affordance, rather
    // than carrying a separate "Choose..." button.
    // ---------------------------------------------------------------
    class PathLabel : public juce::Label
    {
    public:
        std::function<void()> onClicked;
        PathLabel()
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setInterceptsMouseClicks (true, false);
        }
        void mouseEnter (const juce::MouseEvent&) override
        {
            hovered = true;
            applyColours();
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            hovered = false;
            applyColours();
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasDraggedSinceMouseDown() && onClicked) onClicked();
        }
        void setBaseColour (juce::Colour c) { baseColour = c; applyColours(); }
    private:
        void applyColours()
        {
            setColour (juce::Label::textColourId,
                       hovered ? baseColour.brighter (0.35f) : baseColour);
            repaint();
        }
        juce::Colour baseColour { juce::Colours::white };
        bool hovered { false };
    };

    PathLabel outputPath;

    // Listener handles — we MUST deregister these in ~Editor or the
    // backing AudioProcessor (which outlives the editor in every host)
    // will hold dangling lambdas capturing `this`. That's how FL Studio
    // crashed when reopening the plugin window: the destroyed editor's
    // listener was still in the session's vector and fired on next
    // notify, dereferencing freed memory at offset 0x18.
    dsp::StemSession::ListenerHandle sessionListener { 0 };
    dsp::Transport::ListenerHandle   transportListener { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StemmerizerEditor)
};

} // namespace stemmerizer
