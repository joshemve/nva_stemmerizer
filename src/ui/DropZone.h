#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace stemmerizer::ui
{

/// Big drag-and-drop target with an idle pulse animation, hover state, and
/// a "click to browse" affordance. Designed to be the visual centerpiece of
/// the plugin's empty state.
class DropZone : public juce::Component,
                 public juce::FileDragAndDropTarget,
                 public juce::SettableTooltipClient
{
public:
    DropZone();
    ~DropZone() override = default;

    /// Called from the editor's animation timer (~30 Hz).
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit  (const juce::StringArray& files)               override;
    void filesDropped  (const juce::StringArray& files, int x, int y) override;

    std::function<void(const juce::Array<juce::File>&)> onFilesDropped;
    std::function<void()>                               onClickToBrowse;

private:
    static bool isAcceptedAudio (const juce::String& path);

    float pulse         { 0.f };       // 0..2pi idle pulse phase
    float dashPhase     { 0.f };       // accumulates -> flows the dashed border
    float hoverGlow     { 0.f };       // eased toward 1 when mouse over
    float dragGlow      { 0.f };       // eased toward 1 when files dragged over
    bool  hovered       { false };
    bool  draggingOver  { false };
    bool  alwaysAnimate { true };      // always run the dash flow + breath
};

} // namespace stemmerizer::ui
