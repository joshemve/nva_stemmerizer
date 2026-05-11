#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace stemmerizer::ui
{

/// Single source of truth for all colors, spacings, type sizes. Anything
/// touching pixels reads from here. To re-skin the plugin: change this file,
/// rebuild, done.
namespace Theme
{
    // ---- palette --------------------------------------------------------
    // -------------------------------------------------------------------
    // Palette: "Midnight Studio"
    // -------------------------------------------------------------------
    // Design intent (revised after first-pass user feedback that the
    // violet+sky+pastel palette read as "AI generated"):
    //
    //   * Pure neutral darks (no hue tint on background/surfaces) — the
    //     plugin should feel like a piece of professional studio gear,
    //     not a Tailwind dashboard.
    //   * Off-white text with a faint warm cream cast so it doesn't feel
    //     sterile / lab-lit on a black surface.
    //   * Single warm accent (copper / analog meter glow) — distinctive,
    //     audio-coded, not the violet that every AI tool defaults to.
    //   * Stem colors hand-tuned as a muted earth-tone family: still
    //     mutually distinguishable, but they read as a curated set
    //     rather than rainbow saturation maxed out.
    // -------------------------------------------------------------------

    // Surfaces — true neutral, no hue.
    inline constexpr juce::uint32 kBackground       = 0xff0b0b0b;   // app surface
    inline constexpr juce::uint32 kSurface          = 0xff141414;   // elevated panels
    inline constexpr juce::uint32 kSurfaceHi        = 0xff1c1c1c;   // hover / active
    inline constexpr juce::uint32 kBorder           = 0x12ffffff;   // ~7% white
    inline constexpr juce::uint32 kBorderStrong     = 0x26ffffff;   // ~15% white

    // Text — slight warm cream tint for body, neutral grays for the rest.
    inline constexpr juce::uint32 kTextPrimary      = 0xfff1ede5;   // warm off-white
    inline constexpr juce::uint32 kTextSecondary    = 0xff8e8a82;   // warm gray
    inline constexpr juce::uint32 kTextTertiary     = 0xff5a5752;   // muted warm gray
    inline constexpr juce::uint32 kTextDisabled     = 0xff2e2c29;

    // Single accent — copper / analog warmth. Used sparingly: faders,
    // active toggles, progress bars, focus rings.
    inline constexpr juce::uint32 kAccent           = 0xffd4843a;   // copper
    inline constexpr juce::uint32 kAccentHover      = 0xffe49454;
    inline constexpr juce::uint32 kAccentMuted      = 0x80d4843a;

    // Stems — muted, harmonious. Distinguishable but feels curated, not
    // "I picked every Tailwind-500 swatch."
    inline constexpr juce::uint32 kVocals           = 0xffc89c8a;   // warm rose-tan
    inline constexpr juce::uint32 kDrums            = 0xffcfa365;   // dusty gold
    inline constexpr juce::uint32 kBass             = 0xff89a98a;   // sage
    inline constexpr juce::uint32 kOther            = 0xff7d95ad;   // slate blue (muted)
    inline constexpr juce::uint32 kGuitar           = 0xffb37a6a;   // terracotta
    inline constexpr juce::uint32 kPiano            = 0xff9d89a8;   // muted plum

    // Semantic — calmer than the old neon trio.
    inline constexpr juce::uint32 kSuccess          = 0xff7ba386;
    inline constexpr juce::uint32 kError            = 0xffc06868;
    inline constexpr juce::uint32 kWarning          = 0xffc4a05c;

    // ---- metrics --------------------------------------------------------
    inline constexpr float kRadiusSmall  = 6.f;
    inline constexpr float kRadiusMedium = 10.f;
    inline constexpr float kRadiusLarge  = 16.f;

    inline constexpr int kPad   = 16;
    inline constexpr int kPadSm = 8;
    inline constexpr int kGap   = 12;

    inline constexpr int kRowHeight    = 44;
    inline constexpr int kHeaderHeight = 56;

    // ---- typography -----------------------------------------------------
    juce::Font display();   // 28 pt, semibold — h1 / drop zone hint
    juce::Font heading();   // 18 pt, semibold — section titles
    juce::Font body();      // 14 pt, regular  — most labels
    juce::Font caption();   // 12 pt, regular  — secondary labels
    juce::Font mono();      // 12 pt, mono     — file paths, timings

    // ---- accessor helpers ----------------------------------------------
    inline juce::Colour col (juce::uint32 rgba) noexcept { return juce::Colour (rgba); }

    /// Stem color by index, given the active model variant size (4 or 6).
    juce::Colour stemColor (int index, int totalStems) noexcept;

    /// Animation easing — "expo out", standard 2020s motion language.
    inline float easeOut (float t) noexcept
    {
        t = juce::jlimit (0.f, 1.f, t);
        return 1.f - std::pow (1.f - t, 3.f);
    }
} // namespace Theme

// ============================================================================
// LookAndFeel — owns rendering of standard JUCE controls. Custom components
// (DropZone, StemMixer, etc.) use Theme directly.
// ============================================================================
class StemmerizerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StemmerizerLookAndFeel();
    ~StemmerizerLookAndFeel() override = default;

    juce::Font getTextButtonFont (juce::TextButton&, int /*buttonHeight*/) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool isOver, bool isDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawTickBox (juce::Graphics&, juce::Component&,
                      float x, float y, float w, float h,
                      bool ticked, bool isEnabled, bool isHover, bool isDown) override;

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&,
                       const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
};

} // namespace stemmerizer::ui
