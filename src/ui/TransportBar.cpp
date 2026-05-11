#include "TransportBar.h"

#include <cmath>

namespace stemmerizer::ui
{

namespace
{
    constexpr int kButtonSize = 36;
    constexpr int kTimeWidth  = 210;
}

TransportBar::TransportBar (dsp::Transport& t) : transport (t)
{
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (timeLabel);

    timeLabel.setFont (Theme::mono());
    timeLabel.setColour (juce::Label::textColourId, Theme::col (Theme::kTextPrimary));
    timeLabel.setJustificationType (juce::Justification::centred);

    playButton.setTooltip ("Play / pause (Space)");
    stopButton.setTooltip ("Stop (Esc)");
    loopButton.setTooltip ("Toggle loop (L)");

    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this]
    {
        transport.stop();
        playButton.setGlyph (IconButton::Glyph::Play);
        repaint();
    };
    loopButton.onClick = [this] { toggleLoop(); };

    startTimerHz (30);
}

TransportBar::~TransportBar() = default;

void TransportBar::togglePlay()
{
    if (transport.isPlaying()) transport.pause();
    else                       transport.play();
    playButton.setGlyph (transport.isPlaying() ? IconButton::Glyph::Pause
                                               : IconButton::Glyph::Play);
    repaint();
}

void TransportBar::toggleLoop()
{
    const bool now = ! transport.isLoopOn();
    transport.setLoop (now);
    loopButton.setActive (now);
    repaint();
}

void TransportBar::timerCallback()
{
    // Keep the play-button glyph in sync with auto-stop at end of file.
    const auto wantsPlay = transport.isPlaying() ? IconButton::Glyph::Pause
                                                 : IconButton::Glyph::Play;
    playButton.setGlyph (wantsPlay);

    const int sr = std::max (1, transport.sampleRate());
    const auto pos = formatTime (transport.position(), sr);
    const auto len = formatTime (transport.length(),   sr);
    timeLabel.setText (pos + "  /  " + len, juce::dontSendNotification);
}

juce::String TransportBar::formatTime (long long sample, int sampleRate) const
{
    if (sampleRate <= 0) return "--:--.---";
    const double secs  = (double) sample / (double) sampleRate;
    const int    m     = (int) std::floor (secs / 60.0);
    const double rem   = secs - m * 60.0;
    const int    s     = (int) std::floor (rem);
    const int    ms    = (int) std::floor ((rem - s) * 1000.0);
    return juce::String::formatted ("%02d:%02d.%03d", m, s, ms);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.setColour (Theme::col (Theme::kSurface));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.f), Theme::kRadiusLarge);
    g.setColour (Theme::col (Theme::kBorder));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.f), Theme::kRadiusLarge, 1.f);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (Theme::kPad, Theme::kPadSm);

    playButton.setBounds (r.removeFromLeft (kButtonSize).withSizeKeepingCentre (kButtonSize, kButtonSize));
    r.removeFromLeft (Theme::kPadSm);
    stopButton.setBounds (r.removeFromLeft (kButtonSize).withSizeKeepingCentre (kButtonSize, kButtonSize));

    // Loop button anchored to the right edge.
    loopButton.setBounds (r.removeFromRight (kButtonSize).withSizeKeepingCentre (kButtonSize, kButtonSize));
    r.removeFromRight (Theme::kPad);

    // Centered combined timestamp.
    const int tlW = juce::jmin (kTimeWidth, r.getWidth());
    timeLabel.setBounds (r.withSizeKeepingCentre (tlW, r.getHeight()));
}

} // namespace stemmerizer::ui
