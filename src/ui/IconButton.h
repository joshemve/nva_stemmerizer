#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Compact 28×28 round-rect icon button. Uses vector glyphs (no font icons,
/// no PNGs) so it scales cleanly on every display.
class IconButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Glyph
    {
        Gear, Folder, Play, Pause, Stop, Cross, Check, Trash, Reveal,
        Mute, Solo, Plus, Reload
    };

    explicit IconButton (Glyph glyph);
    ~IconButton() override = default;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;

    void setGlyph (Glyph g) { glyph = g; repaint(); }
    void setActive (bool a) { active = a; repaint(); }
    bool isActive() const noexcept { return active; }

    void setSubtle (bool s) { subtle = s; repaint(); }

    std::function<void()> onClick;

private:
    void drawGlyph (juce::Graphics&, juce::Rectangle<float>, juce::Colour) const;

    Glyph glyph;
    bool  hovered { false };
    bool  pressed { false };
    bool  active  { false };
    bool  subtle  { false };
};

} // namespace stemmerizer::ui
