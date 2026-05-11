#include "TransportBar.h"

#include <cmath>

namespace stemmerizer::ui
{

TransportBar::TransportBar (dsp::Transport& t) : transport (t)
{
    addAndMakeVisible (playButton);
    addAndMakeVisible (stopButton);
    addAndMakeVisible (loopButton);
    addAndMakeVisible (currentTime);
    addAndMakeVisible (totalTime);
    addAndMakeVisible (loopBadge);

    currentTime.setFont (Theme::mono());
    totalTime  .setFont (Theme::mono());
    currentTime.setColour (juce::Label::textColourId, Theme::col (Theme::kTextPrimary));
    totalTime  .setColour (juce::Label::textColourId, Theme::col (Theme::kTextTertiary));
    currentTime.setJustificationType (juce::Justification::centredLeft);
    totalTime  .setJustificationType (juce::Justification::centredRight);

    loopBadge.setFont (Theme::caption());
    loopBadge.setColour (juce::Label::textColourId, Theme::col (Theme::kTextTertiary));
    loopBadge.setJustificationType (juce::Justification::centred);
    loopBadge.setText ("LOOP OFF", juce::dontSendNotification);

    playButton.onClick = [this]
    {
        if (transport.isPlaying()) transport.pause();
        else                       transport.play();
        playButton.setGlyph (transport.isPlaying() ? IconButton::Glyph::Pause
                                                   : IconButton::Glyph::Play);
        repaint();
    };
    stopButton.onClick = [this]
    {
        transport.stop();
        playButton.setGlyph (IconButton::Glyph::Play);
        repaint();
    };
    loopButton.onClick = [this]
    {
        const bool now = ! transport.isLoopOn();
        transport.setLoop (now);
        loopButton.setActive (now);
        loopBadge.setText (now ? "LOOP ON" : "LOOP OFF", juce::dontSendNotification);
        loopBadge.setColour (juce::Label::textColourId,
                             now ? Theme::col (Theme::kAccent)
                                 : Theme::col (Theme::kTextTertiary));
        repaint();
    };

    startTimerHz (30);
}

TransportBar::~TransportBar() = default;

void TransportBar::timerCallback()
{
    // Keep the play-button glyph in sync with auto-stop at end of file.
    const auto wantsPlay = transport.isPlaying() ? IconButton::Glyph::Pause
                                                 : IconButton::Glyph::Play;
    playButton.setGlyph (wantsPlay);

    const int sr = std::max (1, transport.sampleRate());
    currentTime.setText (formatTime (transport.position(), sr), juce::dontSendNotification);
    totalTime  .setText (formatTime (transport.length(),   sr), juce::dontSendNotification);
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

    playButton.setBounds (r.removeFromLeft (32).withSizeKeepingCentre (32, 32));
    r.removeFromLeft (Theme::kPadSm);
    stopButton.setBounds (r.removeFromLeft (32).withSizeKeepingCentre (32, 32));
    r.removeFromLeft (Theme::kPad);

    currentTime.setBounds (r.removeFromLeft (110));
    r.removeFromLeft (Theme::kPadSm);

    auto right = r.removeFromRight (32 + Theme::kPadSm + 80);
    loopButton.setBounds (right.removeFromRight (32).withSizeKeepingCentre (32, 32));
    right.removeFromRight (Theme::kPadSm);
    loopBadge.setBounds (right);

    r.removeFromRight (Theme::kPad);
    totalTime.setBounds (r.removeFromRight (110));
}

} // namespace stemmerizer::ui
