#include "Theme.h"

namespace stemmerizer::ui
{

namespace
{
    // Resolved at first use, cached. We embed Inter via JUCE binary data;
    // see CMakeLists.txt -> juce_add_binary_data(StemmerizerData ...).
    juce::Typeface::Ptr loadTypeface (const void* data, size_t size)
    {
        if (data == nullptr || size == 0) return nullptr;
        return juce::Typeface::createSystemTypefaceFor (data, size);
    }
}

namespace Theme
{
    juce::Font display()
    {
        return juce::Font (juce::FontOptions ("Inter", 28.f, juce::Font::plain)
                              .withStyle ("SemiBold"));
    }
    juce::Font heading()
    {
        return juce::Font (juce::FontOptions ("Inter", 18.f, juce::Font::plain)
                              .withStyle ("SemiBold"));
    }
    juce::Font body()
    {
        return juce::Font (juce::FontOptions ("Inter", 14.f, juce::Font::plain));
    }
    juce::Font caption()
    {
        return juce::Font (juce::FontOptions ("Inter", 12.f, juce::Font::plain));
    }
    juce::Font mono()
    {
        return juce::Font (juce::FontOptions ("JetBrains Mono", 12.f, juce::Font::plain));
    }

    juce::Colour stemColor (int index, int totalStems) noexcept
    {
        if (totalStems <= 4)
        {
            switch (index)
            {
                case 0: return col (kDrums);
                case 1: return col (kBass);
                case 2: return col (kOther);
                case 3: return col (kVocals);
            }
        }
        else
        {
            switch (index)
            {
                case 0: return col (kDrums);
                case 1: return col (kBass);
                case 2: return col (kOther);
                case 3: return col (kVocals);
                case 4: return col (kGuitar);
                case 5: return col (kPiano);
            }
        }
        return col (kAccent);
    }
} // namespace Theme

// ============================================================================
StemmerizerLookAndFeel::StemmerizerLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId,  Theme::col (Theme::kBackground));
    setColour (juce::PopupMenu::backgroundColourId,        Theme::col (Theme::kSurface));
    setColour (juce::PopupMenu::textColourId,              Theme::col (Theme::kTextPrimary));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::col (Theme::kSurfaceHi));
    setColour (juce::PopupMenu::highlightedTextColourId,   Theme::col (Theme::kTextPrimary));

    setColour (juce::TextButton::buttonColourId,           Theme::col (Theme::kSurface));
    setColour (juce::TextButton::buttonOnColourId,         Theme::col (Theme::kAccent));
    setColour (juce::TextButton::textColourOnId,           juce::Colours::white);
    setColour (juce::TextButton::textColourOffId,          Theme::col (Theme::kTextPrimary));

    setColour (juce::Label::textColourId,                  Theme::col (Theme::kTextPrimary));

    setColour (juce::ComboBox::backgroundColourId,         Theme::col (Theme::kSurface));
    setColour (juce::ComboBox::textColourId,               Theme::col (Theme::kTextPrimary));
    setColour (juce::ComboBox::outlineColourId,            Theme::col (Theme::kBorder));
    setColour (juce::ComboBox::buttonColourId,             juce::Colours::transparentBlack);
    setColour (juce::ComboBox::arrowColourId,              Theme::col (Theme::kTextSecondary));

    setColour (juce::Slider::backgroundColourId,           Theme::col (Theme::kSurface));
    setColour (juce::Slider::trackColourId,                Theme::col (Theme::kBorderStrong));
    setColour (juce::Slider::thumbColourId,                Theme::col (Theme::kAccent));

    setColour (juce::ScrollBar::thumbColourId,             Theme::col (Theme::kBorderStrong));
}

juce::Font StemmerizerLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return Theme::body(); }
juce::Font StemmerizerLookAndFeel::getLabelFont (juce::Label&)               { return Theme::body(); }
juce::Font StemmerizerLookAndFeel::getComboBoxFont (juce::ComboBox&)         { return Theme::body(); }

void StemmerizerLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                   const juce::Colour& bg,
                                                   bool isOver, bool isDown)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool primary = b.getToggleState() || b.getProperties().getWithDefault ("primary", false);

    juce::Colour fill;
    if (primary)
        fill = isDown ? Theme::col (Theme::kAccent).darker (0.1f)
             : isOver ? Theme::col (Theme::kAccentHover)
             :          Theme::col (Theme::kAccent);
    else
        fill = isDown ? Theme::col (Theme::kSurfaceHi).brighter (0.05f)
             : isOver ? Theme::col (Theme::kSurfaceHi)
             :          Theme::col (Theme::kSurface);

    g.setColour (fill);
    g.fillRoundedRectangle (r, Theme::kRadiusMedium);

    if (! primary)
    {
        g.setColour (Theme::col (Theme::kBorder));
        g.drawRoundedRectangle (r, Theme::kRadiusMedium, 1.f);
    }
}

void StemmerizerLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool,
                                           int, int, int, int, juce::ComboBox&)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);

    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (r, Theme::kRadiusMedium);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (r, Theme::kRadiusMedium, 1.f);

    // Chevron
    juce::Path chev;
    const float cx = (float) w - 18.f, cy = (float) h * 0.5f;
    chev.startNewSubPath (cx - 4.f, cy - 2.f);
    chev.lineTo          (cx,        cy + 2.f);
    chev.lineTo          (cx + 4.f, cy - 2.f);
    g.setColour (Theme::col (Theme::kTextSecondary));
    g.strokePath (chev, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void StemmerizerLookAndFeel::positionComboBoxText (juce::ComboBox& cb, juce::Label& lbl)
{
    lbl.setBounds (Theme::kPad, 0, cb.getWidth() - Theme::kPad - 28, cb.getHeight());
    lbl.setFont (Theme::body());
    lbl.setColour (juce::Label::textColourId, Theme::col (Theme::kTextPrimary));
}

void StemmerizerLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                                               float sliderPos, float, float,
                                               juce::Slider::SliderStyle style, juce::Slider& s)
{
    juce::ignoreUnused (style);

    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float midY  = bounds.getCentreY();
    const float trackH = 4.f;

    // Track bg
    juce::Rectangle<float> track (bounds.getX(), midY - trackH * 0.5f,
                                  bounds.getWidth(), trackH);
    g.setColour (Theme::col (Theme::kBorderStrong));
    g.fillRoundedRectangle (track, trackH * 0.5f);

    // Track fill
    juce::Rectangle<float> fill (bounds.getX(), midY - trackH * 0.5f,
                                 sliderPos - bounds.getX(), trackH);
    auto accent = s.findColour (juce::Slider::thumbColourId);
    g.setColour (accent);
    g.fillRoundedRectangle (fill, trackH * 0.5f);

    // Thumb
    const float thumbR = 8.f;
    g.setColour (juce::Colours::white);
    g.fillEllipse (sliderPos - thumbR, midY - thumbR, thumbR * 2.f, thumbR * 2.f);
    g.setColour (accent);
    g.fillEllipse (sliderPos - thumbR + 2.f, midY - thumbR + 2.f,
                   (thumbR - 2.f) * 2.f, (thumbR - 2.f) * 2.f);
}

void StemmerizerLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&,
                                          float x, float y, float w, float h,
                                          bool ticked, bool, bool isHover, bool)
{
    const auto box = juce::Rectangle<float> (x, y, w, h).reduced (1.f);
    const float r  = 4.f;

    g.setColour (ticked ? Theme::col (Theme::kAccent)
                        : (isHover ? Theme::col (Theme::kSurfaceHi) : Theme::col (Theme::kSurface)));
    g.fillRoundedRectangle (box, r);

    g.setColour (ticked ? Theme::col (Theme::kAccent) : Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (box, r, 1.f);

    if (ticked)
    {
        juce::Path check;
        check.startNewSubPath (box.getX() + box.getWidth() * 0.22f,
                               box.getY() + box.getHeight() * 0.52f);
        check.lineTo          (box.getX() + box.getWidth() * 0.42f,
                               box.getY() + box.getHeight() * 0.72f);
        check.lineTo          (box.getX() + box.getWidth() * 0.78f,
                               box.getY() + box.getHeight() * 0.30f);
        g.setColour (juce::Colours::white);
        g.strokePath (check, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
}

void StemmerizerLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w,
                                           const juce::Rectangle<int>& textArea,
                                           juce::TextLayout& tl)
{
    g.fillAll (Theme::col (Theme::kBackground));
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (w.getLocalBounds().toFloat().reduced (1.f), Theme::kRadiusLarge);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (w.getLocalBounds().toFloat().reduced (1.f), Theme::kRadiusLarge, 1.f);
    tl.draw (g, textArea.toFloat());
}

} // namespace stemmerizer::ui
